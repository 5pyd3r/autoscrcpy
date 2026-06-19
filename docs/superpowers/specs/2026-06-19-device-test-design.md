# Device-Dependent Test Design

## Test Categories

### Automatic Tests (no device, CI safe)
Run: `meson test -C builddir`

| File | Cases | Module |
|------|-------|--------|
| test_binary.c | 6 | binary.h |
| test_input_transform.c | 6 | input_transform.c |
| test_keycode_map.c | 7 | keycode_map.c |
| test_control_msg.c | 8 | control_msg.c |
| test_crypto.c | 7 | crypto.c |
| test_protocol.c | 2 | protocol.c |
| test_adb.c | 2 | adb.c |

### Manual Tests (device required)
Run: `./builddir/tests/test_adb_connect.exe <serial>`
Serial format: `host:port` (e.g., `192.168.13.197:5555`)

| File | Cases | Module |
|------|-------|--------|
| test_adb_connect.c | 3 | adb.c, tls.c |
| test_server.c | 3 | server.c |
| test_video_pipeline.c | 3 | video_decoder.c + device |

Serial provided via argv[1]. No default. Missing → error + exit(1).

## Test Cases

### test_crypto.c (automatic)
```
test_load_key:     load ~/.android/adbkey → 0
test_load_bad:     load nonexistent → -1
test_sign_token:   sign 20B token → sig_len > 0
test_sign_no_init: sign before load → -1
test_public_key:   get key → len == 260
test_public_no_init: get key before load → -1
test_free:         free then sign → -1
```

### test_adb_connect.c (manual, argv[1]=serial)
```
test_tcp_connect:     TCP connect → fd valid
test_adb_handshake:   CNXN+TLS → state CONNECTED, banner contains "device::"
test_adb_disconnect:  disconnect → fd closed
```

### test_server.c (manual, argv[1]=serial)
```
test_push_jar:        push scrcpy-server.jar → success
test_start_server:    start with video=true → video channel open
test_metadata:        read codec_id/dimensions → valid H.264, w>0, h>0
```

### test_video_pipeline.c (manual, argv[1]=serial)
```
test_decode_frames:   recv 10 chunks → at least 1 frame decoded
test_nv12_size:       frame size == w*h + w*(h/2)
test_multi_frame:     recv 100 chunks → multiple frames, consistent size
```

## Files to Create
1. tests/test_crypto.c
2. tests/test_adb_connect.c
3. tests/test_server.c
4. tests/test_video_pipeline.c
5. tests/meson.build (update)
