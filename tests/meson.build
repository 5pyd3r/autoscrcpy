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

# Server sources needed for device tests
test_server_src = files(
    '../src/device/server.c',
    '../src/device/video_socket.c',
    '../src/device/audio_socket.c',
    '../src/device/control_socket.c',
    '../src/device/device_msg.c',
    '../src/device/demuxer.c',
)

# ============================================================
# Automatic tests (no device required)
# ============================================================

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

test_crypto = executable('test_crypto', 'test_crypto.c',
    test_adb_src,
    dependencies: [mbedtls_dep] + winlibs,
)
test('Crypto test', test_crypto)

test_video_decoder = executable('test_video_decoder', 'test_video_decoder.c',
    '../src/decode/video_decoder.c',
    '../src/platform/log.c',
    dependencies: [libavcodec_dep, libavutil_dep] + winlibs,
)
test('Video decoder test', test_video_decoder)

# ============================================================
# Manual tests (device required, run directly with serial arg)
# ============================================================

test_adb_connect = executable('test_adb_connect', 'test_adb_connect.c',
    test_adb_src,
    dependencies: [mbedtls_dep] + winlibs,
)

test_server = executable('test_server', 'test_server.c',
    test_adb_src, test_server_src,
    dependencies: [mbedtls_dep, libavcodec_dep, libavutil_dep] + winlibs,
)

test_video_pipeline = executable('test_video_pipeline', 'test_video_pipeline.c',
    test_adb_src, test_server_src,
    '../src/decode/video_decoder.c',
    dependencies: [mbedtls_dep, libavcodec_dep, libavutil_dep] + winlibs,
)

test_device = executable('test_device', 'test_device.c',
    test_adb_src, test_server_src,
    '../src/decode/video_decoder.c',
    dependencies: [mbedtls_dep, libavcodec_dep, libavutil_dep] + winlibs,
)
