#include "control_msg.h"
#include "../adb/binary.h"
#include "../platform/log.h"
#include <string.h>

uint32_t control_msg_serialize(enum control_msg_type type,
                                const void *msg_data, uint8_t *buf, uint32_t buf_size) {
    buf[0] = (uint8_t)type;

    switch (type) {
        case CONTROL_MSG_TYPE_INJECT_KEYCODE: {
            const uint32_t *args = (const uint32_t *)msg_data;
            if (buf_size < 14) return 0;
            buf[1] = (uint8_t)args[0];
            write32be(&buf[2], args[1]);
            write32be(&buf[6], args[2]);
            write32be(&buf[10], args[3]);
            return 14;
        }
        case CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT: {
            const uint32_t *args = (const uint32_t *)msg_data;
            if (buf_size < 32) return 0;
            buf[1] = (uint8_t)args[0];
            write64be(&buf[2], ((uint64_t)args[1] << 32) | args[2]);
            write32be(&buf[10], args[3]);
            write32be(&buf[14], args[4]);
            write16be(&buf[18], (uint16_t)args[5]);
            write16be(&buf[20], (uint16_t)args[6]);
            write16be(&buf[22], (uint16_t)args[7]);
            write32be(&buf[24], args[8]);
            write32be(&buf[28], args[9]);
            return 32;
        }
        case CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT: {
            const int32_t *args = (const int32_t *)msg_data;
            if (buf_size < 21) return 0;
            write32be(&buf[1], (uint32_t)args[0]);
            write32be(&buf[5], (uint32_t)args[1]);
            write16be(&buf[9], (uint16_t)args[2]);
            write16be(&buf[11], (uint16_t)args[3]);
            write16be(&buf[13], (uint16_t)args[4]);
            write16be(&buf[15], (uint16_t)args[5]);
            write32be(&buf[17], (uint32_t)args[6]);
            return 21;
        }
        case CONTROL_MSG_TYPE_SET_DISPLAY_POWER: {
            if (buf_size < 2) return 0;
            buf[1] = *(const bool *)msg_data ? 1 : 0;
            return 2;
        }
        case CONTROL_MSG_TYPE_BACK_OR_SCREEN_ON:
        case CONTROL_MSG_TYPE_EXPAND_NOTIFICATION_PANEL:
        case CONTROL_MSG_TYPE_EXPAND_SETTINGS_PANEL:
        case CONTROL_MSG_TYPE_COLLAPSE_PANELS:
        case CONTROL_MSG_TYPE_ROTATE_DEVICE:
        case CONTROL_MSG_TYPE_OPEN_HARD_KEYBOARD_SETTINGS:
        case CONTROL_MSG_TYPE_RESET_VIDEO:
            return 1;
        default:
            log_warn("Unknown control message type: %u", type);
            return 0;
    }
}
