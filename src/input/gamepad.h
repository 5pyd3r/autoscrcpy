#ifndef GAMEPAD_H
#define GAMEPAD_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t id;
    uint16_t type;
    uint16_t code;
    int32_t value;
} gamepad_event_t;

bool gamepad_init(void);
bool gamepad_process_event(gamepad_event_t *event, uint8_t *msg, uint32_t *msg_size);
void gamepad_destroy(void);

#endif /* GAMEPAD_H */
