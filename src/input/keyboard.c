#include "keyboard.h"
#include "../control/control_msg.h"
#include "../platform/log.h"

bool keyboard_init(void) {
    // Nothing to initialize
    return true;
}

bool keyboard_process_event(keyboard_event_t *event, uint8_t *msg, uint32_t *msg_size) {
    // Build control message
    msg[0] = CONTROL_MSG_TYPE_INJECT_KEYCODE;
    msg[1] = event->action;
    *(uint32_t *)(msg + 2) = event->keycode;
    *(uint32_t *)(msg + 6) = event->repeat;
    *msg_size = 10;

    return true;
}

void keyboard_destroy(void) {
    // Nothing to clean up
}
