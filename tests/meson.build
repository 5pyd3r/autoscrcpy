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
