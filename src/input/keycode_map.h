#ifndef KEYCODE_MAP_H
#define KEYCODE_MAP_H

#include <stdint.h>

uint32_t vk_to_android_keycode(uint32_t vk);
uint32_t get_android_metastate(void);

#endif /* KEYCODE_MAP_H */
