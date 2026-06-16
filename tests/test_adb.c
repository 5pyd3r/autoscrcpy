#include <stdio.h>
#include <assert.h>
#include "../src/adb/adb.h"

void test_adb_init(void) {
    assert(adb_init() == true);
    adb_destroy();
    printf("test_adb_init passed\n");
}

void test_adb_connect(void) {
    // This test requires a running ADB server
    // Skip in CI
    printf("test_adb_connect skipped (requires ADB server)\n");
}

int main(void) {
    test_adb_init();
    test_adb_connect();
    return 0;
}
