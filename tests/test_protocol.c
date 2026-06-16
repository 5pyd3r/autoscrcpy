#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../src/adb/protocol.h"

void test_adb_checksum(void) {
    uint8_t data[] = {1, 2, 3, 4, 5};
    uint32_t sum = adb_checksum(data, 5);
    assert(sum == 15);
    printf("test_adb_checksum passed\n");
}

void test_adb_message(void) {
    adb_message_t msg;
    msg.command = ADB_CNXN;
    msg.arg0 = ADB_VERSION;
    msg.arg1 = ADB_MAX_PAYLOAD;
    msg.data_length = 0;
    msg.data_check = 0;
    msg.magic = ADB_CNXN ^ 0xffffffff;

    assert(msg.magic == (ADB_CNXN ^ 0xffffffff));
    printf("test_adb_message passed\n");
}

int main(void) {
    test_adb_checksum();
    test_adb_message();
    return 0;
}
