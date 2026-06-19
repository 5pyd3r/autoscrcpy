#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <windows.h>
#include "../src/input/keycode_map.h"

void test_common_keys(void) {
    assert(vk_to_android_keycode(VK_BACK) == 67);     /* KEYCODE_DEL */
    assert(vk_to_android_keycode(VK_RETURN) == 66);   /* KEYCODE_ENTER */
    assert(vk_to_android_keycode(VK_SPACE) == 62);    /* KEYCODE_SPACE */
    assert(vk_to_android_keycode(VK_ESCAPE) == 111);  /* KEYCODE_ESCAPE */
    assert(vk_to_android_keycode(VK_DELETE) == 112);   /* KEYCODE_FORWARD_DELETE (regression: was 67) */
    printf("test_common_keys passed\n");
}

void test_modifier_keys(void) {
    assert(vk_to_android_keycode(VK_LSHIFT) == 59);
    assert(vk_to_android_keycode(VK_RSHIFT) == 60);
    assert(vk_to_android_keycode(VK_LCONTROL) == 113);
    assert(vk_to_android_keycode(VK_RCONTROL) == 114);
    assert(vk_to_android_keycode(VK_LMENU) == 57);
    assert(vk_to_android_keycode(VK_RMENU) == 58);
    printf("test_modifier_keys passed\n");
}

void test_letter_keys(void) {
    assert(vk_to_android_keycode('A') == 29);
    assert(vk_to_android_keycode('Z') == 54);
    assert(vk_to_android_keycode('M') == 41);
    printf("test_letter_keys passed\n");
}

void test_number_keys(void) {
    assert(vk_to_android_keycode('0') == 7);
    assert(vk_to_android_keycode('9') == 16);
    assert(vk_to_android_keycode('5') == 12);
    printf("test_number_keys passed\n");
}

void test_arrow_keys(void) {
    assert(vk_to_android_keycode(VK_LEFT) == 21);
    assert(vk_to_android_keycode(VK_UP) == 19);
    assert(vk_to_android_keycode(VK_RIGHT) == 22);
    assert(vk_to_android_keycode(VK_DOWN) == 20);
    printf("test_arrow_keys passed\n");
}

void test_function_keys(void) {
    assert(vk_to_android_keycode(VK_F1) == 131);
    assert(vk_to_android_keycode(VK_F12) == 142);
    printf("test_function_keys passed\n");
}

void test_unknown_key(void) {
    assert(vk_to_android_keycode(0) == 0);
    assert(vk_to_android_keycode(0xFFFF) == 0);
    printf("test_unknown_key passed\n");
}

int main(void) {
    test_common_keys();
    test_modifier_keys();
    test_letter_keys();
    test_number_keys();
    test_arrow_keys();
    test_function_keys();
    test_unknown_key();
    printf("All keycode_map tests passed!\n");
    return 0;
}
