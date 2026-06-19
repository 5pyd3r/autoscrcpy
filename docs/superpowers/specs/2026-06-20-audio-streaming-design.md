# Audio Streaming Feature Design

**Date:** 2026-06-20
**Branch:** `feat/audio-streaming`
**Scope:** 最小可听 — 跑通音频链路，能听到设备声音

## Goal

Enable audio streaming from Android device to Windows via scrcpy-server, using Opus decoding and WASAPI playback.

## Current State

Audio code exists in the codebase but is completely disconnected. 10 specific issues block the audio pipeline:

1. `server.c:534` hardcodes `audio=false`, ignoring `srv->config.audio`
2. No audio ADB channel negotiation (only video + control)
3. No audio metadata reading (codec_id/sample_rate/channels)
4. Audio decoder never initialized (`audio_decoder_init()` never called)
5. AAC codec ID wrong (`0x61616320` should be `0x00616163`)
6. Audio player hardcoded to 48000Hz/2ch
7. Regulator compiled but never wired up
8. ADB reader thread has no audio dispatch
9. `audio_codec` CLI option never passed to server command
10. `audio_bit_rate` never included in server command string

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Scope | 最小可听 | 先跑通链路，后续迭代补全 regulator |
| Codec | Opus only | 绝大多数 Android 设备默认 Opus |
| Init order | 延迟初始化 | 元数据到达后再初始化 decoder/player |
| Architecture | 最小补丁 | 直接修复 10 个阻断点，不改线程模型 |

## Implementation Plan

### 1. ADB Audio Channel (`src/device/server.c`)

**1a. Fix audio flag hardcode (line 534)**

```c
// Before:
"false", /* audio off */
// After:
srv->config.audio ? "true" : "false",
```

Add `audio_bit_rate=%u` and `audio_codec=%s` to the `snprintf` server command.

**1b. Audio channel order**

With `tunnel_forward=true`, scrcpy-server expects channel connections in order: video → audio → control. Insert audio channel between video and control:

```
Current:  video_channel → control_channel
After:    video_channel → [audio_channel if audio] → control_channel
```

After video channel is established, if `srv->config.audio` is true, send another `localabstract:scrcpy` OPEN request and accept to get `audio_socket->fd`.

**1c. ADB reader audio dispatch**

Add `audio_remote_id` and `audio_write_fd` to `adb_reader_t`. In the WRTE message dispatch, add audio branch alongside existing video and control branches.

### 2. Audio Metadata (`src/device/audio_socket.c`)

scrcpy-server sends metadata on the audio channel before media packets:

```
[4B codec_id (big-endian)] → then loop: [12B packet header][packet data]
```

Add `audio_socket_read_metadata()`:
- Read 4-byte codec_id via `read32be()`
- For Opus (0x6f707573): hardcode sample_rate=48000, channels=2 per scrcpy protocol
- Store in `sock->codec_id`, `sock->sample_rate`, `sock->channels`
- Error handling: codec_id=0 means disabled, codec_id=1 means config error

Call in `application_run()` after audio channel is established, before starting `audio_receiver_thread`.

### 3. Decoder + Player Init (`src/decode/audio_decoder.c`, `src/app/application.c`)

**3a. Fix AAC codec ID (audio_decoder.c)**

```c
// Before (wrong):
case 0x61616320: // "aac " — trailing space
// After (correct):
case 0x00616163: // "\0aac" — matches reference scrcpy
```

**3b. Delayed init flow (application.c)**

```
application_init():
  → audio_player_create()    // allocate struct only, no WASAPI init
  → audio_decoder_create()   // allocate struct only, no codec open
  → audio_socket_create()

application_run():
  → server_start()           // establish ADB channels (including audio)
  → audio_socket_read_metadata()  // read codec_id/sr/ch
  → audio_decoder_init(codec_id, sample_rate, channels)  // open Opus decoder
  → audio_player_init(sample_rate, channels)              // init WASAPI
  → start audio_receiver_thread
```

### 4. Audio Receive/Decode Loop (`src/app/application.c`)

Rewrite `audio_receiver_thread`:

```c
static int audio_receiver_thread(void *arg) {
    application_t *app = arg;
    audio_socket_t *sock = &app->audio_sock;
    audio_decoder_t *dec = &app->audio_decoder;
    audio_player_t *player = &app->audio_player;

    while (!app->quit) {
        // 1. Read one audio packet
        audio_packet_t pkt;
        if (!audio_socket_read_packet(sock, &pkt)) {
            if (!app->quit) log_error("audio socket read failed");
            break;
        }

        // 2. Decode
        AVFrame *frame = audio_decoder_decode(dec, pkt.data, pkt.size);
        free(pkt.data);
        if (!frame) continue;  // skip on decode failure

        // 3. Write directly to WASAPI (no regulator)
        audio_player_write(player, frame);
        av_frame_free(&frame);
    }
    return 0;
}
```

### 5. Testing

- **Build**: `meson compile -C builddir` — no regressions
- **Auto tests**: `meson test -C builddir` — all 38 existing cases pass
- **Device test**: `./builddir/tests/test_device.exe <serial>` — audio channel established
- **E2E**: Launch full app, confirm device audio is audible

## Files Changed

| File | Changes |
|------|---------|
| `src/device/server.c` | Fix audio flag, add audio channel, reader dispatch |
| `src/device/audio_socket.c` | Add metadata reading |
| `src/device/audio_socket.h` | Add `audio_socket_read_metadata()` declaration |
| `src/decode/audio_decoder.c` | Fix AAC codec ID |
| `src/app/application.c` | Delayed init, rewrite audio_receiver_thread |
| `src/app/options.c` | Ensure audio_codec/audio_bit_rate passed to server config |

## Out of Scope

- Audio regulator / clock compensation (future iteration)
- AAC / FLAC codec support (future iteration)
- Audio recording
- Audio unit tests
- Clipboard sync
