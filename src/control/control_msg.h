#ifndef CONTROL_MSG_H
#define CONTROL_MSG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

enum control_msg_type {
    CONTROL_MSG_TYPE_INJECT_KEYCODE,
    CONTROL_MSG_TYPE_INJECT_TEXT,
    CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT,
    CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT,
    CONTROL_MSG_TYPE_BACK_OR_SCREEN_ON,
    CONTROL_MSG_TYPE_EXPAND_NOTIFICATION_PANEL,
    CONTROL_MSG_TYPE_EXPAND_SETTINGS_PANEL,
    CONTROL_MSG_TYPE_COLLAPSE_PANELS,
    CONTROL_MSG_TYPE_GET_CLIPBOARD,
    CONTROL_MSG_TYPE_SET_CLIPBOARD,
    CONTROL_MSG_TYPE_SET_DISPLAY_POWER,
    CONTROL_MSG_TYPE_ROTATE_DEVICE,
    CONTROL_MSG_TYPE_UHID_CREATE,
    CONTROL_MSG_TYPE_UHID_INPUT,
    CONTROL_MSG_TYPE_UHID_DESTROY,
    CONTROL_MSG_TYPE_OPEN_HARD_KEYBOARD_SETTINGS,
    CONTROL_MSG_TYPE_START_APP,
    CONTROL_MSG_TYPE_RESET_VIDEO,
    CONTROL_MSG_TYPE_CAMERA_SET_TORCH,
    CONTROL_MSG_TYPE_CAMERA_ZOOM_IN,
    CONTROL_MSG_TYPE_CAMERA_ZOOM_OUT,
    CONTROL_MSG_TYPE_RESIZE_DISPLAY,
};

#define CONTROL_MSG_MAX_SIZE (1 << 18) /* 256k */

typedef struct {
    int32_t x;
    int32_t y;
    uint16_t width;
    uint16_t height;
} control_position_t;

/* Serialize a control message into buf. Returns bytes written, or 0 on error. */
uint32_t control_msg_serialize(enum control_msg_type type,
                                const void *msg_data, uint8_t *buf, uint32_t buf_size);

#endif /* CONTROL_MSG_H */
