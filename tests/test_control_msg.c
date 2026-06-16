#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../src/control/control_msg.h"

void test_serialize_keycode(void) {
    uint8_t buf[64];
    uint32_t args[4] = {0, 66, 0, 0};
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_INJECT_KEYCODE, args, buf, sizeof(buf));
    assert(len == 14);
    assert(buf[0] == CONTROL_MSG_TYPE_INJECT_KEYCODE);
    assert(buf[1] == 0);
    printf("test_serialize_keycode passed\n");
}

void test_serialize_display_power(void) {
    uint8_t buf[64];
    bool on = true;
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_SET_DISPLAY_POWER, &on, buf, sizeof(buf));
    assert(len == 2);
    assert(buf[0] == CONTROL_MSG_TYPE_SET_DISPLAY_POWER);
    assert(buf[1] == 1);
    printf("test_serialize_display_power passed\n");
}

void test_serialize_collapse(void) {
    uint8_t buf[64];
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_COLLAPSE_PANELS, NULL, buf, sizeof(buf));
    assert(len == 1);
    assert(buf[0] == CONTROL_MSG_TYPE_COLLAPSE_PANELS);
    printf("test_serialize_collapse passed\n");
}

int main(void) {
    test_serialize_keycode();
    test_serialize_display_power();
    test_serialize_collapse();
    return 0;
}
