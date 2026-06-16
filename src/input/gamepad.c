#include "gamepad.h"
#include "../control/control_msg.h"
#include "../platform/log.h"

bool gamepad_init(void) {
    return true;
}

bool gamepad_process_event(gamepad_event_t *event, uint8_t *msg, uint32_t *msg_size) {
    msg[0] = CONTROL_MSG_TYPE_INJECT_UHID_INPUT;
    *(uint16_t *)(msg + 1) = event->id;
    *(uint16_t *)(msg + 3) = event->type;
    *(uint16_t *)(msg + 5) = event->code;
    *(int32_t *)(msg + 7) = event->value;
    *msg_size = 11;

    return true;
}

void gamepad_destroy(void) {
}
