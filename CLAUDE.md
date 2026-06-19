# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

AutoScrcpy 是 scrcpy 的 Windows 原生实现，目标是在 Windows 上完整复现 scrcpy 的全部功能。

## Hard Constraints (必须遵守)

1. **Win32 + DirectX 11** — 不使用 SDL。窗口管理用 Win32 API，视频渲染用 D3D11。禁止重新引入 SDL 依赖。
2. **原生 ADB 协议** — 不依赖 adb 二进制。参考 `reference/adb-impl/` 的实现，shell/push/forward 等操作全部用 C 代码实现，直接与设备 adbd 通信，不经过 adb daemon。
3. **依赖管控** — 构建系统使用 Meson + Ninja + Clang。三方依赖通过 Meson `subprojects/` wrap 管理，目前只允许 FFmpeg 和 mbedtls。引入任何新依赖必须获得用户明确同意。不使用 vcpkg 或其他三方包管理工具。
4. **模块化设计** — 分层架构，模块间松耦合。遵循开闭原则（对扩展开放、对修改关闭）和单一职责原则。
5. **reference/ 只读** — `reference/` 下的代码仓仅供查阅参考，禁止修改其内容。

## Build Commands

```bash
# Configure (uses clang toolchain via cross file)
meson setup builddir --native-file meson-native-clang-gcc.ini

# Build
ninja -C builddir
```

## Testing

### Automatic Tests (无需设备，CI 安全)

```bash
meson test -C builddir
```

覆盖 38 个用例：binary.h、input_transform、keycode_map、control_msg、crypto、protocol、adb 初始化。

### Manual Tests (需要设备)

```bash
# 统一入口：运行所有设备相关测试
./builddir/tests/test_device.exe <serial>

# 示例
./builddir/tests/test_device.exe 192.168.13.197:5555
```

设备测试覆盖 7 个用例：
- ADB 连接：TCP 连接、握手、断开
- Server 流程：推送 JAR、元数据、视频通道
- 视频管线：帧解码、NV12 转换、多帧一致性

**注意：** 设备测试必须通过 `argv[1]` 提供 `host:port`，无默认值。测试前确保设备上没有残留的 scrcpy-server 进程。

### 测试文件

| 文件 | 类型 | 用例数 | 说明 |
|------|------|--------|------|
| `test_binary.c` | 自动 | 6 | 字节序读写 |
| `test_input_transform.c` | 自动 | 6 | 坐标变换 |
| `test_keycode_map.c` | 自动 | 7 | VK→Android 键码 |
| `test_control_msg.c` | 自动 | 8 | 消息序列化 |
| `test_crypto.c` | 自动 | 7 | RSA 密钥/签名 |
| `test_protocol.c` | 自动 | 2 | 协议 checksum |
| `test_adb.c` | 自动 | 2 | ADB 初始化 |
| `test_device.c` | 手动 | 7 | 设备统一入口 |

## Architecture

The codebase is organized into layered modules under `src/`. Each module has a single responsibility and exposes a minimal public API through its header.

### Module Dependency Flow (top → bottom)

```
main.c → app/ → device/ → adb/
                  ↓          ↓
               decode/    platform/
                  ↓
               render/ (D3D11)
```

### Module Summary

| Module | Purpose |
|--------|---------|
| `src/platform/` | OS abstraction: socket types (`SOCKET_T`), threads (`thread_t`/`mutex_t`/`cond_t`), logging. The `platform.h` header provides cross-platform socket macros but the project targets Windows primarily. |
| `src/adb/` | Native ADB protocol: connection lifecycle (`adb.c`), wire format (`protocol.c`), AUTH/crypto (`crypto.c`, `tls.c`), channel multiplexing (`session.c`). Talks directly to adbd on port 5555. |
| `src/device/` | Device-side socket management: `server.c` pushes scrcpy-server.jar and starts it; `video_socket.c`/`audio_socket.c`/`control_socket.c` handle the three scrcpy protocol streams; `demuxer.c` parses the scrcpy packet framing. |
| `src/decode/` | FFmpeg-based decoders for video (H.264/H.265/AV1) and audio (Opus/AAC/FLAC). `frame_buffer` and `packet_queue` provide thread-safe producer/consumer patterns. |
| `src/render/` | DirectX 11 rendering pipeline: `d3d_context.c` manages device/swapchain/viewport, `video_renderer.c` draws NV12 frames via single NV12 texture (staging upload) + Y/UV SRVs + aspect-ratio-preserving transform, `shader.c` wraps D3D shader resources. Reference: `reference/d3d_video/src/render/`. |
| `src/input/` | Win32 input capture: `keyboard.c`, `mouse.c`, `gamepad.c` translate Win32 messages to scrcpy control events. `keycode_map.c` maps VK codes to Android keycodes. |
| `src/control/` | Control message serialization (`control_msg.c`) and a threaded sender (`controller.c`) with a lock-free queue. Also `clipboard.c` and `power.c`. |
| `src/audio/` | WASAPI audio playback (`player.c`) with a timing regulator (`regulator.c`). |
| `src/record/` | Recording via FFmpeg muxer: `recorder.c` orchestrates, `muxer.c` writes MP4/MKV containers. |
| `src/app/` | Application lifecycle (`application.c`), Win32 window management (`window.c`), CLI parsing (`cli.c`), option defaults (`options.c`). |

### Key Data Flow

1. `server.c` connects to device adbd via ADB protocol (TCP + TLS handshake)
2. `server.c` pushes `scrcpy-server.jar` via ADB sync protocol
3. `server.c` starts scrcpy-server via `adb shell` with `tunnel_forward=true`
4. `server.c` opens `localabstract:scrcpy` ADB channel for video stream
5. `server.c` reads metadata (dummy + device_name + codec/dimensions + session_header) from socketpair
6. `adb_reader_thread` reads ADB WRTE messages, sends OKAY ACKs, writes video data to socketpair
7. `video_thread` reads raw H.264 from socketpair → FFmpeg decoder → NV12 `shared_frame` (lock-free swap)
8. Main thread idle loop: `PeekMessage` → if frame ready → `d3d_context_begin_frame` → `video_renderer_render` → `d3d_context_end_frame`

### scrcpy-server Protocol (v3.3.2, tunnel_forward=true)

Video stream data format (bytes sent on the ADB channel):
```
[1B dummy=0x00] [64B device_name] [4B codec_id + 4B width + 4B height] [12B session_header] [raw H.264 Annex B stream...]
```

- Codec IDs: H.264=`0x68323634`, H.265=`0x68323635`, AV1=`0x00617631`
- All multi-byte integers are big-endian
- Session header: first byte `0x80` flags, then width/height (may be 0/31, use codec block dimensions)
- After metadata, data is raw H.264 Annex B (start codes + NAL units), NOT scrcpy packet-framed

### Threading Model

- **Main thread**: Win32 message loop + D3D11 rendering (PeekMessage idle pattern from reference/d3d_video MessageLoop)
- **ADB reader thread**: reads ADB messages from device, sends OKAY flow control, writes video data to socketpair
- **Video thread**: reads H.264 from socketpair, decodes via FFmpeg, writes NV12 frames to `shared_frame` via `InterlockedExchangePointer` (no D3D operations)
- **Audio thread**: reads from audio socket, decodes, writes to audio player (currently disabled)
- **Controller thread**: drains control message queue, sends to device (currently disabled)

**Important**: D3D11 device is created on the main thread (same thread as the Win32 window — DXGI requirement). D3D11 multi-threaded protection is enabled via `ID3D10Multithread::SetMultithreadProtected(TRUE)` as a safety net.

## Third-Party Dependencies

当前已引入的依赖（详见 Hard Constraints 中的依赖管控规则）：

| Dependency | Purpose | Wrap |
|------------|---------|------|
| FFmpeg | Video/audio decoding and recording mux | `subprojects/ffmpeg.wrap` (meson-7.1 branch) |
| mbedtls | TLS for ADB AUTH handshake | `subprojects/mbedtls.wrap` (v3.6.2) |

Win32 system libraries linked directly: d3d11, dxgi, user32, kernel32, gdi32, ws2_32, imm32, bcrypt, mmdevapi, uuid, ole32, oleaut32.

## Coding Conventions

- **C11** standard, compiled with Clang
- Header guards: `#ifndef MODULE_H` / `#define MODULE_H` / `#endif /* MODULE_H */`
- Types use `_t` suffix for structs (`video_renderer_t`, `adb_connection_t`)
- Boolean returns: `true` = success, `false` = failure
- Memory: caller allocates, callee initializes; `_init`/`_destroy` pattern for stack objects, `_create`/`_destroy` for heap objects
- Logging: use `log_info()`, `log_error()`, etc. from `platform/log.h` (never `printf`)
- Endianness: use `read32be()`/`write32be()` from `adb/binary.h` for wire format

## Git Workflow

- **master** branch is the stable mainline
- All new development on feature branches: `feat/<desc>`, `fix/<desc>`, `refactor/<desc>`
- Merge to master only after testing and confirmation
- Milestone tags: `v0.1.0` (video streaming working)

### Current Milestone: v0.1.0
Video streaming fully working - ADB connection, H.264 decoding, D3D11 rendering, aspect ratio correction.

## Project Status

### Working
- ✅ ADB connection to device (TCP/IP, TLS handshake, AUTH)
- ✅ Push scrcpy-server.jar via ADB sync protocol
- ✅ Start scrcpy-server via adb shell
- ✅ Video channel negotiation (localabstract:scrcpy)
- ✅ H.264 video decoding (FFmpeg, raw Annex B via `avcodec_send_packet`)
- ✅ D3D11 NV12 video rendering (single NV12 texture + staging upload + R8/R8G8 SRVs + HLSL shaders)
- ✅ Aspect ratio correction (letterbox/pillarbox via vertex transform matrix)
- ✅ Window resize → D3D resize + aspect ratio update
- ✅ Main-thread rendering with PeekMessage idle loop (reference MessageLoop pattern)

### Not Yet Working
- ❌ Audio streaming (server started with `audio=false`)
- ❌ Control input (server started with `control=false`)
- ❌ Recording
- ❌ Clipboard sync

### Known Issues
- H.264 `No start code is found` errors — raw `recv()` chunks may not align with NAL boundaries; decoder self-recovers
- H.264 P-frame `concealing errors` — caused by initial data misalignment or socketpair buffer overflow
- First few frames may have artifacts until decoder accumulates reference frames (SPS/PPS)

## Reference Code

`reference/` contains read-only reference repositories:
- `reference/scrcpy/` — upstream scrcpy source (protocol and architecture reference)
- `reference/adb/` — ADB protocol implementation reference
- `reference/adb-server/` — ADB server reference
- `reference/d3d_video/` — D3D11 video rendering reference (SwapChainManager, VideoQuad, TextureUpdater, MessageLoop)

**Never modify files under `reference/`.**

## Communication Protocol

所有与用户的问题和结果通过微信发送，不假设用户查看终端。

1. 启动后台 `wait`（无超时），永久阻塞等待消息
2. 收到消息 → 处理 → 通过微信回复 → 启动新的 `wait`
3. 所有问题和结果通过微信发送，不假设用户查看终端
