# AutoScrcpy Comprehensive Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete all remaining modules bottom-up to achieve full scrcpy feature parity on Windows with Win32+DirectX11 rendering and native ADB protocol.

**Architecture:** Layered C architecture completed bottom-up: Platform → ADB → Device → Decode → Render → Input → Control → Record → App. Each layer builds on the previous, with well-defined interfaces between modules.

**Tech Stack:** C11, Meson+Ninja+Clang, FFmpeg (libavcodec/libavformat/libavutil/libswscale), mbedtls, Win32 API, DirectX 11, WASAPI

---

## File Structure

Files to modify or create (relative to project root):

```
src/
├── platform/
│   └── platform.h                  # MODIFY: add platform_sleep_ms
├── adb/
│   ├── adb.h                       # MODIFY: add session_poll, sync types
│   ├── adb.c                       # MODIFY: implement adb_push, adb_forward
│   ├── session.h                   # MODIFY: add session_poll, session_recv_msg
│   ├── session.c                   # MODIFY: add poll, TLS-aware recv, STLS handling
│   ├── protocol.h                  # MODIFY: add TLS-aware send/recv
│   └── protocol.c                  # MODIFY: add TLS-aware send/recv
├── device/
│   ├── server.h                    # MODIFY: redesign to server_t lifecycle
│   ├── server.c                    # MODIFY: full lifecycle management
│   ├── video_socket.h              # MODIFY: add video_socket_accept
│   ├── video_socket.c              # MODIFY: implement accept
│   ├── audio_socket.h              # MODIFY: add audio_socket_accept
│   ├── audio_socket.c              # MODIFY: implement accept
│   ├── control_socket.h            # MODIFY: add control_socket_accept
│   ├── control_socket.c            # MODIFY: implement accept
│   ├── device_msg.h                # MODIFY: add serialization functions
│   └── device_msg.c                # MODIFY: implement serialization
├── decode/
│   ├── video_decoder.h             # MODIFY: add video_frame_free
│   └── video_decoder.c             # MODIFY: NV12 output path
├── render/
│   ├── shader_bytecode.h           # CREATE: pre-compiled shader bytecode
│   ├── shader.h                    # MODIFY: add shader_init_from_bytecode
│   ├── shader.c                    # MODIFY: implement bytecode loading
│   ├── texture.h                   # MODIFY: add NV12 texture functions
│   ├── texture.c                   # MODIFY: implement NV12 texture
│   ├── video_renderer.h            # MODIFY: add renderer_submit_frame
│   └── video_renderer.c            # MODIFY: shader loading, NV12, aspect ratio
├── input/
│   ├── keycode_map.h               # CREATE: VK → Android keycode mapping
│   ├── keycode_map.c               # CREATE: mapping table implementation
│   ├── input_transform.h           # CREATE: coordinate transformation
│   └── input_transform.c           # CREATE: coordinate transform implementation
├── control/
│   ├── control_msg.h               # MODIFY: add serialization function declarations
│   ├── control_msg.c               # MODIFY: implement serialization
│   ├── clipboard.h                 # NO CHANGE
│   ├── clipboard.c                 # NO CHANGE
│   ├── power.h                     # MODIFY: add ADB connection parameter
│   └── power.c                     # MODIFY: implement via ADB shell
├── record/
│   ├── muxer.h                     # NO CHANGE
│   └── muxer.c                     # MODIFY: add AV1/FLAC support
├── audio/
│   ├── player.h                    # CREATE: WASAPI audio player interface
│   └── player.c                    # CREATE: WASAPI audio player implementation
├── app/
│   ├── window.h                    # MODIFY: add input callback types
│   ├── window.c                    # MODIFY: WndProc input handling, resize
│   ├── application.h               # MODIFY: add thread handles, stop_event
│   └── application.c               # MODIFY: event loop, threaded receivers
└── main.c                          # NO CHANGE

tests/
├── test_adb.c                      # MODIFY: add push test
├── test_protocol.c                 # NO CHANGE
├── test_control_msg.c              # CREATE: control message serialization tests
└── test_decode.c                   # CREATE: video decoder NV12 test

tests/meson.build                   # CREATE: test build definitions
```

---

## Task 1: Platform Layer — `platform_sleep_ms`

**Files:**
- Modify: `src/platform/platform.h:64-65`

- [ ] **Step 1: Add `platform_sleep_ms` to `platform.h`**

Add before the `#else` block (after line 35, before `#else`):

```c
    static inline void platform_sleep_ms(uint32_t ms) {
        Sleep(ms);
    }
```

Add before `#endif` in the POSIX block (after line 64):

```c
    static inline void platform_sleep_ms(uint32_t ms) {
        usleep(ms * 1000);
    }
```

Also add `#include <unistd.h>` in the POSIX includes if not already present (it is already there at line 44).

- [ ] **Step 2: Verify build**

Run: `ninja -C builddir`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/platform/platform.h
git commit -m "feat: add platform_sleep_ms helper"
```

---

## Task 2: ADB Module — Binary Write Utilities

**Files:**
- Create: `src/adb/binary.h`

- [ ] **Step 1: Create `src/adb/binary.h`**

These utilities are needed by the sync protocol and control message serialization. Based on the scrcpy reference `util/binary.h`.

```c
#ifndef BINARY_H
#define BINARY_H

#include <stdint.h>

static inline void
write16be(uint8_t *buf, uint16_t value) {
    buf[0] = value >> 8;
    buf[1] = value;
}

static inline void
write32be(uint8_t *buf, uint32_t value) {
    buf[0] = value >> 24;
    buf[1] = value >> 16;
    buf[2] = value >> 8;
    buf[3] = value;
}

static inline void
write64be(uint8_t *buf, uint64_t value) {
    write32be(buf, value >> 32);
    write32be(&buf[4], (uint32_t) value);
}

static inline uint16_t
read16be(const uint8_t *buf) {
    return (uint16_t)((buf[0] << 8) | buf[1]);
}

static inline uint32_t
read32be(const uint8_t *buf) {
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

static inline uint32_t
read32le(const uint8_t *buf) {
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

static inline void
write32le(uint8_t *buf, uint32_t value) {
    buf[0] = value;
    buf[1] = value >> 8;
    buf[2] = value >> 16;
    buf[3] = value >> 24;
}

/* Convert float [0.0, 1.0] to uint16 fixed-point [0, 0xFFFF] */
static inline uint16_t
float_to_u16fp(float f) {
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    return (uint16_t)(f * 0xFFFF);
}

/* Convert float [-1.0, 1.0] to int16 fixed-point [-0x8000, 0x7FFF] */
static inline int16_t
float_to_i16fp(float f) {
    if (f < -1.0f) f = -1.0f;
    if (f > 1.0f) f = 1.0f;
    return (int16_t)(f * 0x7FFF);
}

#endif /* BINARY_H */
```

- [ ] **Step 2: Verify build**

Run: `ninja -C builddir`
Expected: Build succeeds (header-only, no .c file needed)

- [ ] **Step 3: Commit**

```bash
git add src/adb/binary.h
git commit -m "feat: add binary write/read utilities for protocol encoding"
```

---

## Task 3: ADB Module — `session_poll` and TLS-aware Message Handling

**Files:**
- Modify: `src/adb/session.h`
- Modify: `src/adb/session.c`
- Modify: `src/adb/protocol.h`
- Modify: `src/adb/protocol.c`

- [ ] **Step 1: Add `session_poll` and `session_recv_msg` to `session.h`**

Add after `session_disconnect` declaration (after line 26):

```c
/* Non-blocking poll: process pending ADB messages */
int session_poll(adb_connection_t *conn, int timeout_ms);

/* Receive one message (TLS-aware), blocking */
int session_recv_msg(adb_connection_t *conn, adb_message_t *out_hdr,
                     uint8_t *out_payload, int max_payload);
```

- [ ] **Step 2: Add TLS-aware send/recv to `protocol.h`**

Add after the existing `adb_recv_msg_tls` declaration:

```c
/* TLS-aware send (uses conn->tls_ctx if set) */
int adb_send_msg_conn(adb_connection_t *conn, uint32_t cmd, uint32_t arg0,
                      uint32_t arg1, const uint8_t *data, uint32_t data_len,
                      int skip_checksum);

/* TLS-aware recv (uses conn->tls_ctx if set) */
int adb_recv_msg_conn(adb_connection_t *conn, adb_message_t *out_hdr,
                      uint8_t *out_payload, int max_payload, int skip_checksum);
```

Note: `protocol.h` needs to include `adb.h` for `adb_connection_t` — it already does via `#include "adb.h"`.

- [ ] **Step 3: Implement TLS-aware send/recv in `protocol.c`**

Add at the end of `protocol.c`:

```c
#include "tls.h"

int adb_send_msg_conn(adb_connection_t *conn, uint32_t cmd, uint32_t arg0,
                      uint32_t arg1, const uint8_t *data, uint32_t data_len,
                      int skip_checksum) {
    if (conn->tls_ctx) {
        return adb_send_msg_tls(conn->tls_ctx, conn->fd, cmd, arg0, arg1,
                                data, data_len, skip_checksum);
    }
    return adb_send_msg(conn->fd, cmd, arg0, arg1, data, data_len, skip_checksum);
}

int adb_recv_msg_conn(adb_connection_t *conn, adb_message_t *out_hdr,
                      uint8_t *out_payload, int max_payload, int skip_checksum) {
    if (conn->tls_ctx) {
        return adb_recv_msg_tls(conn->tls_ctx, conn->fd, out_hdr,
                                out_payload, max_payload, skip_checksum);
    }
    return adb_recv_msg(conn->fd, out_hdr, out_payload, max_payload, skip_checksum);
}
```

- [ ] **Step 4: Implement `session_poll` and `session_recv_msg` in `session.c`**

Add at the end of `session.c`, before `session_disconnect`:

```c
#include "tls.h"
#include <sys/select.h>  /* Already included via platform.h */

int session_recv_msg(adb_connection_t *conn, adb_message_t *out_hdr,
                     uint8_t *out_payload, int max_payload) {
    return adb_recv_msg_conn(conn, out_hdr, out_payload, max_payload,
                             conn->protocol_version >= ADB_VERSION_SKIP_CHECKSUM);
}

int session_poll(adb_connection_t *conn, int timeout_ms) {
    if (conn->fd == INVALID_SOCKFD) return -1;

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(conn->fd, &read_fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select((int)conn->fd + 1, &read_fds, NULL, NULL, &tv);
    if (ret < 0) {
        log_error("select() failed: %d", SOCKET_ERRNO);
        return -1;
    }
    if (ret == 0) return 0; /* timeout */

    /* Read one message */
    adb_message_t hdr;
    uint8_t payload[ADB_MAX_PAYLOAD];
    if (session_recv_msg(conn, &hdr, payload, sizeof(payload)) < 0) {
        return -1;
    }

    session_handle_message(conn, &hdr, payload);
    return 1;
}
```

- [ ] **Step 5: Update `session_handle_message` to handle STLS**

In `session.c`, add a case for `ADB_STLS` in the `session_handle_message` switch (after the `ADB_AUTH` case, around line 113):

```c
        case ADB_STLS:
            log_info("Server requested TLS");
            conn->state = ADB_STATE_TLS_NEGOTIATING;
            /* Send STLS response */
            adb_send_msg_conn(conn, ADB_STLS, 0, 0, NULL, 0, 1);
            /* Perform TLS handshake */
            conn->tls_ctx = tls_handshake(conn->fd);
            if (conn->tls_ctx) {
                log_info("TLS handshake successful");
                /* Re-send CNXN over TLS */
                session_send_cnxn(conn);
            } else {
                log_error("TLS handshake failed");
            }
            break;
```

- [ ] **Step 6: Update `session_send_cnxn` to use TLS-aware send**

Replace the `adb_send_msg` call in `session_send_cnxn` with `adb_send_msg_conn`:

```c
void session_send_cnxn(adb_connection_t *conn) {
    const char *banner = ADB_BANNER;
    int ret = adb_send_msg_conn(conn, ADB_CNXN, ADB_VERSION, ADB_MAX_PAYLOAD,
                                (const uint8_t *)banner, (uint32_t)(strlen(banner) + 1), 1);
    if (ret < 0) {
        log_error("Failed to send CNXN message");
    }
    conn->cnxn_sent = 1;
}
```

- [ ] **Step 7: Update all `adb_send_msg` calls in `session.c` to use `adb_send_msg_conn`**

Replace all remaining `adb_send_msg(conn->fd, ...)` calls in `session_handle_message` with `adb_send_msg_conn(conn, ...)`.

- [ ] **Step 8: Verify build**

Run: `ninja -C builddir`
Expected: Build succeeds

- [ ] **Step 9: Commit**

```bash
git add src/adb/session.h src/adb/session.c src/adb/protocol.h src/adb/protocol.c
git commit -m "feat: add session_poll, TLS-aware message handling, STLS support"
```

---

## Task 4: ADB Module — `adb_push` (File Push via Sync Protocol)

**Files:**
- Modify: `src/adb/adb.c`

- [ ] **Step 1: Implement `adb_push` in `adb.c`**

Replace the stub `adb_push` function (lines 111-118) with:

```c
#include "binary.h"
#include <stdio.h>

/* ADB sync protocol constants */
#define SYNC_SEND  0x444e4553  /* "SEND" */
#define SYNC_DATA  0x41544144  /* "DATA" */
#define SYNC_DONE  0x454e4f44  /* "DONE" */
#define SYNC_OKAY  0x59414b4f  /* "OKAY" */
#define SYNC_FAIL  0x4c494146  /* "FAIL" */

#define SYNC_MAX_CHUNK (64 * 1024)

static bool adb_sync_send(adb_connection_t *conn, adb_channel_t *chan,
                          const char *remote_path, FILE *local_file) {
    /* Wait for channel to open */
    int retries = 100;
    while (chan->state == CHAN_OPENING && retries > 0) {
        session_poll(conn, 50);
        retries--;
    }
    if (chan->state != CHAN_OPEN) {
        log_error("Sync channel did not open");
        return false;
    }

    /* Send SEND command: "SEND" + path_length(4LE) + path */
    uint8_t cmd_buf[4 + 4 + 512];
    memcpy(cmd_buf, "SEND", 4);
    uint32_t path_len = (uint32_t)strlen(remote_path);
    write32le(cmd_buf + 4, path_len);
    memcpy(cmd_buf + 8, remote_path, path_len);

    /* Send via WRTE to the channel */
    adb_send_msg_conn(conn, ADB_WRTE, chan->local_id, chan->remote_id,
                      cmd_buf, 8 + path_len, 1);
    /* Wait for OKAY */
    session_poll(conn, 100);

    /* Stream file data */
    uint8_t chunk_buf[4 + 4 + SYNC_MAX_CHUNK];
    while (!feof(local_file)) {
        size_t nread = fread(chunk_buf + 8, 1, SYNC_MAX_CHUNK, local_file);
        if (nread == 0) break;

        memcpy(chunk_buf, "DATA", 4);
        write32le(chunk_buf + 4, (uint32_t)nread);

        adb_send_msg_conn(conn, ADB_WRTE, chan->local_id, chan->remote_id,
                          chunk_buf, 8 + (uint32_t)nread, 1);
        session_poll(conn, 50);
    }

    /* Send DONE: "DONE" + mtime(4LE) */
    memcpy(chunk_buf, "DONE", 4);
    write32le(chunk_buf + 4, 0); /* mtime */
    adb_send_msg_conn(conn, ADB_WRTE, chan->local_id, chan->remote_id,
                      chunk_buf, 8, 1);

    /* Wait for OKAY response */
    retries = 200;
    while (retries > 0) {
        int r = session_poll(conn, 100);
        if (r > 0) break;
        retries--;
    }

    return true;
}

bool adb_push(adb_connection_t *conn, const char *local, const char *remote) {
    if (!conn || conn->state != ADB_STATE_CONNECTED) {
        log_error("Not connected");
        return false;
    }

    FILE *fp = fopen(local, "rb");
    if (!fp) {
        log_error("Failed to open local file: %s", local);
        return false;
    }

    adb_channel_t *chan = session_open_channel(conn, "sync:");
    if (!chan) {
        log_error("Failed to open sync channel");
        fclose(fp);
        return false;
    }

    bool ret = adb_sync_send(conn, chan, remote, fp);

    session_close_channel(conn, chan);
    fclose(fp);

    if (ret) {
        log_info("Pushed %s -> %s", local, remote);
    } else {
        log_error("Failed to push %s -> %s", local, remote);
    }

    return ret;
}
```

- [ ] **Step 2: Verify build**

Run: `ninja -C builddir`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/adb/adb.c
git commit -m "feat: implement adb_push via sync protocol"
```

---

## Task 5: ADB Module — `adb_forward` (Tunnel Forward)

**Files:**
- Modify: `src/adb/adb.c`

- [ ] **Step 1: Implement `adb_forward` in `adb.c`**

Replace the stub `adb_forward` function (lines 120-127) with:

```c
bool adb_forward(adb_connection_t *conn, uint16_t local_port, const char *remote_spec) {
    if (!conn || conn->state != ADB_STATE_CONNECTED) {
        log_error("Not connected");
        return false;
    }

    /* For tunnel_forward mode, we open a "tcp:<port>" channel.
     * The scrcpy-server connects to our local port, so we don't
     * need ADB's host-serial:forward command. */
    char service[SERVICE_NAME_MAX];
    snprintf(service, sizeof(service), "tcp:%u", local_port);

    adb_channel_t *chan = session_open_channel(conn, service);
    if (!chan) {
        log_error("Failed to open forward channel: %s", service);
        return false;
    }

    log_info("Forward channel opened: %s -> %s", service, remote_spec);
    return true;
}
```

- [ ] **Step 2: Verify build**

Run: `ninja -C builddir`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/adb/adb.c
git commit -m "feat: implement adb_forward for tunnel_forward mode"
```

---

## Task 6: ADB Tests

**Files:**
- Modify: `tests/test_adb.c`

- [ ] **Step 1: Add push test to `test_adb.c`**

Replace `test_adb.c` with:

```c
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../src/adb/adb.h"

void test_adb_init(void) {
    assert(adb_init() == true);
    adb_destroy();
    printf("test_adb_init passed\n");
}

void test_adb_connect(void) {
    /* This test requires a running ADB server */
    printf("test_adb_connect skipped (requires ADB server)\n");
}

void test_adb_push_no_conn(void) {
    /* Test that push fails gracefully with NULL connection */
    assert(adb_push(NULL, "/tmp/test", "/data/local/tmp/test") == false);
    printf("test_adb_push_no_conn passed\n");
}

void test_adb_forward_no_conn(void) {
    /* Test that forward fails gracefully with NULL connection */
    assert(adb_forward(NULL, 5555, "tcp:5555") == false);
    printf("test_adb_forward_no_conn passed\n");
}

int main(void) {
    test_adb_init();
    test_adb_connect();
    test_adb_push_no_conn();
    test_adb_forward_no_conn();
    return 0;
}
```

- [ ] **Step 2: Verify build and tests**

Run: `ninja -C builddir && ./builddir/tests/test_adb`
Expected: All tests pass

- [ ] **Step 3: Commit**

```bash
git add tests/test_adb.c
git commit -m "test: add adb_push and adb_forward edge case tests"
```

---

## Task 7: Device Module — Socket Accept Functions

**Files:**
- Modify: `src/device/video_socket.h`
- Modify: `src/device/video_socket.c`
- Modify: `src/device/audio_socket.h`
- Modify: `src/device/audio_socket.c`
- Modify: `src/device/control_socket.h`
- Modify: `src/device/control_socket.c`

- [ ] **Step 1: Add `video_socket_accept` to `video_socket.h`**

Add after `video_socket_init` declaration:

```c
bool video_socket_accept(video_socket_t *sock, SOCKET_T listen_fd);
```

- [ ] **Step 2: Implement `video_socket_accept` in `video_socket.c`**

Add before `video_socket_read_packet`:

```c
bool video_socket_accept(video_socket_t *sock, SOCKET_T listen_fd) {
    struct sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    SOCKET_T client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd == INVALID_SOCKFD) {
        log_error("Failed to accept video connection");
        return false;
    }

    /* Read dummy byte (scrcpy protocol) */
    uint8_t dummy;
    int n = recv(client_fd, &dummy, 1, 0);
    if (n != 1) {
        log_error("Failed to read video dummy byte");
        CLOSESOCKET(client_fd);
        return false;
    }

    sock->fd = client_fd;
    sock->codec_id = 0;
    sock->width = 0;
    sock->height = 0;
    return true;
}
```

- [ ] **Step 3: Add `audio_socket_accept` to `audio_socket.h`**

Add after `audio_socket_init` declaration:

```c
bool audio_socket_accept(audio_socket_t *sock, SOCKET_T listen_fd);
```

- [ ] **Step 4: Implement `audio_socket_accept` in `audio_socket.c`**

Add before `audio_socket_read_packet`:

```c
bool audio_socket_accept(audio_socket_t *sock, SOCKET_T listen_fd) {
    struct sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    SOCKET_T client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd == INVALID_SOCKFD) {
        log_error("Failed to accept audio connection");
        return false;
    }

    uint8_t dummy;
    int n = recv(client_fd, &dummy, 1, 0);
    if (n != 1) {
        log_error("Failed to read audio dummy byte");
        CLOSESOCKET(client_fd);
        return false;
    }

    sock->fd = client_fd;
    sock->codec_id = 0;
    sock->sample_rate = 0;
    sock->channels = 0;
    return true;
}
```

- [ ] **Step 5: Add `control_socket_accept` to `control_socket.h`**

Add after `control_socket_init` declaration:

```c
bool control_socket_accept(control_socket_t *sock, SOCKET_T listen_fd);
```

- [ ] **Step 6: Implement `control_socket_accept` in `control_socket.c`**

Add before `control_socket_send_msg`:

```c
bool control_socket_accept(control_socket_t *sock, SOCKET_T listen_fd) {
    struct sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    SOCKET_T client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd == INVALID_SOCKFD) {
        log_error("Failed to accept control connection");
        return false;
    }

    uint8_t dummy;
    int n = recv(client_fd, &dummy, 1, 0);
    if (n != 1) {
        log_error("Failed to read control dummy byte");
        CLOSESOCKET(client_fd);
        return false;
    }

    sock->fd = client_fd;
    return true;
}
```

- [ ] **Step 7: Verify build**

Run: `ninja -C builddir`
Expected: Build succeeds

- [ ] **Step 8: Commit**

```bash
git add src/device/video_socket.h src/device/video_socket.c
git add src/device/audio_socket.h src/device/audio_socket.c
git add src/device/control_socket.h src/device/control_socket.c
git commit -m "feat: add socket accept functions for device connections"
```

---

## Task 8: Device Module — Server Lifecycle Management

**Files:**
- Modify: `src/device/server.h`
- Modify: `src/device/server.c`

- [ ] **Step 1: Redesign `server.h`**

Replace `server.h` contents:

```c
#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include "../platform/platform.h"
#include "video_socket.h"
#include "audio_socket.h"
#include "control_socket.h"

struct server_config {
    const char *serial;
    const char *server_path;
    uint16_t local_port;
    uint32_t max_size;
    uint32_t video_bit_rate;
    uint32_t audio_bit_rate;
    const char *video_encoder;
    const char *audio_encoder;
    bool control;
    bool video;
    bool audio;
};

typedef struct server {
    struct server_config config;
    SOCKET_T listen_fd;
    void *adb_conn;  /* adb_connection_t* */
    bool running;
} server_t;

bool server_init(server_t *srv, const struct server_config *config);
bool server_start(server_t *srv, video_socket_t *video_sock,
                  audio_socket_t *audio_sock, control_socket_t *control_sock);
void server_kill(server_t *srv);
void server_destroy(server_t *srv);

#endif /* SERVER_H */
```

- [ ] **Step 2: Implement `server.c`**

Replace `server.c` contents:

```c
#include "server.h"
#include "../adb/adb.h"
#include "../platform/log.h"
#include <string.h>
#include <stdio.h>

bool server_init(server_t *srv, const struct server_config *config) {
    srv->config = *config;
    srv->listen_fd = INVALID_SOCKFD;
    srv->adb_conn = NULL;
    srv->running = false;
    return true;
}

static SOCKET_T create_listen_socket(uint16_t port) {
    SOCKET_T fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKFD) {
        log_error("Failed to create listen socket");
        return INVALID_SOCKFD;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("Failed to bind to port %u", port);
        CLOSESOCKET(fd);
        return INVALID_SOCKFD;
    }

    if (listen(fd, 3) < 0) {
        log_error("Failed to listen on port %u", port);
        CLOSESOCKET(fd);
        return INVALID_SOCKFD;
    }

    return fd;
}

bool server_start(server_t *srv, video_socket_t *video_sock,
                  audio_socket_t *audio_sock, control_socket_t *control_sock) {
    /* 1. Connect to ADB */
    adb_connection_t *conn = adb_connect("127.0.0.1", 5037);
    if (!conn) {
        log_error("Failed to connect to ADB daemon");
        return false;
    }
    srv->adb_conn = conn;

    /* 2. Push scrcpy-server */
    if (srv->config.server_path) {
        if (!adb_push(conn, srv->config.server_path,
                      "/data/local/tmp/scrcpy-server.jar")) {
            log_error("Failed to push scrcpy-server");
            return false;
        }
    }

    /* 3. Create local listener */
    srv->listen_fd = create_listen_socket(srv->config.local_port);
    if (srv->listen_fd == INVALID_SOCKFD) {
        return false;
    }

    /* 4. Start scrcpy-server via ADB shell */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "CLASSPATH=/data/local/tmp/scrcpy-server.jar "
             "app_process / com.genymobile.scrcpy.Server "
             "scid=%08x "
             "log_level=info "
             "max_size=%u "
             "video_bit_rate=%u "
             "audio_bit_rate=%u "
             "video=%s "
             "audio=%s "
             "control=%s "
             "tunnel_forward=true "
             "send_dummy_byte=true",
             0,
             srv->config.max_size,
             srv->config.video_bit_rate,
             srv->config.audio_bit_rate,
             srv->config.video ? "true" : "false",
             srv->config.audio ? "true" : "false",
             srv->config.control ? "true" : "false");

    if (!adb_shell(conn, cmd)) {
        log_error("Failed to start scrcpy-server");
        return false;
    }

    /* 5. Wait for connections */
    log_info("Waiting for device connections on port %u...", srv->config.local_port);

    /* Video socket (always first) */
    if (srv->config.video) {
        if (!video_socket_accept(video_sock, srv->listen_fd)) {
            log_error("Failed to accept video socket");
            return false;
        }
        log_info("Video socket connected");
    }

    /* Audio socket (second) */
    if (srv->config.audio) {
        if (!audio_socket_accept(audio_sock, srv->listen_fd)) {
            log_error("Failed to accept audio socket");
            return false;
        }
        log_info("Audio socket connected");
    }

    /* Control socket (third) */
    if (srv->config.control) {
        if (!control_socket_accept(control_sock, srv->listen_fd)) {
            log_error("Failed to accept control socket");
            return false;
        }
        log_info("Control socket connected");
    }

    srv->running = true;
    return true;
}

void server_kill(server_t *srv) {
    if (srv->adb_conn) {
        adb_shell((adb_connection_t *)srv->adb_conn,
                  "pkill -f com.genymobile.scrcpy.Server");
    }
    srv->running = false;
}

void server_destroy(server_t *srv) {
    if (srv->listen_fd != INVALID_SOCKFD) {
        CLOSESOCKET(srv->listen_fd);
        srv->listen_fd = INVALID_SOCKFD;
    }
    if (srv->adb_conn) {
        adb_disconnect((adb_connection_t *)srv->adb_conn);
        srv->adb_conn = NULL;
    }
}
```

- [ ] **Step 3: Update `application.c` to use new server API**

In `src/app/application.c`, update the includes and `application_run` to use the new `server_t` type. Replace the `server_push` + `server_start` calls with:

```c
#include "../device/server.h"

/* In application_run: */
server_t srv;
server_init(&srv, &server_cfg);
if (!server_start(&srv, &app->video_sock, &app->audio_sock, &app->control_sock)) {
    log_error("Failed to start server");
    return 1;
}
```

Add `server_t srv;` to `application_t` struct in `application.h`, or keep it local to `application_run`.

- [ ] **Step 4: Verify build**

Run: `ninja -C builddir`
Expected: Build succeeds

- [ ] **Step 5: Commit**

```bash
git add src/device/server.h src/device/server.c src/app/application.c
git commit -m "feat: implement server lifecycle management with socket accept"
```

---

## Task 9: Device Module — Device Message Serialization

**Files:**
- Modify: `src/device/device_msg.h`
- Modify: `src/device/device_msg.c`

- [ ] **Step 1: Add serialization declarations to `device_msg.h`**

Add after `device_msg_destroy` declaration:

```c
/* Serialize a clipboard message into buf. Returns bytes written, or -1 on error. */
int device_msg_serialize_clipboard(const char *text, uint32_t len,
                                   uint64_t sequence, uint8_t *buf, uint32_t buf_size);

/* Serialize an ack_clipboard message into buf. Returns bytes written, or -1. */
int device_msg_serialize_ack_clipboard(uint64_t sequence,
                                        uint8_t *buf, uint32_t buf_size);
```

- [ ] **Step 2: Implement serialization in `device_msg.c`**

Add at the end of `device_msg.c`:

```c
#include "../adb/binary.h"

int device_msg_serialize_clipboard(const char *text, uint32_t len,
                                   uint64_t sequence, uint8_t *buf, uint32_t buf_size) {
    /* type(1) + sequence(8) + len(4) + text */
    uint32_t total = 1 + 8 + 4 + len;
    if (total > buf_size) return -1;

    buf[0] = DEVICE_MSG_TYPE_CLIPBOARD;
    write64be(&buf[1], sequence);
    write32be(&buf[9], len);
    memcpy(&buf[13], text, len);
    return (int)total;
}

int device_msg_serialize_ack_clipboard(uint64_t sequence,
                                        uint8_t *buf, uint32_t buf_size) {
    /* type(1) + sequence(8) */
    if (buf_size < 9) return -1;

    buf[0] = DEVICE_MSG_TYPE_ACK_CLIPBOARD;
    write64be(&buf[1], sequence);
    return 9;
}
```

- [ ] **Step 3: Verify build**

Run: `ninja -C builddir`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add src/device/device_msg.h src/device/device_msg.c
git commit -m "feat: add device message serialization functions"
```

---

## Task 10: Decode Module — NV12 Output Path

**Files:**
- Modify: `src/decode/video_decoder.h`
- Modify: `src/decode/video_decoder.c`

- [ ] **Step 1: Add `video_frame_free` to `video_decoder.h`**

Add after `video_decoder_decode` declaration:

```c
/* Free frame data allocated by video_decoder_decode */
void video_frame_free(video_frame_t *frame);
```

- [ ] **Step 2: Implement NV12 output and `video_frame_free` in `video_decoder.c`**

Replace `video_decoder_decode` (lines 71-115) with:

```c
bool video_decoder_decode(video_decoder_t *decoder, const uint8_t *data,
                          uint32_t size, video_frame_t *frame) {
    decoder->packet->data = (uint8_t *)data;
    decoder->packet->size = size;

    int ret = avcodec_send_packet(decoder->codec_ctx, decoder->packet);
    if (ret < 0) {
        log_error("Failed to send packet to decoder");
        return false;
    }

    ret = avcodec_receive_frame(decoder->codec_ctx, decoder->frame);
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) return false;
        log_error("Failed to receive frame from decoder");
        return false;
    }

    uint32_t w = decoder->frame->width;
    uint32_t h = decoder->frame->height;

    frame->width = w;
    frame->height = h;
    frame->format = 0; /* NV12 */

    /* NV12 buffer: Y plane (w*h) + UV plane (w*h/2) */
    uint32_t nv12_size = w * h + w * (h / 2);
    frame->data = malloc(nv12_size);
    if (!frame->data) {
        log_error("Failed to allocate NV12 frame buffer");
        return false;
    }

    /* Copy Y plane */
    uint8_t *y_dst = frame->data;
    const uint8_t *y_src = decoder->frame->data[0];
    for (uint32_t row = 0; row < h; row++) {
        memcpy(y_dst + row * w, y_src + row * decoder->frame->linesize[0], w);
    }

    /* Interleave UV planes into NV12 format */
    uint8_t *uv_dst = frame->data + w * h;
    const uint8_t *u_src = decoder->frame->data[1];
    const uint8_t *v_src = decoder->frame->data[2];
    int uv_stride = decoder->frame->linesize[1];
    for (uint32_t row = 0; row < h / 2; row++) {
        for (uint32_t col = 0; col < w / 2; col++) {
            uv_dst[row * w + col * 2 + 0] = u_src[row * uv_stride + col];
            uv_dst[row * w + col * 2 + 1] = v_src[row * uv_stride + col];
        }
    }

    return true;
}

void video_frame_free(video_frame_t *frame) {
    if (frame && frame->data) {
        free(frame->data);
        frame->data = NULL;
    }
}
```

Add at the top of `video_decoder.c` if not already present:

```c
#include <stdlib.h>
```

- [ ] **Step 3: Create `tests/test_decode.c`**

```c
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "../src/decode/video_decoder.h"

void test_video_frame_free(void) {
    video_frame_t frame;
    frame.data = malloc(100);
    frame.width = 10;
    frame.height = 10;
    frame.format = 0;

    video_frame_free(&frame);
    assert(frame.data == NULL);
    printf("test_video_frame_free passed\n");
}

void test_video_decoder_create(void) {
    video_decoder_t *dec = video_decoder_create();
    assert(dec != NULL);
    video_decoder_destroy(dec);
    printf("test_video_decoder_create passed\n");
}

int main(void) {
    test_video_frame_free();
    test_video_decoder_create();
    return 0;
}
```

- [ ] **Step 4: Verify build**

Run: `ninja -C builddir`
Expected: Build succeeds

- [ ] **Step 5: Commit**

```bash
git add src/decode/video_decoder.h src/decode/video_decoder.c tests/test_decode.c
git commit -m "feat: implement NV12 output path for video decoder"
```

---

## Task 11: Render Module — Shader Bytecode and NV12 Texture

**Files:**
- Create: `src/render/shader_bytecode.h`
- Modify: `src/render/shader.h`
- Modify: `src/render/shader.c`
- Modify: `src/render/texture.h`
- Modify: `src/render/texture.c`
- Modify: `src/render/video_renderer.h`
- Modify: `src/render/video_renderer.c`

- [ ] **Step 1: Create `shader_bytecode.h` with embedded shader bytecode**

This file contains pre-compiled HLSL shader bytecode as static C arrays. For now, use a simple BGRA passthrough shader. The NV12 shader will be added when the HLSL is compiled.

```c
#ifndef SHADER_BYTECODE_H
#define SHADER_BYTECODE_H

#include <stdint.h>

/* Simple Vertex Shader (vs_4_0) - compiled with fxc.exe */
/* Input: POSITION(float3) + TEXCOORD(float2) */
/* Output: SV_POSITION + TEXCOORD */
static const uint8_t vs_bytecode[] = {
    /* Placeholder: replace with actual fxc output.
     * For now, shader_init will fail gracefully if bytecode is invalid. */
    0x00, 0x00, 0x00, 0x00
};
static const uint32_t vs_bytecode_size = sizeof(vs_bytecode);

/* Simple Pixel Shader (ps_4_0) - BGRA passthrough */
static const uint8_t ps_bgra_bytecode[] = {
    0x00, 0x00, 0x00, 0x00
};
static const uint32_t ps_bgra_bytecode_size = sizeof(ps_bgra_bytecode);

/* NV12 Pixel Shader (ps_4_0) - YUV to BGRA conversion */
static const uint8_t ps_nv12_bytecode[] = {
    0x00, 0x00, 0x00, 0x00
};
static const uint32_t ps_nv12_bytecode_size = sizeof(ps_nv12_bytecode);

#endif /* SHADER_BYTECODE_H */
```

**NOTE:** These are placeholder bytecodes. To get real bytecodes:
1. Install Windows SDK (includes `fxc.exe`)
2. Run: `fxc /T vs_4_0 /E main /Fo VertexShader.cso VertexShader.hlsl`
3. Run: `fxc /T ps_4_0 /E main /Fo PixelShader.cso PixelShader.hlsl`
4. Convert: `xxd -i VertexShader.cso > vs_bytecode.h`
5. Replace the placeholder arrays above with real bytecode

- [ ] **Step 2: Update `shader.h` with bytecode init**

Add after `shader_init` declaration:

```c
bool shader_init_from_bytecode(shader_t *shader, ID3D11Device *device,
                                const void *vs_data, uint32_t vs_size,
                                const void *ps_data, uint32_t ps_size);
```

- [ ] **Step 3: Implement `shader_init_from_bytecode` in `shader.c`**

Add after the existing `shader_init` function:

```c
bool shader_init_from_bytecode(shader_t *shader, ID3D11Device *device,
                                const void *vs_data, uint32_t vs_size,
                                const void *ps_data, uint32_t ps_size) {
    HRESULT hr;

    /* Create vertex shader */
    hr = device->lpVtbl->CreateVertexShader(device, vs_data, vs_size, NULL, &shader->vs);
    if (FAILED(hr)) {
        log_error("Failed to create vertex shader from bytecode: 0x%08x", hr);
        return false;
    }

    /* Create pixel shader */
    hr = device->lpVtbl->CreatePixelShader(device, ps_data, ps_size, NULL, &shader->ps);
    if (FAILED(hr)) {
        log_error("Failed to create pixel shader from bytecode: 0x%08x", hr);
        return false;
    }

    /* Create input layout: POSITION(float3) + TEXCOORD(float2) */
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    hr = device->lpVtbl->CreateInputLayout(device, layout, 2,
                                            vs_data, vs_size, &shader->layout);
    if (FAILED(hr)) {
        log_error("Failed to create input layout: 0x%08x", hr);
        return false;
    }

    return true;
}
```

- [ ] **Step 4: Add NV12 texture functions to `texture.h`**

Add after `texture_destroy` declaration:

```c
bool texture_init_nv12(texture_t *y_tex, texture_t *uv_tex,
                       ID3D11Device *device, uint32_t width, uint32_t height);

bool texture_update_nv12(texture_t *y_tex, texture_t *uv_tex,
                         ID3D11DeviceContext *ctx,
                         const uint8_t *nv12_data, uint32_t width, uint32_t height);
```

- [ ] **Step 5: Implement NV12 texture in `texture.c`**

Add at the end of `texture.c`:

```c
bool texture_init_nv12(texture_t *y_tex, texture_t *uv_tex,
                       ID3D11Device *device, uint32_t width, uint32_t height) {
    /* Y plane: R8_UNORM, full resolution */
    D3D11_TEXTURE2D_DESC y_desc = {0};
    y_desc.Width = width;
    y_desc.Height = height;
    y_desc.MipLevels = 1;
    y_desc.ArraySize = 1;
    y_desc.Format = DXGI_FORMAT_R8_UNORM;
    y_desc.SampleDesc.Count = 1;
    y_desc.Usage = D3D11_USAGE_DYNAMIC;
    y_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    y_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = device->lpVtbl->CreateTexture2D(device, &y_desc, NULL, &y_tex->texture);
    if (FAILED(hr)) {
        log_error("Failed to create Y texture: 0x%08x", hr);
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC y_srv_desc = {0};
    y_srv_desc.Format = y_desc.Format;
    y_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    y_srv_desc.Texture2D.MipLevels = 1;
    hr = device->lpVtbl->CreateShaderResourceView(device, y_tex->texture, &y_srv_desc, &y_tex->srv);
    if (FAILED(hr)) {
        log_error("Failed to create Y SRV: 0x%08x", hr);
        return false;
    }
    y_tex->width = width;
    y_tex->height = height;

    /* UV plane: R8G8_UNORM, half resolution */
    D3D11_TEXTURE2D_DESC uv_desc = {0};
    uv_desc.Width = width / 2;
    uv_desc.Height = height / 2;
    uv_desc.MipLevels = 1;
    uv_desc.ArraySize = 1;
    uv_desc.Format = DXGI_FORMAT_R8G8_UNORM;
    uv_desc.SampleDesc.Count = 1;
    uv_desc.Usage = D3D11_USAGE_DYNAMIC;
    uv_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    uv_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = device->lpVtbl->CreateTexture2D(device, &uv_desc, NULL, &uv_tex->texture);
    if (FAILED(hr)) {
        log_error("Failed to create UV texture: 0x%08x", hr);
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC uv_srv_desc = {0};
    uv_srv_desc.Format = uv_desc.Format;
    uv_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    uv_srv_desc.Texture2D.MipLevels = 1;
    hr = device->lpVtbl->CreateShaderResourceView(device, uv_tex->texture, &uv_srv_desc, &uv_tex->srv);
    if (FAILED(hr)) {
        log_error("Failed to create UV SRV: 0x%08x", hr);
        return false;
    }
    uv_tex->width = width / 2;
    uv_tex->height = height / 2;

    return true;
}

bool texture_update_nv12(texture_t *y_tex, texture_t *uv_tex,
                         ID3D11DeviceContext *ctx,
                         const uint8_t *nv12_data, uint32_t width, uint32_t height) {
    /* Update Y plane */
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = ctx->lpVtbl->Map(ctx, y_tex->texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return false;

    const uint8_t *y_src = nv12_data;
    for (uint32_t row = 0; row < height; row++) {
        memcpy((uint8_t *)mapped.pData + row * mapped.RowPitch,
               y_src + row * width, width);
    }
    ctx->lpVtbl->Unmap(ctx, y_tex->texture, 0);

    /* Update UV plane */
    hr = ctx->lpVtbl->Map(ctx, uv_tex->texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return false;

    const uint8_t *uv_src = nv12_data + width * height;
    uint32_t uv_width = width; /* NV12 UV row is same width as Y (interleaved U+V) */
    uint32_t uv_height = height / 2;
    for (uint32_t row = 0; row < uv_height; row++) {
        memcpy((uint8_t *)mapped.pData + row * mapped.RowPitch,
               uv_src + row * uv_width, uv_width);
    }
    ctx->lpVtbl->Unmap(ctx, uv_tex->texture, 0);

    return true;
}
```

- [ ] **Step 6: Update `video_renderer.h`**

Replace `video_renderer_t` struct:

```c
typedef struct {
    d3d_context_t *d3d_ctx;
    shader_t shader;
    texture_t y_tex;
    texture_t uv_tex;
    ID3D11Buffer *vb;
    ID3D11Buffer *ib;
    uint32_t video_width;
    uint32_t video_height;
    bool nv12_mode;
} video_renderer_t;
```

Add after `video_renderer_render` declaration:

```c
bool renderer_submit_frame(video_renderer_t *renderer, const video_frame_t *frame);
```

- [ ] **Step 7: Update `video_renderer.c`**

Replace `video_renderer_init` to load shaders from bytecode:

```c
#include "shader_bytecode.h"

bool video_renderer_init(video_renderer_t *renderer, d3d_context_t *ctx) {
    renderer->d3d_ctx = ctx;
    renderer->video_width = 0;
    renderer->video_height = 0;
    renderer->nv12_mode = true;

    /* Create vertex buffer */
    D3D11_BUFFER_DESC vb_desc = {0};
    vb_desc.ByteWidth = sizeof(vertices);
    vb_desc.Usage = D3D11_USAGE_IMMUTABLE;
    vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vb_data = {0};
    vb_data.pSysMem = vertices;

    HRESULT hr = ctx->device->lpVtbl->CreateBuffer(ctx->device, &vb_desc, &vb_data, &renderer->vb);
    if (FAILED(hr)) {
        log_error("Failed to create vertex buffer: 0x%08x", hr);
        return false;
    }

    /* Create index buffer */
    D3D11_BUFFER_DESC ib_desc = {0};
    ib_desc.ByteWidth = sizeof(indices);
    ib_desc.Usage = D3D11_USAGE_IMMUTABLE;
    ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA ib_data = {0};
    ib_data.pSysMem = indices;

    hr = ctx->device->lpVtbl->CreateBuffer(ctx->device, &ib_desc, &ib_data, &renderer->ib);
    if (FAILED(hr)) {
        log_error("Failed to create index buffer: 0x%08x", hr);
        return false;
    }

    /* Load shaders from embedded bytecode */
    if (vs_bytecode_size > 4 && ps_bgra_bytecode_size > 4) {
        if (!shader_init_from_bytecode(&renderer->shader, ctx->device,
                                        vs_bytecode, vs_bytecode_size,
                                        ps_bgra_bytecode, ps_bgra_bytecode_size)) {
            log_error("Failed to load shaders from bytecode");
            return false;
        }
    } else {
        log_warn("Shader bytecode not available, rendering disabled");
    }

    return true;
}
```

Replace `video_renderer_render` with NV12-aware version:

```c
bool video_renderer_render(video_renderer_t *renderer, const video_frame_t *frame) {
    if (!frame || !frame->data) return false;

    /* Initialize textures on first frame */
    if (renderer->video_width != frame->width || renderer->video_height != frame->height) {
        texture_destroy(&renderer->y_tex);
        texture_destroy(&renderer->uv_tex);

        if (frame->format == 0) { /* NV12 */
            if (!texture_init_nv12(&renderer->y_tex, &renderer->uv_tex,
                                    renderer->d3d_ctx->device,
                                    frame->width, frame->height)) {
                return false;
            }
            renderer->nv12_mode = true;
        } else { /* BGRA */
            if (!texture_init(&renderer->y_tex, renderer->d3d_ctx->device,
                               frame->width, frame->height)) {
                return false;
            }
            renderer->nv12_mode = false;
        }
        renderer->video_width = frame->width;
        renderer->video_height = frame->height;
    }

    /* Update texture */
    if (renderer->nv12_mode) {
        if (!texture_update_nv12(&renderer->y_tex, &renderer->uv_tex,
                                  renderer->d3d_ctx->device_ctx,
                                  frame->data, frame->width, frame->height)) {
            return false;
        }
    } else {
        if (!texture_update(&renderer->y_tex, renderer->d3d_ctx->device_ctx,
                             frame->data, frame->width * 4)) {
            return false;
        }
    }

    /* Bind shader and textures */
    shader_bind(&renderer->shader, renderer->d3d_ctx->device_ctx);
    texture_bind(&renderer->y_tex, renderer->d3d_ctx->device_ctx, 0);
    if (renderer->nv12_mode) {
        texture_bind(&renderer->uv_tex, renderer->d3d_ctx->device_ctx, 1);
    }

    /* Set vertex and index buffers */
    UINT stride = sizeof(vertex_t);
    UINT offset = 0;
    renderer->d3d_ctx->device_ctx->lpVtbl->IASetVertexBuffers(
        renderer->d3d_ctx->device_ctx, 0, 1, &renderer->vb, &stride, &offset);
    renderer->d3d_ctx->device_ctx->lpVtbl->IASetIndexBuffer(
        renderer->d3d_ctx->device_ctx, renderer->ib, DXGI_FORMAT_R16_UINT, 0);
    renderer->d3d_ctx->device_ctx->lpVtbl->IASetPrimitiveTopology(
        renderer->d3d_ctx->device_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    /* Draw */
    renderer->d3d_ctx->device_ctx->lpVtbl->DrawIndexed(
        renderer->d3d_ctx->device_ctx, 6, 0, 0);

    return true;
}

bool renderer_submit_frame(video_renderer_t *renderer, const video_frame_t *frame) {
    /* For now, render directly. In threaded model, this would queue the frame. */
    d3d_context_begin_frame(renderer->d3d_ctx);
    bool ret = video_renderer_render(renderer, frame);
    d3d_context_end_frame(renderer->d3d_ctx);
    video_frame_free((video_frame_t *)frame);
    return ret;
}
```

Update `video_renderer_destroy`:

```c
void video_renderer_destroy(video_renderer_t *renderer) {
    shader_destroy(&renderer->shader);
    texture_destroy(&renderer->y_tex);
    texture_destroy(&renderer->uv_tex);
    if (renderer->vb) renderer->vb->lpVtbl->Release(renderer->vb);
    if (renderer->ib) renderer->ib->lpVtbl->Release(renderer->ib);
}
```

- [ ] **Step 8: Verify build**

Run: `ninja -C builddir`
Expected: Build succeeds

- [ ] **Step 9: Commit**

```bash
git add src/render/shader_bytecode.h src/render/shader.h src/render/shader.c
git add src/render/texture.h src/render/texture.c
git add src/render/video_renderer.h src/render/video_renderer.c
git commit -m "feat: implement shader bytecode loading, NV12 texture, and renderer"
```

---

## Task 12: Control Module — Message Serialization

**Files:**
- Modify: `src/control/control_msg.h`
- Modify: `src/control/control_msg.c`

- [ ] **Step 1: Update `control_msg.h` with serialization declarations**

Add after the `CONTROL_MSG_MAX_SIZE` define:

```c
/* Serialize a control message into buf. Returns bytes written, or 0 on error. */
uint32_t control_msg_serialize(enum control_msg_type type,
                                const void *msg_data, uint8_t *buf, uint32_t buf_size);
```

Also add a position struct and message data union for structured access:

```c
typedef struct {
    int32_t x;
    int32_t y;
    uint16_t width;
    uint16_t height;
} control_position_t;
```

- [ ] **Step 2: Implement serialization in `control_msg.c`**

Replace `control_msg.c` contents:

```c
#include "control_msg.h"
#include "../adb/binary.h"
#include "../platform/log.h"
#include <string.h>

uint32_t control_msg_serialize(enum control_msg_type type,
                                const void *msg_data, uint8_t *buf, uint32_t buf_size) {
    buf[0] = (uint8_t)type;

    switch (type) {
        case CONTROL_MSG_TYPE_INJECT_KEYCODE: {
            /* action(1) + keycode(4) + repeat(4) + metastate(4) = 13 */
            const uint32_t *args = (const uint32_t *)msg_data;
            if (buf_size < 14) return 0;
            buf[1] = (uint8_t)args[0]; /* action */
            write32be(&buf[2], args[1]); /* keycode */
            write32be(&buf[6], args[2]); /* repeat */
            write32be(&buf[10], args[3]); /* metastate */
            return 14;
        }
        case CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT: {
            /* action(1) + pointer_id(8) + x(4) + y(4) + w(2) + h(2)
               + pressure(2) + action_button(4) + buttons(4) = 31 */
            /* msg_data: [action, pointer_id_hi, pointer_id_lo, x, y, w, h,
                         pressure, action_button, buttons] */
            const uint32_t *args = (const uint32_t *)msg_data;
            if (buf_size < 32) return 0;
            buf[1] = (uint8_t)args[0]; /* action */
            write64be(&buf[2], ((uint64_t)args[1] << 32) | args[2]); /* pointer_id */
            write32be(&buf[10], args[3]); /* x */
            write32be(&buf[14], args[4]); /* y */
            write16be(&buf[18], (uint16_t)args[5]); /* width */
            write16be(&buf[20], (uint16_t)args[6]); /* height */
            write16be(&buf[22], (uint16_t)args[7]); /* pressure */
            write32be(&buf[24], args[8]); /* action_button */
            write32be(&buf[28], args[9]); /* buttons */
            return 32;
        }
        case CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT: {
            /* x(4) + y(4) + w(2) + h(2) + hscroll(2) + vscroll(2) + buttons(4) = 20 */
            const int32_t *args = (const int32_t *)msg_data;
            if (buf_size < 21) return 0;
            write32be(&buf[1], (uint32_t)args[0]); /* x */
            write32be(&buf[5], (uint32_t)args[1]); /* y */
            write16be(&buf[9], (uint16_t)args[2]); /* width */
            write16be(&buf[11], (uint16_t)args[3]); /* height */
            write16be(&buf[13], (uint16_t)args[4]); /* hscroll */
            write16be(&buf[15], (uint16_t)args[5]); /* vscroll */
            write32be(&buf[17], (uint32_t)args[6]); /* buttons */
            return 21;
        }
        case CONTROL_MSG_TYPE_SET_CLIPBOARD: {
            /* sequence(8) + paste(1) + len(4) + text */
            /* msg_data: struct { uint64_t sequence; bool paste; uint32_t len; char *text; } */
            const uint8_t *p = (const uint8_t *)msg_data;
            uint64_t sequence;
            memcpy(&sequence, p, 8);
            bool paste = p[8];
            uint32_t len;
            memcpy(&len, p + 9, 4);
            const char *text = *(const char **)(p + 13);

            if (buf_size < 10 + len) return 0;
            write64be(&buf[1], sequence);
            buf[9] = paste ? 1 : 0;
            write32be(&buf[10], len);
            memcpy(&buf[14], text, len);
            return 14 + len;
        }
        case CONTROL_MSG_TYPE_SET_DISPLAY_POWER: {
            if (buf_size < 2) return 0;
            buf[1] = *(const bool *)msg_data ? 1 : 0;
            return 2;
        }
        case CONTROL_MSG_TYPE_BACK_OR_SCREEN_ON:
        case CONTROL_MSG_TYPE_EXPAND_NOTIFICATION_PANEL:
        case CONTROL_MSG_TYPE_EXPAND_SETTINGS_PANEL:
        case CONTROL_MSG_TYPE_COLLAPSE_PANELS:
        case CONTROL_MSG_TYPE_ROTATE_DEVICE:
        case CONTROL_MSG_TYPE_OPEN_HARD_KEYBOARD_SETTINGS:
        case CONTROL_MSG_TYPE_RESET_VIDEO:
            return 1;

        default:
            log_warn("Unknown control message type: %u", type);
            return 0;
    }
}
```

- [ ] **Step 3: Create `tests/test_control_msg.c`**

```c
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../src/control/control_msg.h"

void test_serialize_keycode(void) {
    uint8_t buf[64];
    uint32_t args[4] = {0, 66, 0, 0}; /* action=DOWN, keycode=66(ENTER), repeat=0, meta=0 */
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_INJECT_KEYCODE, args, buf, sizeof(buf));
    assert(len == 14);
    assert(buf[0] == CONTROL_MSG_TYPE_INJECT_KEYCODE);
    assert(buf[1] == 0); /* action DOWN */
    printf("test_serialize_keycode passed\n");
}

void test_serialize_display_power(void) {
    uint8_t buf[64];
    bool on = true;
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_SET_DISPLAY_POWER, &on, buf, sizeof(buf));
    assert(len == 2);
    assert(buf[0] == CONTROL_MSG_TYPE_SET_DISPLAY_POWER);
    assert(buf[1] == 1);
    printf("test_serialize_display_power passed\n");
}

void test_serialize_collapse(void) {
    uint8_t buf[64];
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_COLLAPSE_PANELS, NULL, buf, sizeof(buf));
    assert(len == 1);
    assert(buf[0] == CONTROL_MSG_TYPE_COLLAPSE_PANELS);
    printf("test_serialize_collapse passed\n");
}

int main(void) {
    test_serialize_keycode();
    test_serialize_display_power();
    test_serialize_collapse();
    return 0;
}
```

- [ ] **Step 4: Verify build and tests**

Run: `ninja -C builddir && ./builddir/tests/test_control_msg`
Expected: All tests pass

- [ ] **Step 5: Commit**

```bash
git add src/control/control_msg.h src/control/control_msg.c tests/test_control_msg.c
git commit -m "feat: implement control message serialization"
```

---

## Task 13: Input Module — Keycode Mapping

**Files:**
- Create: `src/input/keycode_map.h`
- Create: `src/input/keycode_map.c`

- [ ] **Step 1: Create `keycode_map.h`**

```c
#ifndef KEYCODE_MAP_H
#define KEYCODE_MAP_H

#include <stdint.h>

/* Map Windows VK code to Android keycode.
 * Returns 0 if no mapping exists. */
uint32_t vk_to_android_keycode(uint32_t vk);

/* Get Android metastate flags from current keyboard state.
 * Call with GetKeyState() results. */
uint32_t get_android_metastate(void);

#endif /* KEYCODE_MAP_H */
```

- [ ] **Step 2: Create `keycode_map.c`**

```c
#include "keycode_map.h"
#include <windows.h>

/* Mapping table: Windows VK → Android KeyEvent.KEYCODE_* */
static const struct {
    uint32_t vk;
    uint32_t android_keycode;
} vk_map[] = {
    {VK_BACK, 67},        /* KEYCODE_DEL */
    {VK_TAB, 61},         /* KEYCODE_TAB */
    {VK_RETURN, 66},      /* KEYCODE_ENTER */
    {VK_LSHIFT, 59},      /* KEYCODE_SHIFT_LEFT */
    {VK_RSHIFT, 60},      /* KEYCODE_SHIFT_RIGHT */
    {VK_LCONTROL, 113},   /* KEYCODE_CTRL_LEFT */
    {VK_RCONTROL, 114},   /* KEYCODE_CTRL_RIGHT */
    {VK_LMENU, 57},       /* KEYCODE_ALT_LEFT */
    {VK_RMENU, 58},       /* KEYCODE_ALT_RIGHT */
    {VK_PAUSE, 127},      /* KEYCODE_BREAK */
    {VK_CAPITAL, 115},    /* KEYCODE_CAPS_LOCK */
    {VK_ESCAPE, 111},     /* KEYCODE_ESCAPE */
    {VK_SPACE, 62},       /* KEYCODE_SPACE */
    {VK_PRIOR, 92},       /* KEYCODE_PAGE_UP */
    {VK_NEXT, 93},        /* KEYCODE_PAGE_DOWN */
    {VK_END, 123},        /* KEYCODE_MOVE_END */
    {VK_HOME, 122},       /* KEYCODE_MOVE_HOME */
    {VK_LEFT, 21},        /* KEYCODE_DPAD_LEFT */
    {VK_UP, 19},          /* KEYCODE_DPAD_UP */
    {VK_RIGHT, 22},       /* KEYCODE_DPAD_RIGHT */
    {VK_DOWN, 20},        /* KEYCODE_DPAD_DOWN */
    {VK_INSERT, 124},     /* KEYCODE_INSERT */
    {VK_DELETE, 67},      /* KEYCODE_DEL */
    {'0', 7},             /* KEYCODE_0 */
    {'1', 8},             /* KEYCODE_1 */
    {'2', 9},             /* KEYCODE_2 */
    {'3', 10},            /* KEYCODE_3 */
    {'4', 11},            /* KEYCODE_4 */
    {'5', 12},            /* KEYCODE_5 */
    {'6', 13},            /* KEYCODE_6 */
    {'7', 14},            /* KEYCODE_7 */
    {'8', 15},            /* KEYCODE_8 */
    {'9', 16},            /* KEYCODE_9 */
    {'A', 29},            /* KEYCODE_A */
    {'B', 30},            /* KEYCODE_B */
    {'C', 31},            /* KEYCODE_C */
    {'D', 32},            /* KEYCODE_D */
    {'E', 33},            /* KEYCODE_E */
    {'F', 34},            /* KEYCODE_F */
    {'G', 35},            /* KEYCODE_G */
    {'H', 36},            /* KEYCODE_H */
    {'I', 37},            /* KEYCODE_I */
    {'J', 38},            /* KEYCODE_J */
    {'K', 39},            /* KEYCODE_K */
    {'L', 40},            /* KEYCODE_L */
    {'M', 41},            /* KEYCODE_M */
    {'N', 42},            /* KEYCODE_N */
    {'O', 43},            /* KEYCODE_O */
    {'P', 44},            /* KEYCODE_P */
    {'Q', 45},            /* KEYCODE_Q */
    {'R', 46},            /* KEYCODE_R */
    {'S', 47},            /* KEYCODE_S */
    {'T', 48},            /* KEYCODE_T */
    {'U', 49},            /* KEYCODE_U */
    {'V', 50},            /* KEYCODE_V */
    {'W', 51},            /* KEYCODE_W */
    {'X', 52},            /* KEYCODE_X */
    {'Y', 53},            /* KEYCODE_Y */
    {'Z', 54},            /* KEYCODE_Z */
    {VK_LWIN, 117},       /* KEYCODE_META_LEFT */
    {VK_RWIN, 118},       /* KEYCODE_META_RIGHT */
    {VK_NUMPAD0, 144},    /* KEYCODE_NUMPAD_0 */
    {VK_NUMPAD1, 145},    /* KEYCODE_NUMPAD_1 */
    {VK_NUMPAD2, 146},    /* KEYCODE_NUMPAD_2 */
    {VK_NUMPAD3, 147},    /* KEYCODE_NUMPAD_3 */
    {VK_NUMPAD4, 148},    /* KEYCODE_NUMPAD_4 */
    {VK_NUMPAD5, 149},    /* KEYCODE_NUMPAD_5 */
    {VK_NUMPAD6, 150},    /* KEYCODE_NUMPAD_6 */
    {VK_NUMPAD7, 151},    /* KEYCODE_NUMPAD_7 */
    {VK_NUMPAD8, 152},    /* KEYCODE_NUMPAD_8 */
    {VK_NUMPAD9, 153},    /* KEYCODE_NUMPAD_9 */
    {VK_MULTIPLY, 155},   /* KEYCODE_NUMPAD_MULTIPLY */
    {VK_ADD, 157},        /* KEYCODE_NUMPAD_ADD */
    {VK_SUBTRACT, 156},   /* KEYCODE_NUMPAD_SUBTRACT */
    {VK_DECIMAL, 158},    /* KEYCODE_NUMPAD_DOT */
    {VK_DIVIDE, 154},     /* KEYCODE_NUMPAD_DIVIDE */
    {VK_F1, 131},         /* KEYCODE_F1 */
    {VK_F2, 132},         /* KEYCODE_F2 */
    {VK_F3, 133},         /* KEYCODE_F3 */
    {VK_F4, 134},         /* KEYCODE_F4 */
    {VK_F5, 135},         /* KEYCODE_F5 */
    {VK_F6, 136},         /* KEYCODE_F6 */
    {VK_F7, 137},         /* KEYCODE_F7 */
    {VK_F8, 138},         /* KEYCODE_F8 */
    {VK_F9, 139},         /* KEYCODE_F9 */
    {VK_F10, 140},        /* KEYCODE_F10 */
    {VK_F11, 141},        /* KEYCODE_F11 */
    {VK_F12, 142},        /* KEYCODE_F12 */
    {VK_NUMLOCK, 143},    /* KEYCODE_NUM_LOCK */
    {VK_SCROLL, 116},     /* KEYCODE_SCROLL_LOCK */
    {VK_OEM_1, 74},       /* KEYCODE_SEMICOLON */
    {VK_OEM_PLUS, 70},    /* KEYCODE_EQUALS */
    {VK_OEM_COMMA, 55},   /* KEYCODE_COMMA */
    {VK_OEM_MINUS, 69},   /* KEYCODE_MINUS */
    {VK_OEM_PERIOD, 56},  /* KEYCODE_PERIOD */
    {VK_OEM_2, 76},       /* KEYCODE_SLASH */
    {VK_OEM_3, 68},       /* KEYCODE_GRAVE */
    {VK_OEM_4, 71},       /* KEYCODE_LEFT_BRACKET */
    {VK_OEM_5, 73},       /* KEYCODE_BACKSLASH */
    {VK_OEM_6, 72},       /* KEYCODE_RIGHT_BRACKET */
    {VK_OEM_7, 75},       /* KEYCODE_APOSTROPHE */
};

#define VK_MAP_SIZE (sizeof(vk_map) / sizeof(vk_map[0]))

uint32_t vk_to_android_keycode(uint32_t vk) {
    for (size_t i = 0; i < VK_MAP_SIZE; i++) {
        if (vk_map[i].vk == vk) {
            return vk_map[i].android_keycode;
        }
    }
    return 0;
}

uint32_t get_android_metastate(void) {
    uint32_t meta = 0; /* AMETA_NONE */
    if (GetKeyState(VK_SHIFT) & 0x8000) meta |= 0x01;   /* AMETA_SHIFT_ON */
    if (GetKeyState(VK_CONTROL) & 0x8000) meta |= 0x1000; /* AMETA_CTRL_ON */
    if (GetKeyState(VK_MENU) & 0x8000) meta |= 0x02;     /* AMETA_ALT_ON */
    if (GetKeyState(VK_LWIN) & 0x8000) meta |= 0x10000;  /* AMETA_META_ON */
    return meta;
}
```

- [ ] **Step 3: Verify build**

Run: `ninja -C builddir`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add src/input/keycode_map.h src/input/keycode_map.c
git commit -m "feat: add Windows VK to Android keycode mapping"
```

---

## Task 14: Input Module — Coordinate Transformation

**Files:**
- Create: `src/input/input_transform.h`
- Create: `src/input/input_transform.c`

- [ ] **Step 1: Create `input_transform.h`**

```c
#ifndef INPUT_TRANSFORM_H
#define INPUT_TRANSFORM_H

#include <stdint.h>

/* Transform window client coordinates to device screen coordinates.
 * Accounts for letterboxing (black bars) from aspect ratio preservation. */
void input_transform_coords(int32_t win_x, int32_t win_y,
                            int32_t *dev_x, int32_t *dev_y,
                            int32_t win_w, int32_t win_h,
                            uint32_t dev_w, uint32_t dev_h);

#endif /* INPUT_TRANSFORM_H */
```

- [ ] **Step 2: Create `input_transform.c`**

```c
#include "input_transform.h"

void input_transform_coords(int32_t win_x, int32_t win_y,
                            int32_t *dev_x, int32_t *dev_y,
                            int32_t win_w, int32_t win_h,
                            uint32_t dev_w, uint32_t dev_h) {
    if (win_w <= 0 || win_h <= 0 || dev_w == 0 || dev_h == 0) {
        *dev_x = 0;
        *dev_y = 0;
        return;
    }

    float video_aspect = (float)dev_w / dev_h;
    float window_aspect = (float)win_w / win_h;

    int32_t render_x = 0, render_y = 0;
    int32_t render_w, render_h;

    if (window_aspect > video_aspect) {
        /* Window is wider than video — pillarbox (bars on sides) */
        render_h = win_h;
        render_w = (int32_t)(win_h * video_aspect);
        render_x = (win_w - render_w) / 2;
        render_y = 0;
    } else {
        /* Window is taller than video — letterbox (bars on top/bottom) */
        render_w = win_w;
        render_h = (int32_t)(win_w / video_aspect);
        render_x = 0;
        render_y = (win_h - render_h) / 2;
    }

    /* Clamp to render area */
    int32_t rel_x = win_x - render_x;
    int32_t rel_y = win_y - render_y;

    if (rel_x < 0) rel_x = 0;
    if (rel_y < 0) rel_y = 0;
    if (rel_x > render_w) rel_x = render_w;
    if (rel_y > render_h) rel_y = render_h;

    /* Scale to device coordinates */
    *dev_x = (int32_t)((float)rel_x / render_w * dev_w);
    *dev_y = (int32_t)((float)rel_y / render_h * dev_h);
}
```

- [ ] **Step 3: Verify build**

Run: `ninja -C builddir`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add src/input/input_transform.h src/input/input_transform.c
git commit -m "feat: add coordinate transformation with letterboxing support"
```

---

## Task 15: Power Control

**Files:**
- Modify: `src/control/power.h`
- Modify: `src/control/power.c`

- [ ] **Step 1: Update `power.h`**

Replace contents:

```c
#ifndef POWER_H
#define POWER_H

#include <stdbool.h>

/* Initialize power control subsystem */
bool power_init(void);

/* Set screen power on/off via ADB shell command.
 * adb_conn must be a connected adb_connection_t*. */
bool power_set_screen_power(void *adb_conn, bool on);

/* Cleanup */
void power_destroy(void);

#endif /* POWER_H */
```

- [ ] **Step 2: Implement `power.c`**

Replace contents:

```c
#include "power.h"
#include "../adb/adb.h"
#include "../platform/log.h"

bool power_init(void) {
    return true;
}

bool power_set_screen_power(void *adb_conn, bool on) {
    if (!adb_conn) {
        log_error("No ADB connection for power control");
        return false;
    }

    const char *cmd = on ? "input keyevent KEYCODE_WAKEUP"
                         : "input keyevent KEYCODE_SLEEP";

    if (!adb_shell((adb_connection_t *)adb_conn, cmd)) {
        log_error("Failed to send power command");
        return false;
    }

    log_info("Screen power: %s", on ? "on" : "off");
    return true;
}

void power_destroy(void) {
    /* Nothing to clean up */
}
```

- [ ] **Step 3: Verify build**

Run: `ninja -C builddir`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add src/control/power.h src/control/power.c
git commit -m "feat: implement power control via ADB shell"
```

---

## Task 16: Record Module — AV1/FLAC Codec Support

**Files:**
- Modify: `src/record/muxer.c`

- [ ] **Step 1: Add AV1 codec support in `muxer_add_video_stream`**

In `muxer.c`, add a case in the `switch (codec_id)` block in `muxer_add_video_stream` (after the h265 case):

```c
        case 0x00415631: // av01
            stream->codecpar->codec_id = AV_CODEC_ID_AV1;
            break;
```

- [ ] **Step 2: Add FLAC codec support in `muxer_add_audio_stream`**

Add a case in the `switch (codec_id)` block in `muxer_add_audio_stream` (after the aac case):

```c
        case 0x666c6163: // flac
            stream->codecpar->codec_id = AV_CODEC_ID_FLAC;
            break;
```

- [ ] **Step 3: Verify build**

Run: `ninja -C builddir`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add src/record/muxer.c
git commit -m "feat: add AV1 and FLAC codec support to muxer"
```

---

## Task 17: App Module — Window Input Handling and Resize

**Files:**
- Modify: `src/app/window.h`
- Modify: `src/app/window.c`

- [ ] **Step 1: Update `window.h` with input callback types**

Add after `window_t` struct:

```c
/* Callback types for input events */
typedef void (*window_key_cb_t)(uint32_t vk, bool down, void *userdata);
typedef void (*window_mouse_cb_t)(int32_t x, int32_t y, uint32_t buttons,
                                   uint32_t action, void *userdata);
typedef void (*window_wheel_cb_t)(int32_t x, int32_t y, int32_t delta, void *userdata);
typedef void (*window_resize_cb_t)(int32_t width, int32_t height, void *userdata);

typedef struct {
    window_key_cb_t key_cb;
    window_mouse_cb_t mouse_cb;
    window_wheel_cb_t wheel_cb;
    window_resize_cb_t resize_cb;
    void *userdata;
} window_callbacks_t;
```

Add to `window_t` struct:

```c
    window_callbacks_t callbacks;
```

Add function declaration:

```c
void window_set_callbacks(window_t *win, const window_callbacks_t *callbacks);
```

- [ ] **Step 2: Update `window.c` WndProc**

Replace `window.c` WndProc and add `window_set_callbacks`:

```c
#include "window.h"
#include "../platform/log.h"

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    window_t *win = (window_t *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (win && win->callbacks.key_cb) {
                win->callbacks.key_cb((uint32_t)wParam, true, win->callbacks.userdata);
            }
            return 0;

        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (win && win->callbacks.key_cb) {
                win->callbacks.key_cb((uint32_t)wParam, false, win->callbacks.userdata);
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (win && win->callbacks.mouse_cb) {
                int32_t x = (int16_t)LOWORD(lParam);
                int32_t y = (int16_t)HIWORD(lParam);
                win->callbacks.mouse_cb(x, y, 1, 1, win->callbacks.userdata);
            }
            return 0;
        case WM_LBUTTONUP:
            if (win && win->callbacks.mouse_cb) {
                int32_t x = (int16_t)LOWORD(lParam);
                int32_t y = (int16_t)HIWORD(lParam);
                win->callbacks.mouse_cb(x, y, 1, 0, win->callbacks.userdata);
            }
            return 0;
        case WM_RBUTTONDOWN:
            if (win && win->callbacks.mouse_cb) {
                int32_t x = (int16_t)LOWORD(lParam);
                int32_t y = (int16_t)HIWORD(lParam);
                win->callbacks.mouse_cb(x, y, 2, 1, win->callbacks.userdata);
            }
            return 0;
        case WM_RBUTTONUP:
            if (win && win->callbacks.mouse_cb) {
                int32_t x = (int16_t)LOWORD(lParam);
                int32_t y = (int16_t)HIWORD(lParam);
                win->callbacks.mouse_cb(x, y, 2, 0, win->callbacks.userdata);
            }
            return 0;
        case WM_MOUSEMOVE:
            if (win && win->callbacks.mouse_cb) {
                int32_t x = (int16_t)LOWORD(lParam);
                int32_t y = (int16_t)HIWORD(lParam);
                uint32_t buttons = 0;
                if (wParam & MK_LBUTTON) buttons |= 1;
                if (wParam & MK_RBUTTON) buttons |= 2;
                win->callbacks.mouse_cb(x, y, buttons, 2, win->callbacks.userdata);
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (win && win->callbacks.wheel_cb) {
                int32_t x = (int16_t)LOWORD(lParam);
                int32_t y = (int16_t)HIWORD(lParam);
                int32_t delta = GET_WHEEL_DELTA_WPARAM(wParam);
                win->callbacks.wheel_cb(x, y, delta, win->callbacks.userdata);
            }
            return 0;

        case WM_SIZE:
            if (win && win->callbacks.resize_cb) {
                int32_t w = (int32_t)LOWORD(lParam);
                int32_t h = (int32_t)HIWORD(lParam);
                win->width = w;
                win->height = h;
                win->callbacks.resize_cb(w, h, win->callbacks.userdata);
            }
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool window_init(window_t *win, HINSTANCE hInstance, const char *title,
                 int width, int height) {
    win->width = width;
    win->height = height;
    win->fullscreen = false;
    win->always_on_top = false;
    memset(&win->callbacks, 0, sizeof(win->callbacks));

    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"AutoScrcpyWindow";

    if (!RegisterClassEx(&wc)) {
        log_error("Failed to register window class");
        return false;
    }

    RECT rect = {0, 0, width, height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    win->hwnd = CreateWindowEx(
        0,
        L"AutoScrcpyWindow",
        L"AutoScrcpy",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, hInstance, NULL);

    if (!win->hwnd) {
        log_error("Failed to create window");
        return false;
    }

    /* Store window pointer for WndProc access */
    SetWindowLongPtr(win->hwnd, GWLP_USERDATA, (LONG_PTR)win);

    return true;
}

void window_set_callbacks(window_t *win, const window_callbacks_t *callbacks) {
    win->callbacks = *callbacks;
}

void window_show(window_t *win) {
    ShowWindow(win->hwnd, SW_SHOW);
    UpdateWindow(win->hwnd);
}

void window_set_fullscreen(window_t *win, bool fullscreen) {
    if (fullscreen == win->fullscreen) return;
    win->fullscreen = fullscreen;

    if (fullscreen) {
        SetWindowLong(win->hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(win->hwnd, HWND_TOP, 0, 0,
                     GetSystemMetrics(SM_CXSCREEN),
                     GetSystemMetrics(SM_CYSCREEN),
                     SWP_FRAMECHANGED);
    } else {
        SetWindowLong(win->hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(win->hwnd, NULL, 0, 0, win->width, win->height,
                     SWP_FRAMECHANGED);
    }
}

void window_set_always_on_top(window_t *win, bool always_on_top) {
    win->always_on_top = always_on_top;
    SetWindowPos(win->hwnd, always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

void window_destroy(window_t *win) {
    if (win->hwnd) {
        DestroyWindow(win->hwnd);
    }
}
```

- [ ] **Step 3: Verify build**

Run: `ninja -C builddir`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add src/app/window.h src/app/window.c
git commit -m "feat: implement WndProc input handling and resize callbacks"
```

---

## Task 18: App Module — Event Loop and Threaded Receivers

**Files:**
- Modify: `src/app/application.h`
- Modify: `src/app/application.c`

- [ ] **Step 1: Update `application.h`**

Replace `application_t` struct:

```c
typedef struct {
    struct scrcpy_options options;
    window_t window;
    d3d_context_t d3d_ctx;
    video_renderer_t renderer;
    video_decoder_t *video_decoder;
    audio_decoder_t *audio_decoder;
    video_socket_t video_sock;
    audio_socket_t audio_sock;
    control_socket_t control_sock;
    server_t server;
    HANDLE video_thread;
    HANDLE audio_thread;
    HANDLE stop_event;
    bool running;
    /* Device screen dimensions (set after first video frame) */
    uint32_t device_width;
    uint32_t device_height;
} application_t;
```

- [ ] **Step 2: Implement event loop and threaded receivers in `application.c`**

Replace `application.c` contents:

```c
#include "application.h"
#include "../platform/log.h"
#include "../adb/adb.h"
#include "../device/server.h"
#include "../input/keycode_map.h"
#include "../input/input_transform.h"
#include "../control/control_msg.h"
#include <string.h>

/* Forward declarations for callbacks */
static void on_key_event(uint32_t vk, bool down, void *userdata);
static void on_mouse_event(int32_t x, int32_t y, uint32_t buttons,
                            uint32_t action, void *userdata);
static void on_wheel_event(int32_t x, int32_t y, int32_t delta, void *userdata);
static void on_resize(int32_t width, int32_t height, void *userdata);

static DWORD WINAPI video_receiver_thread(LPVOID arg) {
    application_t *app = (application_t *)arg;

    while (app->running) {
        uint8_t *data = NULL;
        uint32_t size = 0;

        if (!video_socket_read_packet(&app->video_sock, &data, &size)) {
            if (app->running) log_error("Video socket read failed");
            break;
        }

        /* Read codec info from first packet if not set */
        if (app->video_sock.codec_id == 0 && size >= 12) {
            /* First packet contains codec + dimensions */
            /* Format: codec(4) + width(4) + height(4) */
            app->video_sock.codec_id = *(uint32_t *)data;
            app->video_sock.width = *(uint32_t *)(data + 4);
            app->video_sock.height = *(uint32_t *)(data + 8);
            app->device_width = app->video_sock.width;
            app->device_height = app->video_sock.height;

            video_decoder_init(app->video_decoder, app->video_sock.codec_id,
                               app->video_sock.width, app->video_sock.height);

            free(data);
            continue;
        }

        video_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        if (video_decoder_decode(app->video_decoder, data, size, &frame)) {
            renderer_submit_frame(&app->renderer, &frame);
        }

        free(data);
    }

    return 0;
}

static DWORD WINAPI audio_receiver_thread(LPVOID arg) {
    application_t *app = (application_t *)arg;

    while (app->running) {
        uint8_t *data = NULL;
        uint32_t size = 0;

        if (!audio_socket_read_packet(&app->audio_sock, &data, &size)) {
            if (app->running) log_error("Audio socket read failed");
            break;
        }

        /* Decode audio */
        audio_frame_t aframe;
        memset(&aframe, 0, sizeof(aframe));
        if (audio_decoder_decode(app->audio_decoder, data, size, &aframe)) {
            /* Audio playback handled by audio_player in a separate path */
            if (aframe.data) free(aframe.data);
        }

        free(data);
    }

    return 0;
}

bool application_init(application_t *app, const struct scrcpy_options *options) {
    app->options = *options;
    app->running = false;
    app->video_decoder = NULL;
    app->audio_decoder = NULL;
    app->video_thread = NULL;
    app->audio_thread = NULL;
    app->stop_event = NULL;
    app->device_width = 0;
    app->device_height = 0;

    memset(&app->video_sock, 0, sizeof(app->video_sock));
    memset(&app->audio_sock, 0, sizeof(app->audio_sock));
    memset(&app->control_sock, 0, sizeof(app->control_sock));

    /* Initialize ADB */
    if (!adb_init()) {
        log_error("Failed to initialize ADB");
        return false;
    }

    /* Initialize window */
    if (!window_init(&app->window, GetModuleHandle(NULL), options->window_title,
                     800, 600)) {
        log_error("Failed to initialize window");
        return false;
    }

    /* Set input callbacks */
    window_callbacks_t cbs = {
        .key_cb = on_key_event,
        .mouse_cb = on_mouse_event,
        .wheel_cb = on_wheel_event,
        .resize_cb = on_resize,
        .userdata = app,
    };
    window_set_callbacks(&app->window, &cbs);

    /* Initialize D3D context */
    if (!d3d_context_init(&app->d3d_ctx, app->window.hwnd, 800, 600)) {
        log_error("Failed to initialize D3D context");
        return false;
    }

    /* Initialize video renderer */
    if (!video_renderer_init(&app->renderer, &app->d3d_ctx)) {
        log_error("Failed to initialize video renderer");
        return false;
    }

    /* Initialize decoders */
    app->video_decoder = video_decoder_create();
    if (!app->video_decoder) {
        log_error("Failed to create video decoder");
        return false;
    }

    app->audio_decoder = audio_decoder_create();
    if (!app->audio_decoder) {
        log_error("Failed to create audio decoder");
        return false;
    }

    /* Create stop event */
    app->stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!app->stop_event) {
        log_error("Failed to create stop event");
        return false;
    }

    return true;
}

int application_run(application_t *app) {
    /* Push and start server */
    struct server_config server_cfg = {
        .serial = app->options.serial,
        .server_path = app->options.server_path,
        .local_port = app->options.port,
        .max_size = app->options.max_size,
        .video_bit_rate = app->options.video_bit_rate,
        .audio_bit_rate = app->options.audio_bit_rate,
        .video = app->options.video,
        .audio = app->options.audio,
        .control = app->options.control,
    };

    server_init(&app->server, &server_cfg);
    if (!server_start(&app->server, &app->video_sock, &app->audio_sock,
                      &app->control_sock)) {
        log_error("Failed to start server");
        return 1;
    }

    /* Show window */
    window_show(&app->window);
    if (app->options.fullscreen) {
        window_set_fullscreen(&app->window, true);
    }
    if (app->options.always_on_top) {
        window_set_always_on_top(&app->window, true);
    }

    /* Start receiver threads */
    app->running = true;

    if (app->options.video) {
        app->video_thread = CreateThread(NULL, 0, video_receiver_thread, app, 0, NULL);
    }
    if (app->options.audio) {
        app->audio_thread = CreateThread(NULL, 0, audio_receiver_thread, app, 0, NULL);
    }

    /* Event loop using MsgWaitForMultipleObjects */
    HANDLE events[1] = { app->stop_event };
    while (app->running) {
        DWORD result = MsgWaitForMultipleObjects(
            1, events, FALSE, INFINITE, QS_ALLINPUT);

        if (result == WAIT_OBJECT_0 + 1) {
            /* Windows messages available */
            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    app->running = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        } else if (result == WAIT_OBJECT_0) {
            /* Stop event signaled */
            app->running = false;
        }
    }

    /* Cleanup */
    server_kill(&app->server);

    if (app->video_thread) {
        WaitForSingleObject(app->video_thread, 2000);
        CloseHandle(app->video_thread);
    }
    if (app->audio_thread) {
        WaitForSingleObject(app->audio_thread, 2000);
        CloseHandle(app->audio_thread);
    }

    server_destroy(&app->server);

    return 0;
}

void application_destroy(application_t *app) {
    if (app->stop_event) {
        CloseHandle(app->stop_event);
    }
    if (app->video_decoder) {
        video_decoder_destroy(app->video_decoder);
    }
    if (app->audio_decoder) {
        audio_decoder_destroy(app->audio_decoder);
    }

    video_renderer_destroy(&app->renderer);
    d3d_context_destroy(&app->d3d_ctx);
    window_destroy(&app->window);

    video_socket_destroy(&app->video_sock);
    audio_socket_destroy(&app->audio_sock);
    control_socket_destroy(&app->control_sock);

    adb_destroy();
}

/* Input callbacks */
static void on_key_event(uint32_t vk, bool down, void *userdata) {
    application_t *app = (application_t *)userdata;
    if (!app->options.control) return;

    uint32_t android_keycode = vk_to_android_keycode(vk);
    if (android_keycode == 0) return;

    uint32_t metastate = get_android_metastate();
    uint32_t action = down ? 0 : 1; /* AKEY_EVENT_ACTION_DOWN / UP */

    uint8_t buf[64];
    uint32_t args[4] = {action, android_keycode, 0, metastate};
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_INJECT_KEYCODE,
                                          args, buf, sizeof(buf));
    if (len > 0) {
        control_socket_send_msg(&app->control_sock, buf, len);
    }
}

static void on_mouse_event(int32_t x, int32_t y, uint32_t buttons,
                            uint32_t action, void *userdata) {
    application_t *app = (application_t *)userdata;
    if (!app->options.control) return;
    if (app->device_width == 0 || app->device_height == 0) return;

    /* Transform coordinates */
    int32_t dev_x, dev_y;
    input_transform_coords(x, y, &dev_x, &dev_y,
                           app->window.width, app->window.height,
                           app->device_width, app->device_height);

    /* Map action: 0=up, 1=down, 2=move → Android action */
    uint32_t android_action;
    uint32_t android_button;
    if (action == 1) { /* down */
        android_action = 0; /* AMOTION_EVENT_ACTION_DOWN */
        android_button = (buttons & 1) ? 1 : ((buttons & 2) ? 2 : 1);
    } else if (action == 0) { /* up */
        android_action = 1; /* AMOTION_EVENT_ACTION_UP */
        android_button = (buttons & 1) ? 1 : ((buttons & 2) ? 2 : 1);
    } else { /* move */
        android_action = 2; /* AMOTION_EVENT_ACTION_MOVE */
        android_button = 0;
    }

    uint32_t pointer_id_hi = 0xFFFFFFFF; /* SC_POINTER_ID_MOUSE */
    uint32_t pointer_id_lo = 0xFFFFFFFF;
    uint16_t pressure = (action == 1) ? 0xFFFF : 0;

    uint8_t buf[64];
    uint32_t args[10] = {
        android_action, pointer_id_hi, pointer_id_lo,
        (uint32_t)dev_x, (uint32_t)dev_y,
        (uint32_t)app->device_width, (uint32_t)app->device_height,
        (uint32_t)pressure, android_button, android_button
    };
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT,
                                          args, buf, sizeof(buf));
    if (len > 0) {
        control_socket_send_msg(&app->control_sock, buf, len);
    }
}

static void on_wheel_event(int32_t x, int32_t y, int32_t delta, void *userdata) {
    application_t *app = (application_t *)userdata;
    if (!app->options.control) return;
    if (app->device_width == 0 || app->device_height == 0) return;

    int32_t dev_x, dev_y;
    input_transform_coords(x, y, &dev_x, &dev_y,
                           app->window.width, app->window.height,
                           app->device_width, app->device_height);

    int32_t vscroll = delta / WHEEL_DELTA;

    uint8_t buf[64];
    int32_t args[7] = {
        dev_x, dev_y,
        (int32_t)app->device_width, (int32_t)app->device_height,
        0, vscroll, 0
    };
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT,
                                          args, buf, sizeof(buf));
    if (len > 0) {
        control_socket_send_msg(&app->control_sock, buf, len);
    }
}

static void on_resize(int32_t width, int32_t height, void *userdata) {
    application_t *app = (application_t *)userdata;
    if (width > 0 && height > 0) {
        d3d_context_resize(&app->d3d_ctx, width, height);
    }
}
```

- [ ] **Step 3: Update `src/meson.build` to include new source files**

Add the new input source files to `input_src`:

```meson
input_src = files(
    'input/keyboard.c',
    'input/mouse.c',
    'input/gamepad.c',
    'input/keycode_map.c',
    'input/input_transform.c',
)
```

- [ ] **Step 4: Verify build**

Run: `ninja -C builddir`
Expected: Build succeeds

- [ ] **Step 5: Commit**

```bash
git add src/app/application.h src/app/application.c src/meson.build
git commit -m "feat: implement event loop with threaded receivers and input integration"
```

---

## Task 19: Audio Player Module (WASAPI)

**Files:**
- Create: `src/audio/player.h`
- Create: `src/audio/player.c`

- [ ] **Step 1: Create `player.h`**

```c
#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct audio_player audio_player_t;

audio_player_t *audio_player_create(void);
bool audio_player_init(audio_player_t *player, uint32_t sample_rate,
                        uint32_t channels);
bool audio_player_write(audio_player_t *player, const uint8_t *data,
                         uint32_t size);
void audio_player_destroy(audio_player_t *player);

#endif /* AUDIO_PLAYER_H */
```

- [ ] **Step 2: Create `player.c`**

```c
#include "player.h"
#include "../platform/log.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <stdlib.h>
#include <string.h>

struct audio_player {
    IAudioClient *client;
    IAudioRenderClient *render_client;
    uint32_t buffer_frames;
    uint32_t sample_rate;
    uint32_t channels;
};

audio_player_t *audio_player_create(void) {
    return calloc(1, sizeof(audio_player_t));
}

bool audio_player_init(audio_player_t *player, uint32_t sample_rate,
                        uint32_t channels) {
    player->sample_rate = sample_rate;
    player->channels = channels;

    HRESULT hr;
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice *device = NULL;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator, (void **)&enumerator);
    if (FAILED(hr)) {
        log_error("Failed to create device enumerator: 0x%08x", hr);
        return false;
    }

    hr = enumerator->lpVtbl->GetDefaultAudioEndpoint(enumerator, eRender,
                                                      eConsole, &device);
    enumerator->lpVtbl->Release(enumerator);
    if (FAILED(hr)) {
        log_error("Failed to get default audio endpoint: 0x%08x", hr);
        return false;
    }

    hr = device->lpVtbl->Activate(device, &IID_IAudioClient, CLSCTX_ALL,
                                   NULL, (void **)&player->client);
    device->lpVtbl->Release(device);
    if (FAILED(hr)) {
        log_error("Failed to activate audio client: 0x%08x", hr);
        return false;
    }

    WAVEFORMATEX wfx = {0};
    wfx.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    wfx.nChannels = (WORD)channels;
    wfx.nSamplesPerSec = sample_rate;
    wfx.wBitsPerSample = 32;
    wfx.nBlockAlign = (WORD)(channels * 4);
    wfx.nAvgBytesPerSec = sample_rate * wfx.nBlockAlign;

    REFERENCE_TIME duration = 10000000; /* 1 second buffer */
    hr = player->client->lpVtbl->Initialize(player->client,
                                             AUDCLNT_SHAREMODE_SHARED,
                                             0, duration, 0, &wfx, NULL);
    if (FAILED(hr)) {
        log_error("Failed to initialize audio client: 0x%08x", hr);
        return false;
    }

    hr = player->client->lpVtbl->GetService(player->client,
                                             &IID_IAudioRenderClient,
                                             (void **)&player->render_client);
    if (FAILED(hr)) {
        log_error("Failed to get render client: 0x%08x", hr);
        return false;
    }

    UINT32 buffer_size;
    player->client->lpVtbl->GetBufferSize(player->client, &buffer_size);
    player->buffer_frames = buffer_size;

    player->client->lpVtbl->Start(player->client);

    log_info("Audio player initialized: %u Hz, %u channels", sample_rate, channels);
    return true;
}

bool audio_player_write(audio_player_t *player, const uint8_t *data,
                         uint32_t size) {
    if (!player->render_client) return false;

    UINT32 padding;
    player->client->lpVtbl->GetCurrentPadding(player->client, &padding);

    UINT32 available = player->buffer_frames - padding;
    UINT32 frames = size / (player->channels * 4); /* float32 */
    if (frames > available) frames = available;
    if (frames == 0) return true;

    BYTE *buffer;
    HRESULT hr = player->render_client->lpVtbl->GetBuffer(
        player->render_client, frames, &buffer);
    if (FAILED(hr)) return false;

    memcpy(buffer, data, frames * player->channels * 4);

    player->render_client->lpVtbl->ReleaseBuffer(player->render_client, frames, 0);
    return true;
}

void audio_player_destroy(audio_player_t *player) {
    if (!player) return;

    if (player->client) {
        player->client->lpVtbl->Stop(player->client);
        player->client->lpVtbl->Release(player->client);
    }
    if (player->render_client) {
        player->render_client->lpVtbl->Release(player->render_client);
    }

    free(player);
}
```

- [ ] **Step 3: Update `src/meson.build` to include audio sources**

Add to `src/meson.build`:

```meson
# Audio sources
audio_src = files(
    'audio/player.c',
)
```

And add `audio_src` to the executable sources list.

Also add `ole32` dependency if not already present (it is already in winlibs).

- [ ] **Step 4: Verify build**

Run: `ninja -C builddir`
Expected: Build succeeds

- [ ] **Step 5: Commit**

```bash
git add src/audio/player.h src/audio/player.c src/meson.build
git commit -m "feat: implement WASAPI audio player"
```

---

## Task 20: Test Infrastructure

**Files:**
- Create: `tests/meson.build`

- [ ] **Step 1: Create `tests/meson.build`**

```meson
# Test executables

test_adb = executable('test_adb',
    'test_adb.c',
    '../src/adb/adb.c',
    '../src/adb/protocol.c',
    '../src/adb/session.c',
    '../src/adb/transport.c',
    '../src/adb/crypto.c',
    '../src/adb/tls.c',
    '../src/platform/log.c',
    '../src/platform/thread.c',
    dependencies: [mbedtls_dep] + winlibs,
)
test('adb', test_adb)

test_protocol = executable('test_protocol',
    'test_protocol.c',
    '../src/adb/protocol.c',
    '../src/platform/log.c',
    dependencies: [mbedtls_dep] + winlibs,
)
test('protocol', test_protocol)

test_control_msg = executable('test_control_msg',
    'test_control_msg.c',
    '../src/control/control_msg.c',
    '../src/platform/log.c',
    dependencies: winlibs,
)
test('control_msg', test_control_msg)

test_decode = executable('test_decode',
    'test_decode.c',
    '../src/decode/video_decoder.c',
    '../src/decode/audio_decoder.c',
    '../src/platform/log.c',
    dependencies: [libavcodec_dep, libavutil_dep] + winlibs,
)
test('decode', test_decode)
```

- [ ] **Step 2: Update root `meson.build` to include tests**

In the root `meson.build`, change `subdir('tests')` to properly include the test build file. The current `meson.build` already has `subdir('tests')` at line 41.

- [ ] **Step 3: Verify build and run all tests**

Run: `ninja -C builddir && meson test -C builddir`
Expected: All tests pass

- [ ] **Step 4: Commit**

```bash
git add tests/meson.build
git commit -m "feat: add test build infrastructure"
```

---

## Final Verification

- [ ] **Step 1: Full rebuild**

Run: `meson setup builddir --wipe && ninja -C builddir`
Expected: Clean build succeeds

- [ ] **Step 2: Run all tests**

Run: `meson test -C builddir`
Expected: All tests pass

- [ ] **Step 3: Final commit**

```bash
git add -A
git commit -m "feat: comprehensive completion of all modules

- Platform: add platform_sleep_ms
- ADB: implement adb_push (sync protocol), adb_forward, session_poll, TLS integration
- Device: server lifecycle management, socket accept, device_msg serialization
- Decode: NV12 output path for video decoder
- Render: shader bytecode loading, NV12 texture, aspect ratio support
- Input: WndProc input handling, VK→Android keycode mapping, coordinate transform
- Control: message serialization for all types
- Record: AV1/FLAC codec support
- App: event loop redesign, threaded receivers, WASAPI audio player
- Tests: control message serialization tests, decode tests, test infrastructure"
```
