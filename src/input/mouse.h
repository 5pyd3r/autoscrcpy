#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t action; // 0=up, 1=down, 2=move
    uint32_t buttons;
} mouse_event_t;

bool mouse_init(void);
bool mouse_process_event(mouse_event_t *event, uint8_t *msg, uint32_t *msg_size);
void mouse_destroy(void);

#endif /* MOUSE_H */
