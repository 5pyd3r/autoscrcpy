#include "keycode_map.h"
#include <windows.h>

static const struct {
    uint32_t vk;
    uint32_t android_keycode;
} vk_map[] = {
    {VK_BACK, 67}, {VK_TAB, 61}, {VK_RETURN, 66},
    {VK_LSHIFT, 59}, {VK_RSHIFT, 60}, {VK_LCONTROL, 113}, {VK_RCONTROL, 114},
    {VK_LMENU, 57}, {VK_RMENU, 58}, {VK_CAPITAL, 115}, {VK_ESCAPE, 111},
    {VK_SPACE, 62}, {VK_PRIOR, 92}, {VK_NEXT, 93}, {VK_END, 123}, {VK_HOME, 122},
    {VK_LEFT, 21}, {VK_UP, 19}, {VK_RIGHT, 22}, {VK_DOWN, 20},
    {VK_INSERT, 124}, {VK_DELETE, 67},
    {'0', 7}, {'1', 8}, {'2', 9}, {'3', 10}, {'4', 11},
    {'5', 12}, {'6', 13}, {'7', 14}, {'8', 15}, {'9', 16},
    {'A', 29}, {'B', 30}, {'C', 31}, {'D', 32}, {'E', 33}, {'F', 34},
    {'G', 35}, {'H', 36}, {'I', 37}, {'J', 38}, {'K', 39}, {'L', 40},
    {'M', 41}, {'N', 42}, {'O', 43}, {'P', 44}, {'Q', 45}, {'R', 46},
    {'S', 47}, {'T', 48}, {'U', 49}, {'V', 50}, {'W', 51}, {'X', 52},
    {'Y', 53}, {'Z', 54},
    {VK_LWIN, 117}, {VK_RWIN, 118},
    {VK_F1, 131}, {VK_F2, 132}, {VK_F3, 133}, {VK_F4, 134}, {VK_F5, 135},
    {VK_F6, 136}, {VK_F7, 137}, {VK_F8, 138}, {VK_F9, 139}, {VK_F10, 140},
    {VK_F11, 141}, {VK_F12, 142},
    {VK_NUMLOCK, 143}, {VK_SCROLL, 116},
};

#define VK_MAP_SIZE (sizeof(vk_map) / sizeof(vk_map[0]))

uint32_t vk_to_android_keycode(uint32_t vk) {
    for (size_t i = 0; i < VK_MAP_SIZE; i++) {
        if (vk_map[i].vk == vk) return vk_map[i].android_keycode;
    }
    return 0;
}

uint32_t get_android_metastate(void) {
    uint32_t meta = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) meta |= 0x01;
    if (GetKeyState(VK_CONTROL) & 0x8000) meta |= 0x1000;
    if (GetKeyState(VK_MENU) & 0x8000) meta |= 0x02;
    if (GetKeyState(VK_LWIN) & 0x8000) meta |= 0x10000;
    return meta;
}
