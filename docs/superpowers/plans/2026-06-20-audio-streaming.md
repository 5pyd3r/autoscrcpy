# Audio Streaming Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable audio streaming from Android device to Windows — Opus decode + WASAPI playback, minimal viable audio.

**Architecture:** Patch 10 blocking issues in existing code. Add audio ADB channel between video and control in server.c, read audio metadata (codec_id) before packet loop in audio_socket.c, defer audio player/decoder init until metadata arrives in application.c, fix AAC codec ID bug in audio_decoder.c.

**Tech Stack:** C11, Win32 (WASAPI), FFmpeg (libavcodec), ADB protocol

**Branch:** `feat/audio-streaming` from latest `master`

---

### Task 1: Create feature branch and fix AAC codec ID

**Files:**
- Modify: `src/decode/audio_decoder.c:36`

- [ ] **Step 1: Create feature branch**

```bash
git checkout master
git pull
git checkout -b feat/audio-streaming
```

- [ ] **Step 2: Fix AAC codec ID**

In `src/decode/audio_decoder.c`, line 36, the AAC codec ID is wrong. Change `0x61616320` to `0x00616163`:

```c
// Before (line 36):
        case 0x61616320: // aac
// After:
        case 0x00616163: // aac
```

- [ ] **Step 3: Build and test**

```bash
meson compile -C builddir
meson test -C builddir
```

Expected: All 38 tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/decode/audio_decoder.c
git commit -m "fix: correct AAC codec ID to match scrcpy protocol (0x00616163)"
```

---

### Task 2: Add audio metadata reading to audio_socket

**Files:**
- Modify: `src/device/audio_socket.h`
- Modify: `src/device/audio_socket.c`

- [ ] **Step 1: Add `audio_socket_read_metadata` declaration to header**

In `src/device/audio_socket.h`, add after the `audio_socket_read_packet` declaration (line 17):

```c
bool audio_socket_read_metadata(audio_socket_t *sock);
```

- [ ] **Step 2: Implement `audio_socket_read_metadata` in .c file**

In `src/device/audio_socket.c`, add this function **before** `audio_socket_read_packet` (after line 37):

```c
bool audio_socket_read_metadata(audio_socket_t *sock) {
    /* scrcpy-server sends audio metadata as 4-byte codec_id (big-endian).
     * For audio, sample_rate and channels are hardcoded per scrcpy protocol:
     * Opus/AAC/FLAC: 48000 Hz, 2 channels (stereo). */
    uint8_t buf[4];
    size_t received = 0;
    while (received < 4) {
        int n = recv(sock->fd, (char *)buf + received, 4 - received, 0);
        if (n <= 0) {
            if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
            log_error("Failed to read audio metadata");
            return false;
        }
        received += n;
    }

    sock->codec_id = read32be(buf);

    if (sock->codec_id == 0) {
        log_warn("Audio stream disabled by server");
        return false;
    }
    if (sock->codec_id == 1) {
        log_error("Audio config error reported by server");
        return false;
    }

    /* scrcpy protocol hardcodes stereo 48kHz for all audio codecs */
    sock->sample_rate = 48000;
    sock->channels = 2;

    const char *name = "unknown";
    if (sock->codec_id == 0x6f707573) name = "Opus";
    else if (sock->codec_id == 0x00616163) name = "AAC";
    else if (sock->codec_id == 0x666c6163) name = "FLAC";
    log_info("Audio codec: %s (0x%08x), %u Hz, %u ch",
             name, sock->codec_id, sock->sample_rate, sock->channels);

    return true;
}
```

- [ ] **Step 3: Build**

```bash
meson compile -C builddir
```

Expected: Compiles without errors.

- [ ] **Step 4: Commit**

```bash
git add src/device/audio_socket.h src/device/audio_socket.c
git commit -m "feat: add audio metadata reading (codec_id, sample_rate, channels)"
```

---

### Task 3: Add audio channel to server — ADB reader dispatch

**Files:**
- Modify: `src/device/server.c`

This is the largest change. We modify the `adb_reader_t` struct, the `adb_reader_thread` dispatch, and the channel setup in `server_start()`.

- [ ] **Step 1: Add audio fields to `adb_reader_t` struct**

In `src/device/server.c`, modify the `adb_reader_t` struct (lines 253-261):

```c
typedef struct {
    adb_connection_t *conn;
    adb_channel_t    *video_chan;
    adb_channel_t    *audio_chan;      /* audio channel (may be NULL) */
    adb_channel_t    *ctrl_chan;       /* control channel (may be NULL) */
    SOCKET_T          video_write_fd;  /* write end of video socketpair */
    SOCKET_T          audio_write_fd;  /* write end of audio socketpair */
    SOCKET_T          ctrl_write_fd;   /* write end of control socketpair (app writes here) */
    CRITICAL_SECTION  send_lock;       /* Protects TLS writes */
    volatile int      running;
} adb_reader_t;
```

- [ ] **Step 2: Add audio dispatch in `adb_reader_thread`**

In `adb_reader_thread` (lines 290-344), modify the WRTE dispatch block (lines 301-318). Replace the current dispatch logic:

```c
        if (msg.command == ADB_WRTE) {
            /* Identify which channel this WRTE belongs to */
            if (r->video_chan && r->video_chan->remote_id == 0) {
                r->video_chan->remote_id = msg.arg0;
            }
            if (r->audio_chan && r->audio_chan->remote_id == 0) {
                r->audio_chan->remote_id = msg.arg0;
            }

            /* Send OKAY first for flow control (with lock) */
            EnterCriticalSection(&r->send_lock);
            adb_send_msg_conn(r->conn, ADB_OKAY,
                              msg.arg1, msg.arg0, NULL, 0, 1);
            LeaveCriticalSection(&r->send_lock);

            /* Dispatch data to appropriate socketpair */
            if (r->video_chan && msg.arg0 == r->video_chan->remote_id && msg.data_length > 0) {
                send(r->video_write_fd, (const char *)pl, msg.data_length, 0);
            } else if (r->audio_chan && msg.arg0 == r->audio_chan->remote_id && msg.data_length > 0) {
                send(r->audio_write_fd, (const char *)pl, msg.data_length, 0);
            } else if (r->ctrl_chan && msg.arg0 == r->ctrl_chan->remote_id && msg.data_length > 0) {
                send(r->ctrl_write_fd, (const char *)pl, msg.data_length, 0);
            }
```

- [ ] **Step 3: Add audio channel OKAY logging**

In the OKAY handler (around line 332), add audio channel logging:

```c
            if (r->video_chan && local_id == r->video_chan->local_id) {
                log_info("Video channel OKAY: remote_id=%u", remote_id);
            } else if (r->audio_chan && local_id == r->audio_chan->local_id) {
                log_info("Audio channel OKAY: remote_id=%u", remote_id);
            }
```

- [ ] **Step 4: Build to verify struct changes compile**

```bash
meson compile -C builddir
```

Expected: Compiles without errors (struct changes are compatible since new fields are added at end).

- [ ] **Step 5: Commit**

```bash
git add src/device/server.c
git commit -m "feat: add audio fields to ADB reader and audio dispatch in reader thread"
```

---

### Task 4: Enable audio in scrcpy-server command and open audio channel

**Files:**
- Modify: `src/device/server.c`

- [ ] **Step 1: Fix audio flag and add audio params to server command**

In `server_start()`, modify the `snprintf` call (lines 527-537):

```c
        snprintf(cmd, sizeof(cmd),
                 "CLASSPATH=/data/local/tmp/scrcpy-server "
                 "app_process / com.genymobile.scrcpy.Server 3.3.2 "
                 "tunnel_forward=true "
                 "video=%s audio=%s control=%s "
                 "video_bit_rate=%u audio_bit_rate=%u max_size=%u "
                 "audio_codec=opus",
                 srv->config.video ? "true" : "false",
                 srv->config.audio ? "true" : "false",
                 srv->config.control ? "true" : "false",
                 srv->config.video_bit_rate,
                 srv->config.audio_bit_rate,
                 srv->config.max_size);
```

Note: `audio_codec=opus` is hardcoded since we only support Opus in this iteration. The `server_config` struct has an `audio_encoder` field but it's not wired through from options — that's fine for minimal scope.

- [ ] **Step 2: Open audio channel between video and control**

In `server_start()`, after the video channel is opened (after line 563) and before the control channel block (line 565), insert audio channel logic:

```c
    /* Open audio channel if audio is enabled.
     * scrcpy-server tunnel_forward order: video → audio → control */
    adb_channel_t *audio_chan = NULL;
    if (srv->config.audio) {
        audio_chan = session_open_channel(conn, "localabstract:scrcpy");
        if (!audio_chan) {
            log_error("Failed to open audio channel");
            return false;
        }
    }
```

- [ ] **Step 3: Create audio socketpair**

After the video socketpair creation (after line 580) and before the control socketpair block (line 583), add:

```c
    /* Create socketpair for audio data relay */
    SOCKET_T audio_sp[2] = {INVALID_SOCKFD, INVALID_SOCKFD};
    if (audio_chan) {
        if (create_socketpair(audio_sp) < 0) {
            log_error("Failed to create audio socketpair");
            return false;
        }
        audio_sock->fd = audio_sp[0];
    }
```

- [ ] **Step 4: Wire audio into reader struct**

Modify the reader initialization (lines 593-600) to include audio:

```c
    static adb_reader_t reader;
    reader.conn = conn;
    reader.video_chan = video_chan;
    reader.audio_chan = audio_chan;
    reader.ctrl_chan = ctrl_chan;
    reader.video_write_fd = sp[1];
    reader.audio_write_fd = audio_sp[1];
    reader.ctrl_write_fd = ctrl_sp[1];
    InitializeCriticalSection(&reader.send_lock);
    reader.running = 1;
```

- [ ] **Step 5: Wait for audio channel to open**

After the video channel wait (after line 620) and before the control channel wait (line 623), add:

```c
    /* Wait for audio channel */
    if (audio_chan) {
        for (int i = 0; i < 300; i++) {
            if (audio_chan->state == CHAN_OPEN) break;
            Sleep(100);
        }
        if (audio_chan->state == CHAN_OPEN) {
            log_info("Audio channel open (remote_id=%u)", audio_chan->remote_id);
        } else {
            log_warn("Audio channel did not open");
        }
    }
```

- [ ] **Step 6: Update comment about channel order**

Change the comment at line 556-558 from:
```c
    /* Open channels that the server expects.
     * scrcpy-server with tunnel_forward=true does blocking accept() for each
     * channel in order: video → control (audio disabled). */
```
to:
```c
    /* Open channels that the server expects.
     * scrcpy-server with tunnel_forward=true does blocking accept() for each
     * channel in order: video → audio (if enabled) → control. */
```

- [ ] **Step 7: Build**

```bash
meson compile -C builddir
```

Expected: Compiles without errors.

- [ ] **Step 8: Commit**

```bash
git add src/device/server.c
git commit -m "feat: enable audio in scrcpy-server command and open audio ADB channel"
```

---

### Task 5: Defer audio init and rewrite audio_receiver_thread

**Files:**
- Modify: `src/app/application.c`

- [ ] **Step 1: Remove early audio_player_init from `application_init`**

In `application_init()`, remove lines 119-122 (the `audio_player_init` call and its warning):

```c
// Remove these lines:
    if (!audio_player_init(app->audio_player, 48000, 2)) {
        log_warn("Audio player init failed (WASAPI unavailable)");
        /* Non-fatal: audio just won't work */
    }
```

The `audio_player_create()` call on line 113 stays — it only allocates the struct.

- [ ] **Step 2: Add audio init block in `application_run` after server_start**

In `application_run()`, after `server_start()` succeeds (after line 149) and before the video decoder init block (line 151), add audio initialization:

```c
    /* Initialize audio pipeline if audio channel is available */
    if (app->options.audio && app->audio_sock.fd != INVALID_SOCKFD) {
        if (audio_socket_read_metadata(&app->audio_sock)) {
            if (audio_decoder_init(app->audio_decoder, app->audio_sock.codec_id,
                                   app->audio_sock.sample_rate, app->audio_sock.channels)) {
                if (audio_player_init(app->audio_player, app->audio_sock.sample_rate,
                                      app->audio_sock.channels)) {
                    log_info("Audio pipeline ready");
                } else {
                    log_warn("Audio player init failed (WASAPI unavailable)");
                }
            } else {
                log_error("Audio decoder init failed");
            }
        } else {
            log_warn("Audio metadata read failed, audio disabled");
        }
    }
```

- [ ] **Step 3: Rewrite `audio_receiver_thread`**

Replace the existing `audio_receiver_thread` (lines 44-60) with:

```c
static DWORD WINAPI audio_receiver_thread(LPVOID arg) {
    application_t *app = (application_t *)arg;
    audio_socket_t *sock = &app->audio_sock;
    audio_decoder_t *dec = app->audio_decoder;
    audio_player_t *player = app->audio_player;

    while (app->running) {
        /* 1. Read one audio packet */
        uint8_t *data = NULL;
        uint32_t size = 0;
        if (!audio_socket_read_packet(sock, &data, &size)) {
            if (app->running) log_error("audio socket read failed");
            break;
        }

        /* 2. Decode */
        audio_frame_t aframe;
        memset(&aframe, 0, sizeof(aframe));
        if (!audio_decoder_decode(dec, data, size, &aframe)) {
            free(data);
            continue;  /* skip on decode failure (config packet or error) */
        }
        free(data);

        /* 3. Write directly to WASAPI (no regulator) */
        if (aframe.data) {
            audio_player_write(player, aframe.data, aframe.size);
            free(aframe.data);
        }
    }
    return 0;
}
```

- [ ] **Step 4: Build**

```bash
meson compile -C builddir
```

Expected: Compiles without errors.

- [ ] **Step 5: Commit**

```bash
git add src/app/application.c
git commit -m "feat: defer audio init to metadata arrival and rewrite audio receiver loop"
```

---

### Task 6: Build, test, and verify

- [ ] **Step 1: Full clean build**

```bash
rm -rf builddir
meson setup builddir --native-file meson-native-clang-gcc.ini
meson compile -C builddir
```

Expected: Clean build with no warnings related to audio code.

- [ ] **Step 2: Run all automatic tests**

```bash
meson test -C builddir
```

Expected: All 38 tests pass (audio changes don't affect existing auto tests).

- [ ] **Step 3: Device test (requires connected device)**

```bash
./builddir/tests/test_device.exe <serial>
```

Expected: Audio channel negotiation appears in logs.

- [ ] **Step 4: E2E test — launch full application**

```bash
./builddir/autoscrcpy.exe <serial>
```

Verify in logs:
- "Audio codec: Opus (0x6f707573), 48000 Hz, 2 ch"
- "Audio channel open (remote_id=N)"
- "Audio pipeline ready"
- "Audio player initialized: 48000 Hz, 2 channels"

Verify audibly: device audio plays through Windows speakers.

- [ ] **Step 5: Final commit with any fixes**

If any fixes were needed during testing, commit them:

```bash
git add -A
git commit -m "fix: address audio streaming issues from device testing"
```

---

## Summary

| Task | Files Changed | Blocking Issues Fixed |
|------|--------------|----------------------|
| 1 | `audio_decoder.c` | #5 AAC codec ID |
| 2 | `audio_socket.h`, `audio_socket.c` | #3 No metadata reading |
| 3 | `server.c` | #8 No audio reader dispatch |
| 4 | `server.c` | #1 audio=false hardcode, #2 No audio channel, #9 audio_codec unused, #10 audio_bit_rate unused |
| 5 | `application.c` | #4 Decoder never init, #6 Hardcoded player params |
| 6 | (verification) | All |
