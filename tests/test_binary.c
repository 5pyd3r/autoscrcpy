#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include "../src/adb/binary.h"

void test_read_write16be(void) {
    uint8_t buf[2];

    write16be(buf, 0x0000);
    assert(buf[0] == 0x00 && buf[1] == 0x00);
    assert(read16be(buf) == 0x0000);

    write16be(buf, 0x0102);
    assert(buf[0] == 0x01 && buf[1] == 0x02);
    assert(read16be(buf) == 0x0102);

    write16be(buf, 0xFFFF);
    assert(buf[0] == 0xFF && buf[1] == 0xFF);
    assert(read16be(buf) == 0xFFFF);

    printf("test_read_write16be passed\n");
}

void test_read_write32be(void) {
    uint8_t buf[4];

    write32be(buf, 0x00000000);
    assert(buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x00 && buf[3] == 0x00);
    assert(read32be(buf) == 0x00000000);

    write32be(buf, 0x01020304);
    assert(buf[0] == 0x01 && buf[1] == 0x02 && buf[2] == 0x03 && buf[3] == 0x04);
    assert(read32be(buf) == 0x01020304);

    write32be(buf, 0xFFFFFFFF);
    assert(buf[0] == 0xFF && buf[1] == 0xFF && buf[2] == 0xFF && buf[3] == 0xFF);
    assert(read32be(buf) == 0xFFFFFFFF);

    printf("test_read_write32be passed\n");
}

void test_read_write64be(void) {
    uint8_t buf[8];

    write64be(buf, UINT64_C(0x0000000000000000));
    assert(read64be(buf) == UINT64_C(0x0000000000000000));

    write64be(buf, UINT64_C(0x0102030405060708));
    assert(buf[0] == 0x01 && buf[7] == 0x08);
    assert(read64be(buf) == UINT64_C(0x0102030405060708));

    write64be(buf, UINT64_C(0xFFFFFFFFFFFFFFFF));
    assert(read64be(buf) == UINT64_C(0xFFFFFFFFFFFFFFFF));

    printf("test_read_write64be passed\n");
}

void test_read_write32le(void) {
    uint8_t buf[4];

    write32le(buf, 0x01020304);
    assert(buf[0] == 0x04 && buf[1] == 0x03 && buf[2] == 0x02 && buf[3] == 0x01);
    assert(read32le(buf) == 0x01020304);

    write32le(buf, 0x00000000);
    assert(read32le(buf) == 0x00000000);

    write32le(buf, 0xFFFFFFFF);
    assert(read32le(buf) == 0xFFFFFFFF);

    printf("test_read_write32le passed\n");
}

void test_float_to_u16fp(void) {
    assert(float_to_u16fp(0.0f) == 0);
    assert(float_to_u16fp(1.0f) == 0xFFFF);
    assert(float_to_u16fp(0.5f) == 0x7FFF);
    assert(float_to_u16fp(-1.0f) == 0);     /* clamped */
    assert(float_to_u16fp(2.0f) == 0xFFFF); /* clamped */

    printf("test_float_to_u16fp passed\n");
}

void test_float_to_i16fp(void) {
    assert(float_to_i16fp(0.0f) == 0);
    assert(float_to_i16fp(1.0f) == 0x7FFF);
    assert(float_to_i16fp(-1.0f) == -0x7FFF);
    assert(float_to_i16fp(-2.0f) == -0x7FFF); /* clamped */
    assert(float_to_i16fp(2.0f) == 0x7FFF);   /* clamped */

    printf("test_float_to_i16fp passed\n");
}

int main(void) {
    test_read_write16be();
    test_read_write32be();
    test_read_write64be();
    test_read_write32le();
    test_float_to_u16fp();
    test_float_to_i16fp();
    printf("All binary tests passed!\n");
    return 0;
}
