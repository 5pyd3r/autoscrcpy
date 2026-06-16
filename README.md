# AutoScrcpy

A Windows-native implementation of scrcpy with Win32+DirectX11 rendering and native ADB protocol.

## Features

- Screen mirroring from Android device
- Audio streaming
- Input control (keyboard, mouse, gamepad)
- Recording to MP4/MKV
- Clipboard synchronization
- No SDL dependency
- No ADB binary dependency

## Requirements

- Windows 10 or later
- Meson build system
- Ninja
- Clang

## Building

```bash
meson setup builddir
ninja -C builddir
```

## Usage

```bash
autoscrcpy [options]
```

### Options

- `-s, --serial <serial>` - Device serial number
- `-p, --port <port>` - ADB port (default: 5555)
- `-m, --max-size <size>` - Max video size
- `-b, --video-bit-rate <bps>` - Video bit rate
- `--video-codec <codec>` - Video codec (h264, h265, av1)
- `--audio-codec <codec>` - Audio codec (opus, aac, flac)
- `--no-control` - Disable control
- `--no-video` - Disable video
- `--no-audio` - Disable audio
- `-f, --fullscreen` - Start in fullscreen
- `--always-on-top` - Keep window on top
- `--turn-screen-off` - Turn screen off
- `--stay-awake` - Keep device awake
- `--show-touches` - Show touches
- `-r, --record <file>` - Record to file

## License

GPL-2.0
