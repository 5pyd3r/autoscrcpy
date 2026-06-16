#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t keycode;
    uint32_t action; // 0=up, 1=down
    uint32_t repeat;
} keyboard_event_t;

bool keyboard_init(void);
bool keyboard_process_event(keyboard_event_t *event, uint8_t *msg, uint32_t *msg_size);
void keyboard_destroy(void);

#endif /* KEYBOARD_H */
