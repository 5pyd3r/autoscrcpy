# Control Feature Design

## Overview

Enable control input (keyboard, mouse) to scrcpy-server. All modules already exist, just need wiring.

## Data Flow

```
Win32 消息 → on_key_event/on_mouse_event
  → control_msg_serialize()
    → control_socket_send_msg()
      → ADB WRTE → scrcpy-server
```

## Changes

### 1. server.c

- Shell command: `control=true`
- Open control channel: `session_open_channel(conn, "localabstract:scrcpy")`
  - scrcpy-server expects: video → audio → control (顺序)
  - If audio=false, we still need to open the channel or server blocks
- Create control socketpair
- Reader thread: forward control WRTE to control socketpair (for device→host messages like clipboard)

### 2. application.c

- Set `control_sock.fd` from socketpair
- Keyboard/mouse events already call `control_socket_send_msg()` — just need valid fd
- Add control channel wait in application_run

### 3. server.c reader thread

- Add `ctrl_chan` pointer
- When WRTE arrives on control channel, forward to control socketpair
- Send OKAY for flow control

## scrcpy-server Channel Order

tunnel_forward=true expects channels in order:
1. video (localabstract:scrcpy)
2. audio (localabstract:scrcpy) — can skip if audio=false, but server still blocks
3. control (localabstract:scrcpy)

**Problem:** If audio=false but server expects audio channel, it blocks on accept.

**Solution:** Open all 3 channels regardless, then close unused ones after metadata exchange. Or set audio=false in server command so it doesn't expect audio channel.

## Testing

```bash
./builddir/tests/test_device.exe 192.168.13.197:5555
# Add control channel test
```

Manual: open app, press keys, verify input on device.
