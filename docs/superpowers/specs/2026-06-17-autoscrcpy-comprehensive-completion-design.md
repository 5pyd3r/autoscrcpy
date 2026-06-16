# AutoScrcpy Comprehensive Completion Design

## Overview

This spec covers the bottom-up completion of all remaining modules in the autoscrcpy project. The project has a fully scaffolded structure with basic implementations, but several modules contain stubs, TODOs, or placeholder code. This design defines the work needed to achieve full scrcpy feature parity on Windows.

## Strategy

**Bottom-up approach**: complete each layer fully before building on it, ensuring a solid foundation at every level.

---

## Layer 1: Platform (`src/platform/`)

### Current State
- `log.h/c`: Complete — leveled logging with file/line info
- `thread.h/c`: Complete — Win32/pthreads abstraction
- `atomic.h`: Complete — Interlocked/stdatomic abstraction
- `platform.h`: Complete — Socket abstraction (SOCKET_T, CLOSESOCKET, etc.)

### Required Changes
- Add `platform_sleep_ms(uint32_t ms)` helper to `platform.h`
- No other changes needed

---

## Layer 2: ADB (`src/adb/`)

### Current State
- `protocol.h/c`: Complete — send/recv ADB messages, checksum
- `session.h/c`: Partial — connect, auth, open/close channel, message dispatch
- `crypto.h/c`: Complete — RSA key load, sign, public key export (via mbedtls)
- `tls.h/c`: Complete — TLS handshake via mbedtls
- `adb.h/c`: Partial — `adb_connect`, `adb_shell` work; `adb_push`, `adb_forward` are stubs

### Required Changes

#### 2.1 Implement `adb_push` (File Push via Sync Protocol)

The ADB sync protocol uses a dedicated `sync:` channel for file transfer:

1. Open a `sync:` channel via `session_open_channel`
2. Send `SEND` command (4 bytes: "SEND") + path length (4 bytes LE) + path string
3. Stream file data in chunks: `DATA` (4 bytes) + chunk size (4 bytes LE) + data
4. Send `DONE` (4 bytes) + 0 (4 bytes) to signal completion
5. Read `OKAY` response to confirm success

```c
bool adb_push(adb_connection_t *conn, const char *local_path, const char *remote_path);
```

Implementation:
- Open local file with `fopen`
- Open sync channel: `session_open_channel(conn, "sync:")`
- Wait for channel to reach `CHAN_OPEN` state (poll messages)
- Send SEND + remote_path
- Read local file in 64KB chunks, send DATA + chunk
- Send DONE, read OKAY
- Close channel

#### 2.2 Implement `adb_forward` (Port Forwarding)

```c
bool adb_forward(adb_connection_t *conn, uint16_t local_port, const char *remote_spec);
```

Implementation (tunnel_forward mode, used by scrcpy):
- Open a local TCP listener on `local_port`
- scrcpy-server will connect to this port after starting
- No ADB forward command needed — the server uses `tunnel_forward=true` and connects directly

#### 2.3 Event Loop Enhancement

Add non-blocking message polling to support concurrent channel I/O:

```c
// Poll for incoming messages, dispatch to channel handlers
int session_poll(adb_connection_t *conn, int timeout_ms);
```

- Use `select()` with timeout on the ADB socket
- Process any pending messages via `session_handle_message`
- Return number of messages processed, or -1 on error

#### 2.4 TLS Integration in Session

When the server sends `ADB_STLS`, initiate TLS negotiation:
- After receiving STLS, call `tls_handshake(conn->fd)`
- Store TLS context in `conn->tls_ctx`
- All subsequent send/recv use TLS variants

---

## Layer 3: Device (`src/device/`)

### Current State
- `server.h/c`: Partial — `server_push` and `server_start` call ADB but don't manage lifecycle; `server_kill` is stub
- `video_socket.h/c`: Partial — has read_packet but no connection establishment
- `audio_socket.h/c`: Partial — same as video_socket
- `control_socket.h/c`: Partial — has send/recv but no connection establishment
- `device_msg.h/c`: Partial — has deserialization but not serialization

### Required Changes

#### 3.1 Server Lifecycle Management

Redesign `server.c` to manage the full lifecycle:

```c
typedef struct server server_t;

bool server_init(server_t *srv, const struct server_config *config);
bool server_start(server_t *srv);  // push + start + wait for connections
void server_kill(server_t *srv);
void server_destroy(server_t *srv);
```

`server_start` flow:
1. Connect to ADB daemon (`adb_connect`)
2. Push scrcpy-server.jar (`adb_push`)
3. Start scrcpy-server via `adb_shell` with tunnel_forward=true
4. Open local TCP listener on `config->local_port`
5. Accept 2-3 connections (video, audio, control) with dummy byte handling
6. Assign sockets to video/audio/control socket structs

#### 3.2 Socket Connection Establishment

Each socket module gets an `accept` function:

```c
bool video_socket_accept(video_socket_t *sock, SOCKET_T listen_fd);
bool audio_socket_accept(audio_socket_t *sock, SOCKET_T listen_fd);
bool control_socket_accept(control_socket_t *sock, SOCKET_T listen_fd);
```

Each accept:
1. `accept()` on the listen socket
2. Read 1 byte dummy byte (scrcpy protocol)
3. Initialize socket state

#### 3.3 Device Message Serialization

Add serialization functions to `device_msg.c`:

```c
int device_msg_serialize_clipboard(const char *text, uint32_t len,
                                   uint64_t sequence, uint8_t *buf, uint32_t buf_size);
int device_msg_serialize_ack_clipboard(uint64_t sequence,
                                        uint8_t *buf, uint32_t buf_size);
```

---

## Layer 4: Decode (`src/decode/`)

### Current State
- `video_decoder.h/c`: Partial — codec init works, but `video_decoder_decode` outputs black frames (placeholder YUV→BGRA)
- `audio_decoder.h/c`: Complete — decodes opus/aac/flac
- `packet_queue.h/c`: Complete — thread-safe ring buffer

### Required Changes

#### 4.1 NV12 Output Path (Preferred)

Instead of converting YUV→BGRA on CPU, output NV12 directly for D3D11:

```c
// video_frame_t format values:
// 0 = NV12 (preferred, GPU-native)
// 1 = BGRA (fallback)

bool video_decoder_decode(video_decoder_t *decoder, const uint8_t *data,
                          uint32_t size, video_frame_t *frame);
```

Changes to `video_decoder_decode`:
- Set `frame->format = 0` (NV12)
- Allocate NV12 buffer: `width * height * 3 / 2` bytes
- Copy Y plane: `frame->data` = `decoder->frame->data[0]`, stride = `frame->width`
- Copy UV plane: `frame->data + width*height` = `decoder->frame->data[1]`, interleaved UV

If the decoder outputs YUV420P (3 planes), interleave UV:
```
for each row:
  U[i] = frame->data[1][i]
  V[i] = frame->data[2][i]
  → NV12 UV plane: U0 V0 U1 V1 ...
```

#### 4.2 Fallback BGRA via Swscale

If NV12 path is not feasible, use libswscale:

```c
// In video_decoder struct:
struct SwsContext *sws_ctx;

// In decode:
sws_ctx = sws_getContext(width, height, AV_PIX_FMT_YUV420P,
                         width, height, AV_PIX_FMT_BGRA,
                         SWS_BILINEAR, NULL, NULL, NULL);
sws_scale(sws_ctx, frame->data, frame->linesize, 0, height,
          out_frame->data, out_linesize);
```

---

## Layer 5: Render (`src/render/`)

### Current State
- `d3d_context.h/c`: Complete — device, swapchain, resize, begin/end frame
- `shader.h/c`: Complete — create/bind/destroy, but needs compiled bytecode
- `texture.h/c`: Partial — only BGRA texture, no NV12 support
- `video_renderer.h/c`: Partial — shader loading is TODO

### Required Changes

#### 5.1 Shader Compilation Pipeline

Use pre-compiled shader bytecode embedded as static C arrays. This avoids a build-time dependency on `fxc.exe`.

Steps:
1. Manually compile `VertexShader.hlsl` and `PixelShader.hlsl` with `fxc.exe` once
2. Convert the `.cso` output to C byte arrays (e.g., `xxd -i`)
3. Store in `src/render/shader_bytecode.h` as `static const uint8_t vs_bytecode[]` and `ps_bytecode[]`
4. `shader_init` reads directly from these arrays

For NV12 rendering, also compile and embed an NV12 pixel shader bytecode.

#### 5.2 NV12 Pixel Shader

For NV12 rendering, need a custom pixel shader that converts NV12→BGRA:

```hlsl
Texture2D y_tex : register(t0);
Texture2D uv_tex : register(t1);
SamplerState sam : register(s0);

float4 main(PS_INPUT input) : SV_Target {
    float y = y_tex.Sample(sam, input.tex).r;
    float2 uv = uv_tex.Sample(sam, input.tex).rg - 0.5;
    float r = y + 1.402 * uv.y;
    float g = y - 0.344 * uv.x - 0.714 * uv.y;
    float b = y + 1.772 * uv.x;
    return float4(r, g, b, 1.0);
}
```

#### 5.3 NV12 Texture Support

```c
// Create two textures: Y (R8) and UV (R8G8)
bool texture_init_nv12(texture_t *y_tex, texture_t *uv_tex,
                       ID3D11Device *device, uint32_t width, uint32_t height);

// Update NV12 planes
bool texture_update_nv12(texture_t *y_tex, texture_t *uv_tex,
                         ID3D11DeviceContext *ctx,
                         const uint8_t *nv12_data, uint32_t width, uint32_t height);
```

#### 5.4 Aspect Ratio

In `video_renderer_render`, calculate viewport to maintain aspect ratio:

```c
float video_aspect = (float)frame->width / frame->height;
float window_aspect = (float)ctx->width / ctx->height;
// Adjust viewport to letterbox/pillarbox
```

---

## Layer 6: Input (`src/input/` + `src/app/window.c`)

### Current State
- `keyboard.h/c`: Has `keyboard_process_event` but no keycode mapping
- `mouse.h/c`: Has `mouse_process_event` but no coordinate transform
- `gamepad.h/c`: Has `gamepad_process_event` but basic
- `window.c`: WndProc only handles `WM_DESTROY`

### Required Changes

#### 6.1 WndProc Input Handling

Expand `window.c` WndProc to handle:

```c
case WM_KEYDOWN:
case WM_KEYUP:
case WM_SYSKEYDOWN:
case WM_SYSKEYUP:
    // Map VK code → Android keycode
    // Build control_msg_inject_keycode
    // Send via control_socket

case WM_LBUTTONDOWN:
case WM_LBUTTONUP:
case WM_MOUSEMOVE:
case WM_RBUTTONDOWN:
case WM_RBUTTONUP:
case WM_MBUTTONDOWN:
case WM_MBUTTONUP:
case WM_MOUSEWHEEL:
    // Transform window coords → device coords
    // Build control_msg_inject_touch / scroll
    // Send via control_socket

case WM_SIZE:
    // Trigger D3D11 resize
```

#### 6.2 Keycode Mapping Table

Create `src/input/keycode_map.h`:

```c
// Windows VK → Android KeyEvent.KEYCODE_*
uint32_t vk_to_android_keycode(uint32_t vk);
```

Mapping table covering standard keys, media keys, etc.

#### 6.3 Coordinate Transformation

```c
// Transform window client coords to device screen coords
void input_transform_coords(int32_t win_x, int32_t win_y,
                            int32_t *dev_x, int32_t *dev_y,
                            int32_t win_w, int32_t win_h,
                            uint32_t dev_w, uint32_t dev_h);
```

Must account for letterboxing (black bars) from aspect ratio preservation.

---

## Layer 7: Control (`src/control/`)

### Current State
- `control_msg.h`: Only has enum, no serialization
- `clipboard.h/c`: Complete for Win32
- `power.h/c`: Stub

### Required Changes

#### 7.1 Control Message Serialization

Add to `control_msg.c`:

```c
// Serialize inject keycode message
int control_msg_serialize_inject_keycode(uint32_t keycode, uint32_t action,
                                          uint32_t repeat,
                                          uint8_t *buf, uint32_t buf_size);

// Serialize inject touch event
int control_msg_serialize_inject_touch(int32_t x, int32_t y,
                                        uint32_t width, uint32_t height,
                                        uint32_t action, uint32_t buttons,
                                        uint8_t *buf, uint32_t buf_size);

// Serialize inject scroll event
int control_msg_serialize_inject_scroll(int32_t x, int32_t y,
                                         uint32_t width, uint32_t height,
                                         int32_t h_scroll, int32_t v_scroll,
                                         uint8_t *buf, uint32_t buf_size);

// Serialize set clipboard
int control_msg_serialize_set_clipboard(const char *text, uint32_t len,
                                         uint64_t sequence, bool paste,
                                         uint8_t *buf, uint32_t buf_size);

// Serialize set display power
int control_msg_serialize_set_display_power(bool on,
                                             uint8_t *buf, uint32_t buf_size);
```

Wire format: 1 byte type + type-specific fields, packed in network byte order.

#### 7.2 Power Control

```c
bool power_set_screen_power(bool on) {
    // Send via ADB shell: "input keyevent KEYCODE_POWER"
    // Or use control message: CONTROL_MSG_TYPE_SET_DISPLAY_POWER
}
```

---

## Layer 8: Record (`src/record/`)

### Current State
- `recorder.h/c`: Complete — create, init, write video/audio, destroy
- `muxer.h/c`: Complete — FFmpeg muxer wrapper

### Required Changes
- Minor: add AV1 codec support in `muxer_add_video_stream`
- Minor: add FLAC codec support in `muxer_add_audio_stream`
- Otherwise complete

---

## Layer 9: App (`src/app/`)

### Current State
- `application.h/c`: Partial — init/run/destroy, but run has placeholder video loop
- `window.h/c`: Partial — create/show/fullscreen/always-on-top, but WndProc is minimal
- `options.h/c`: Complete
- `cli.h/c`: Complete (assumed)

### Required Changes

#### 9.1 Event Loop Redesign

Replace blocking `GetMessage` with `MsgWaitForMultipleObjects`:

```c
int application_run(application_t *app) {
    // 1. Push and start server
    // 2. Accept video/audio/control sockets
    // 3. Start video/audio receiver threads
    // 4. Enter event loop

    HANDLE events[1] = { app->stop_event };
    while (app->running) {
        DWORD result = MsgWaitForMultipleObjects(
            1, events, FALSE, INFINITE, QS_ALLINPUT);

        if (result == WAIT_OBJECT_0) {
            // Process Windows messages
            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    app->running = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        } else if (result == WAIT_OBJECT_0 + 1) {
            // Stop event signaled
            app->running = false;
        }
    }
}
```

#### 9.2 Threaded Receiver Model

```c
// Video receiver thread
static DWORD WINAPI video_receiver_thread(LPVOID arg) {
    application_t *app = arg;
    while (app->running) {
        uint8_t *data;
        uint32_t size;
        if (!video_socket_read_packet(&app->video_sock, &data, &size)) break;

        // Decode
        video_frame_t frame;
        if (video_decoder_decode(app->video_decoder, data, size, &frame)) {
            // Queue for rendering
            renderer_submit_frame(&app->renderer, &frame);
        }
        free(data);
    }
    return 0;
}

// Audio receiver thread — similar pattern
```

#### 9.3 Audio Player (New Module)

Create `src/audio/player.h/c`:

```c
typedef struct audio_player audio_player_t;

audio_player_t *audio_player_create(void);
bool audio_player_init(audio_player_t *player, uint32_t sample_rate,
                        uint32_t channels);
bool audio_player_write(audio_player_t *player, const uint8_t *data,
                         uint32_t size);
void audio_player_destroy(audio_player_t *player);
```

Implementation uses WASAPI:
- `IMMDeviceEnumerator` → get default audio endpoint
- `IAudioClient` → initialize shared mode, 48kHz float
- `IAudioRenderClient` → write decoded PCM frames
- Pull model: audio thread consumes from packet_queue

#### 9.4 Window Resize Handling

Wire `WM_SIZE` to `d3d_context_resize`:
- Update swap chain dimensions
- Recalculate viewport for aspect ratio
- Trigger re-render

---

## Dependencies Between Layers

```
Platform ← ADB ← Device ← App
                  ↑
         Decode ←─┤
                  ↑
         Render ←─┤
                  ↑
         Input  ←─┤
                  ↑
         Control ←┘
```

- ADB depends on Platform
- Device depends on ADB
- Decode depends on Platform (threading)
- Render depends on Decode (frame format) and Platform (D3D11)
- Input depends on Control (message format) and Window
- App orchestrates all layers

---

## Testing Strategy

### Per-Layer Tests
- ADB: `test_adb.c` — extend for push/forward
- Protocol: `test_protocol.c` — extend for sync protocol
- Decode: `test_decode.c` — add YUV→NV12 conversion test
- Render: `test_render.c` — add texture upload test
- Control: `test_control_msg.c` — add serialization tests

### Integration Tests
- Full video pipeline: mock socket → decode → render
- Full input pipeline: Win32 msg → control msg → socket

---

## Build System Changes

### Shader Bytecode
No build system changes needed for shaders. Pre-compiled bytecode is embedded in `shader_bytecode.h` (generated once manually with `fxc.exe` + `xxd`).

### Test Targets
Add test executables to `tests/meson.build`:
```meson
test_adb = executable('test_adb', 'test_adb.c', adb_src, platform_src,
    dependencies: [mbedtls_dep] + winlibs)
test('adb', test_adb)

test_protocol = executable('test_protocol', 'test_protocol.c', adb_src, platform_src,
    dependencies: [mbedtls_dep] + winlibs)
test('protocol', test_protocol)

test_control_msg = executable('test_control_msg', 'test_control_msg.c', control_src, platform_src,
    dependencies: winlibs)
test('control_msg', test_control_msg)
```

---

## Implementation Order (Bottom-Up)

| Phase | Layer | Key Deliverables |
|-------|-------|-----------------|
| 1 | Platform | `platform_sleep_ms` |
| 2 | ADB | `adb_push`, `adb_forward`, `session_poll`, TLS integration |
| 3 | Device | Server lifecycle, socket accept, device_msg serialization |
| 4 | Decode | NV12 output path (or swscale fallback) |
| 5 | Render | Shader compilation, NV12 texture, aspect ratio |
| 6 | Input | WndProc, keycode mapping, coordinate transform |
| 7 | Control | Message serialization, power control |
| 8 | Record | AV1/FLAC codec support |
| 9 | App | Event loop, threaded receivers, audio player, resize |
