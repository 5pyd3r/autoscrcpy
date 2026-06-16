#include "mouse.h"
#include "../control/control_msg.h"
#include "../platform/log.h"

bool mouse_init(void) {
    return true;
}

bool mouse_process_event(mouse_event_t *event, uint8_t *msg, uint32_t *msg_size) {
    msg[0] = CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT;
    *(int32_t *)(msg + 1) = event->x;
    *(int32_t *)(msg + 5) = event->y;
    *(uint32_t *)(msg + 9) = event->width;
    *(uint32_t *)(msg + 13) = event->height;
    *(uint32_t *)(msg + 17) = event->action;
    *(uint32_t *)(msg + 21) = event->buttons;
    *msg_size = 25;

    return true;
}

void mouse_destroy(void) {
}
