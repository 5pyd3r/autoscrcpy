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

void test_serialize_touch_down(void) {
    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    uint32_t args[10] = {0, 0xFFFFFFFF, 0xFFFFFFFF, 100, 200, 800, 600, 0xFFFF, 1, 1};
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT, args, buf, sizeof(buf));
    assert(len == 32);
    assert(buf[0] == CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT);
    assert(buf[1] == 0); /* action = DOWN */
    printf("test_serialize_touch_down passed\n");
}

void test_serialize_touch_up(void) {
    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    uint32_t args[10] = {1, 0xFFFFFFFF, 0xFFFFFFFF, 100, 200, 800, 600, 0, 1, 1};
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT, args, buf, sizeof(buf));
    assert(len == 32);
    assert(buf[1] == 1); /* action = UP */
    printf("test_serialize_touch_up passed\n");
}

void test_serialize_touch_move(void) {
    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    uint32_t args[10] = {2, 0xFFFFFFFF, 0xFFFFFFFF, 300, 400, 800, 600, 0, 0, 0};
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT, args, buf, sizeof(buf));
    assert(len == 32);
    assert(buf[1] == 2); /* action = MOVE */
    printf("test_serialize_touch_move passed\n");
}

void test_serialize_scroll(void) {
    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    int32_t args[7] = {100, 200, 800, 600, 0, 3, 0};
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT, args, buf, sizeof(buf));
    assert(len == 21);
    assert(buf[0] == CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT);
    printf("test_serialize_scroll passed\n");
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

void test_serialize_buffer_too_small(void) {
    uint8_t buf[1];
    uint32_t args[4] = {0, 66, 0, 0};
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_INJECT_KEYCODE, args, buf, sizeof(buf));
    assert(len == 0); /* Should fail */
    printf("test_serialize_buffer_too_small passed\n");
}

int main(void) {
    test_serialize_keycode();
    test_serialize_touch_down();
    test_serialize_touch_up();
    test_serialize_touch_move();
    test_serialize_scroll();
    test_serialize_display_power();
    test_serialize_collapse();
    test_serialize_buffer_too_small();
    printf("All control_msg tests passed!\n");
    return 0;
}
