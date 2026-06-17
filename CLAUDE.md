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

# Run tests
meson test -C builddir

# Run a single test
meson test -C builddir --test-name "ADB test"
```

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
| `src/render/` | DirectX 11 rendering pipeline: `d3d_context.c` manages device/swapchain, `video_renderer.c` draws NV12 frames via Y/UV textures and HLSL shaders, `shader.c`/`texture.c` wrap D3D resources. |
| `src/input/` | Win32 input capture: `keyboard.c`, `mouse.c`, `gamepad.c` translate Win32 messages to scrcpy control events. `keycode_map.c` maps VK codes to Android keycodes. |
| `src/control/` | Control message serialization (`control_msg.c`) and a threaded sender (`controller.c`) with a lock-free queue. Also `clipboard.c` and `power.c`. |
| `src/audio/` | WASAPI audio playback (`player.c`) with a timing regulator (`regulator.c`). |
| `src/record/` | Recording via FFmpeg muxer: `recorder.c` orchestrates, `muxer.c` writes MP4/MKV containers. |
| `src/app/` | Application lifecycle (`application.c`), Win32 window management (`window.c`), CLI parsing (`cli.c`), option defaults (`options.c`). |

### Key Data Flow

1. `server.c` uses ADB to push and execute the scrcpy server JAR on the device
2. Device opens three sockets: video, audio, control
3. `video_socket` → `demuxer` → `video_decoder` (FFmpeg) → `video_renderer` (D3D11)
4. `audio_socket` → `demuxer` → `audio_decoder` (FFmpeg) → `audio_player` (WASAPI)
5. Win32 input events → `control_msg` serialization → `controller` thread → `control_socket`

### Threading Model

- **Main thread**: Win32 message loop, D3D11 rendering, input capture
- **Video thread**: reads from video socket, decodes, signals frame buffer
- **Audio thread**: reads from audio socket, decodes, writes to audio player
- **Controller thread**: drains control message queue, sends to device

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

- **master** branch is the working branch
- Feature branches: `feat/<desc>`, `fix/<desc>`, `refactor/<desc>`
- Never commit directly to main without going through a feature branch
- Use `git worktree` for isolated branch work when needed

## Reference Code

`reference/` contains read-only reference repositories:
- `reference/scrcpy/` — upstream scrcpy source (protocol and architecture reference)
- `reference/adb-impl/` — ADB protocol implementation reference
- `reference/d3d_video/` — D3D11 video rendering reference

**Never modify files under `reference/`.**

## Communication Protocol

所有与用户的问题和结果通过微信发送，不假设用户查看终端。

1. 启动后台 `wait`（无超时），永久阻塞等待消息
2. 收到消息 → 处理 → 通过微信回复 → 启动新的 `wait`
3. 所有问题和结果通过微信发送，不假设用户查看终端
