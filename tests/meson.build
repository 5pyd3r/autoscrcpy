# ADB sources needed for tests
test_adb_src = files(
    '../src/adb/adb.c',
    '../src/adb/protocol.c',
    '../src/adb/transport.c',
    '../src/adb/session.c',
    '../src/adb/crypto.c',
    '../src/adb/tls.c',
    '../src/platform/log.c',
    '../src/platform/thread.c',
)

# Existing tests
test_adb = executable('test_adb', 'test_adb.c',
    test_adb_src,
    dependencies: [mbedtls_dep] + winlibs,
)
test('ADB test', test_adb)

test_protocol = executable('test_protocol', 'test_protocol.c',
    test_adb_src,
    dependencies: [mbedtls_dep] + winlibs,
)
test('Protocol test', test_protocol)

test_control_msg = executable('test_control_msg', 'test_control_msg.c',
    '../src/control/control_msg.c',
    '../src/platform/log.c',
    dependencies: winlibs,
)
test('Control message test', test_control_msg)

# New unit tests (no external dependencies)
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

test_keycode_map = executable('test_keycode_map', 'test_keycode_map.c',
    '../src/input/keycode_map.c',
    '../src/platform/log.c',
    dependencies: winlibs,
)
test('Keycode map test', test_keycode_map)

# Integration tests (with dependencies)
test_video_decoder = executable('test_video_decoder', 'test_video_decoder.c',
    '../src/decode/video_decoder.c',
    '../src/platform/log.c',
    dependencies: [libavcodec_dep, libavutil_dep] + winlibs,
)
test('Video decoder test', test_video_decoder)
