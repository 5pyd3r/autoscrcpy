# AutoScrcpy Design Specification

## Overview

AutoScrcpy is a Windows-native implementation of scrcpy that provides complete Android screen mirroring and control functionality. It replaces SDL with Win32+DirectX11 for rendering and implements ADB protocol natively without depending on the adb binary.

## Goals

1. **Feature Parity**: Implement all scrcpy features (screen mirroring, audio, control, recording, etc.)
2. **No SDL Dependency**: Use Win32 window management and DirectX11 for rendering
3. **No ADB Binary**: Implement ADB protocol natively using code (based on adb-impl reference)
4. **Clean Build System**: Meson + Ninja + Clang with subprojects for dependencies
5. **Modular Design**: Layered architecture with loose coupling and single responsibility

## Architecture

### Module Structure

```
autoscrcpy/
├── src/
│   ├── adb/                    # ADB protocol implementation
│   │   ├── protocol.h/c        # ADB wire protocol
│   │   ├── transport.h/c       # Connection management
│   │   ├── session.h/c         # Session lifecycle
│   │   ├── crypto.h/c          # RSA key management
│   │   └── tls.h/c             # TLS handshake (via mbedtls)
│   │
│   ├── device/                 # Device communication
│   │   ├── server.h/c          # scrcpy-server management
│   │   ├── video_socket.h/c    # Video stream socket
│   │   ├── audio_socket.h/c    # Audio stream socket
│   │   ├── control_socket.h/c  # Control channel
│   │   └── device_msg.h/c      # Device message parsing
│   │
│   ├── decode/                 # Media decoding
│   │   ├── video_decoder.h/c   # H.264/H.265/AV1 decoding
│   │   ├── audio_decoder.h/c   # Opus/AAC/FLAC decoding
│   │   └── packet_queue.h/c    # Thread-safe packet queue
│   │
│   ├── render/                 # D3D11 rendering
│   │   ├── d3d_context.h/c     # D3D11 device/swapchain
│   │   ├── video_renderer.h/c  # NV12/BGRA rendering
│   │   ├── shader.h/c          # HLSL shader management
│   │   └── texture.h/c         # Texture upload/management
│   │
│   ├── input/                  # Input handling
│   │   ├── keyboard.h/c        # Keyboard input injection
│   │   ├── mouse.h/c           # Mouse input injection
│   │   ├── gamepad.h/c         # Gamepad input injection
│   │   └── hid/hid_*.h/c       # HID protocol implementation
│   │
│   ├── control/                # Control messages
│   │   ├── control_msg.h/c     # Control message serialization
│   │   ├── clipboard.h/c       # Clipboard sync
│   │   └── power.h/c           # Screen power control
│   │
│   ├── record/                 # Recording
│   │   ├── recorder.h/c        # MP4/MKV recording
│   │   └── muxer.h/c           # FFmpeg muxer wrapper
│   │
│   ├── app/                    # Application layer
│   │   ├── application.h/c     # Main application class
│   │   ├── window.h/c          # Win32 window management
│   │   ├── options.h/c         # Command-line options
│   │   └── cli.h/c             # CLI parsing
│   │
│   └── main.c                  # Entry point
│
├── server/                     # Android server (Java)
│   └── src/                    # scrcpy-server source
│
├── subprojects/                # Meson subprojects
│   ├── ffmpeg.wrap
│   └── mbedtls.wrap
│
└── meson.build                 # Root build file
```

### Layer Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
│  (Application, Window, CLI, Options)                         │
├─────────────────────────────────────────────────────────────┤
│                    Business Logic Layer                       │
│  (Device management, Recording, Clipboard sync)              │
├─────────────────────────────────────────────────────────────┤
│                    Processing Layer                           │
│  (Video/Audio decoding, Encoding, Muxing)                    │
├─────────────────────────────────────────────────────────────┤
│                    Protocol Layer                             │
│  (ADB protocol, Device protocol, Control messages)           │
├─────────────────────────────────────────────────────────────┤
│                    Platform Layer                             │
│  (Win32 APIs, D3D11, Socket I/O, Threading)                  │
└─────────────────────────────────────────────────────────────┘
```

## Module Details

### 1. ADB Module (`src/adb/`)

Implements the ADB protocol natively without depending on the adb binary.

**Key Components:**
- `protocol.h/c`: ADB wire protocol (message format, command IDs)
- `transport.h/c`: Connection management, event loop, ring buffers
- `session.h/c`: Session lifecycle (connect, auth, TLS negotiation)
- `crypto.h/c`: RSA key management for ADB authentication
- `tls.h/c`: TLS handshake using mbedtls

**Reference**: adb-impl implementation

**Public API:**
```c
// Initialize ADB subsystem
bool adb_init(void);

// Connect to device
adb_connection_t *adb_connect(const char *host, uint16_t port);

// Execute shell command
bool adb_shell(adb_connection_t *conn, const char *command);

// Push file to device
bool adb_push(adb_connection_t *conn, const char *local, const char *remote);

// Forward port
bool adb_forward(adb_connection_t *conn, uint16_t local_port, const char *remote_spec);

// Disconnect
void adb_disconnect(adb_connection_t *conn);

// Cleanup
void adb_destroy(void);
```

### 2. Device Module (`src/device/`)

Manages communication with the scrcpy-server running on the Android device.

**Key Components:**
- `server.h/c`: Push and start scrcpy-server.jar
- `video_socket.h/c`: Video stream socket management
- `audio_socket.h/c`: Audio stream socket management
- `control_socket.h/c`: Control channel for input injection
- `device_msg.h/c`: Device message parsing (clipboard, etc.)

**Protocol:**
- Video stream: Raw H.264/H.265/AV1 packets
- Audio stream: Raw Opus/AAC/FLAC packets
- Control channel: Binary control messages

### 3. Decode Module (`src/decode/`)

Handles video and audio decoding using FFmpeg.

**Key Components:**
- `video_decoder.h/c`: H.264/H.265/AV1 decoding
- `audio_decoder.h/c`: Opus/AAC/FLAC decoding
- `packet_queue.h/c`: Thread-safe packet queue for producer-consumer pattern

**Design:**
- Uses FFmpeg's hardware acceleration when available
- Supports multiple codec types
- Thread-safe packet queuing for smooth playback

### 4. Render Module (`src/render/`)

D3D11-based video rendering.

**Key Components:**
- `d3d_context.h/c`: D3D11 device and swapchain management
- `video_renderer.h/c`: NV12/BGRA texture rendering
- `shader.h/c`: HLSL shader compilation and management
- `texture.h/c`: Texture creation and upload

**Reference**: d3d_video project

**Rendering Pipeline:**
1. Decode video frame to NV12/BGRA
2. Upload to D3D11 texture
3. Render using vertex/pixel shaders
4. Present to swapchain

### 5. Input Module (`src/input/`)

Handles keyboard, mouse, and gamepad input injection.

**Key Components:**
- `keyboard.h/c`: Keyboard input injection
- `mouse.h/c`: Mouse input injection
- `gamepad.h/c`: Gamepad input injection
- `hid/hid_*.h/c`: HID protocol implementation for UHID/AOA modes

**Input Modes:**
- SDK mode: Android KeyEvent/MotionEvent
- UHID mode: USB HID protocol
- AOA mode: Android Open Accessory protocol

### 6. Control Module (`src/control/`)

Manages control messages and clipboard synchronization.

**Key Components:**
- `control_msg.h/c`: Control message serialization
- `clipboard.h/c`: Clipboard synchronization
- `power.h/c`: Screen power control

### 7. Record Module (`src/record/`)

Handles recording of video/audio streams.

**Key Components:**
- `recorder.h/c`: MP4/MKV recording management
- `muxer.h/c`: FFmpeg muxer wrapper

### 8. Application Module (`src/app/`)

Main application logic and window management.

**Key Components:**
- `application.h/c`: Main application class
- `window.h/c`: Win32 window management
- `options.h/c`: Command-line options
- `cli.h/c`: CLI parsing

## Dependencies

### Required Dependencies
1. **FFmpeg**: Video/audio decoding and muxing
   - libavcodec, libavformat, libavutil, libswscale
   - Managed via meson subproject

2. **mbedtls**: TLS for ADB protocol
   - Managed via meson subproject

3. **Win32 APIs**: Window management, input handling
   - User32, Kernel32, Gdi32, etc.

4. **DirectX 11**: Video rendering
   - D3D11, DXGI

### Optional Dependencies
1. **spdlog**: Logging (can be replaced with custom logger)

## Build System

### Meson Configuration
```meson
project('autoscrcpy', 'c',
    meson_version: '>=1.3.0',
    default_options: [
        'c_std=c11',
        'buildtype=debugoptimized',
    ]
)

# Dependencies
ffmpeg = dependency('libavcodec', 'libavformat', 'libavutil', 'libswscale')
mbedtls = dependency('mbedtls')

# Windows libraries
winlibs = [
    meson.get_compiler('c').find_library('d3d11'),
    meson.get_compiler('c').find_library('dxgi'),
    meson.get_compiler('c').find_library('user32'),
    # ... etc
]
```

### Subprojects
- `ffmpeg.wrap`: FFmpeg meson port
- `mbedtls.wrap`: mbedtls library

## Data Flow

### Video Pipeline
```
Android Device → scrcpy-server → Video Socket → Decoder → Texture Upload → D3D11 Renderer → Screen
```

### Audio Pipeline
```
Android Device → scrcpy-server → Audio Socket → Decoder → Audio Player → Speakers
```

### Input Pipeline
```
Keyboard/Mouse → Win32 Messages → Input Handler → Control Message → Control Socket → scrcpy-server → Android Device
```

## Error Handling

### Strategy
- Use error codes for recoverable errors
- Use assertions for programming errors
- Log all errors with context
- Graceful degradation when possible

### Error Types
1. **Connection Errors**: ADB connection failures, device disconnection
2. **Protocol Errors**: Invalid messages, authentication failures
3. **Decoding Errors**: Unsupported codecs, corrupted frames
4. **Rendering Errors**: D3D11 failures, texture upload errors
5. **Input Errors**: Invalid input, injection failures

## Testing Strategy

### Unit Tests
- Test each module independently
- Mock external dependencies
- Test error handling paths

### Integration Tests
- Test module interactions
- Test full pipeline (video/audio/input)
- Test recording functionality

### Platform Tests
- Test on different Windows versions
- Test with different Android versions
- Test with different devices

## Performance Considerations

### Video Rendering
- Use hardware-accelerated decoding when available
- Use D3D11 texture streaming for efficient updates
- Implement frame dropping for smooth playback

### Memory Management
- Use ring buffers for streaming data
- Implement proper cleanup on errors
- Avoid memory leaks in long-running sessions

### Threading
- Use producer-consumer pattern for pipelines
- Minimize lock contention
- Use appropriate thread priorities

## Security Considerations

### ADB Authentication
- RSA key management
- TLS encryption for ADB protocol
- Secure key storage

### Input Injection
- Validate input before injection
- Prevent unauthorized input injection
- Handle malicious input gracefully

## Future Extensions

### Planned Features
1. USB ADB support
2. Multiple device support
3. Virtual display support
4. Camera mirroring
5. File transfer UI

### Extension Points
- Plugin system for custom renderers
- Custom input handlers
- Custom recording formats
- Custom control messages
