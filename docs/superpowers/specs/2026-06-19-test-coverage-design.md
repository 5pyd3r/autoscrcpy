# Test Coverage Design

## Overview

Extend the existing test suite (`test_adb`, `test_protocol`, `test_control_msg`) with comprehensive unit tests for pure-logic modules and integration tests for modules with external dependencies.

Style: `assert()` + `printf()`, consistent with existing tests.

## Unit Tests (No External Dependencies)

### 1. `test_binary.c` — Byte Manipulation Functions

Module: `src/adb/binary.h`

```
test_read_write16be:
  - write16be(0x0000) → {0x00, 0x00}
  - write16be(0x0102) → {0x01, 0x02}
  - write16be(0xFFFF) → {0xFF, 0xFF}
  - read16be round-trip for each value

test_read_write32be:
  - write32be(0x00000000) → {0x00, 0x00, 0x00, 0x00}
  - write32be(0x01020304) → {0x01, 0x02, 0x03, 0x04}
  - write32be(0xFFFFFFFF) → {0xFF, 0xFF, 0xFF, 0xFF}
  - read32be round-trip for each value

test_read_write64be:
  - write64be(0x0000000000000000)
  - write64be(0x0102030405060708)
  - write64be(0xFFFFFFFFFFFFFFFF)
  - read64be round-trip

test_read_write32le:
  - write32le(0x01020304) → {0x04, 0x03, 0x02, 0x01}
  - read32le round-trip

test_float_to_u16fp:
  - float_to_u16fp(0.0f) == 0
  - float_to_u16fp(1.0f) == 0xFFFF
  - float_to_u16fp(0.5f) ≈ 0x7FFF
  - float_to_u16fp(-1.0f) == 0 (clamped)
  - float_to_u16fp(2.0f) == 0xFFFF (clamped)

test_float_to_i16fp:
  - float_to_i16fp(0.0f) == 0
  - float_to_i16fp(1.0f) == 0x7FFF
  - float_to_i16fp(-1.0f) == -0x7FFF
  - float_to_i16fp(-2.0f) == -0x7FFF (clamped)
```

### 2. `test_input_transform.c` — Coordinate Transformation

Module: `src/input/input_transform.c`

```
test_identity_transform:
  - Window 800x600, Device 800x600
  - (400, 300) → (400, 300)

test_letterbox (window wider than video):
  - Window 1600x600, Device 800x600 (aspect 4:3 vs 16:9)
  - Center point should map to device center
  - Left edge of video area → (0, y)
  - Right edge of video area → (dev_w, y)

test_pillarbox (window taller than video):
  - Window 600x800, Device 800x600
  - Center point should map to device center
  - Top edge of video area → (x, 0)
  - Bottom edge of video area → (x, dev_h)

test_clipping:
  - Click outside video area (in black bar) → clamped to edge
  - Click at (0, 0) → (0, 0)

test_zero_dimensions:
  - win_w=0 → (0, 0)
  - dev_h=0 → (0, 0)
```

### 3. `test_control_msg.c` — Extend Existing

Module: `src/control/control_msg.c`

```
test_serialize_touch_down:
  - args = {0, 0xFFFFFFFF, 0xFFFFFFFF, 100, 200, 800, 600, 0xFFFF, 1, 1}
  - Verify buf[0] == INJECT_TOUCH_EVENT
  - Verify total length == 32

test_serialize_touch_up:
  - args = {1, 0xFFFFFFFF, 0xFFFFFFFF, 100, 200, 800, 600, 0, 1, 1}
  - Verify pressure == 0

test_serialize_touch_move:
  - args = {2, 0xFFFFFFFF, 0xFFFFFFFF, 300, 400, 800, 600, 0, 0, 0}
  - Verify action == 2

test_serialize_scroll:
  - args = {100, 200, 800, 600, 0, 3, 0}
  - Verify buf[0] == INJECT_SCROLL_EVENT
  - Verify total length == 21

test_serialize_buffer_too_small:
  - buf_size = 1 for keycode → returns 0
  - buf_size = 1 for touch → returns 0
```

## Integration Tests (With Dependencies)

### 4. `test_video_decoder.c` — H.264 Decoding

Module: `src/decode/video_decoder.c`, dependency: FFmpeg

```
test_decoder_create_destroy:
  - video_decoder_create() returns non-NULL
  - video_decoder_destroy(NULL) is safe

test_decoder_init:
  - H.264 codec (0x68323634) → init succeeds
  - Unknown codec (0x00000000) → init fails
  - avcodec_open2 failure → returns false

test_decode_sps_pps:
  - Feed minimal H.264 SPS+PPS NAL units
  - Verify no crash, frame.data == NULL (no picture yet)

test_decode_idr_frame:
  - Feed SPS + PPS + IDR frame
  - Verify frame.data != NULL
  - Verify frame.width and frame.height > 0
  - Verify frame.format == 0 (NV12)
  - Verify NV12 buffer size == w*h + w*(h/2)

test_frame_to_nv12:
  - Decode known frame
  - Verify Y plane values in valid range [16, 235]
  - Verify UV plane values in valid range [16, 240]
```

### 5. `test_keycode_map.c` — Key Mapping

Module: `src/input/keycode_map.c`, dependency: Windows.h

```
test_common_keys:
  - VK_BACK → 67
  - VK_RETURN → 66
  - VK_SPACE → 62
  - VK_ESCAPE → 111
  - VK_DELETE → 112 (regression: was 67)

test_letter_keys:
  - 'A' → 29, 'Z' → 54

test_number_keys:
  - '0' → 7, '9' → 16

test_unknown_key:
  - 0 → 0
  - 0xFFFF → 0
```

## Build Integration

Update `tests/meson.build`:

```meson
# Unit tests (no external dependencies)
test_binary = executable('test_binary', 'test_binary.c',
    '../src/platform/log.c',
    dependencies: winlibs,
)
test('Binary test', test_binary)

test_input_transform = executable('test_input_transform', 'test_input_transform.c',
    '../src/input/input_transform.c',
    '../src/platform/log.c',
    dependencies: winlibs,
)
test('Input transform test', test_input_transform)

# Integration tests (with dependencies)
test_video_decoder = executable('test_video_decoder', 'test_video_decoder.c',
    '../src/decode/video_decoder.c',
    '../src/platform/log.c',
    dependencies: [libavcodec_dep, libavutil_dep] + winlibs,
)
test('Video decoder test', test_video_decoder)

test_keycode_map = executable('test_keycode_map', 'test_keycode_map.c',
    '../src/input/keycode_map.c',
    '../src/platform/log.c',
    dependencies: winlibs,
)
test('Keycode map test', test_keycode_map)
```

## Test Execution

```bash
# Run all tests
meson test -C builddir

# Run specific test
meson test -C builddir --test-name "Binary test"

# Run with verbose output
meson test -C builddir -v
```

## Files to Create

1. `tests/test_binary.c`
2. `tests/test_input_transform.c`
3. `tests/test_video_decoder.c`
4. `tests/test_keycode_map.c`
5. `tests/meson.build` (update)

## Files to Extend

1. `tests/test_control_msg.c` — add touch/scroll test cases
