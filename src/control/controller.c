#include "controller.h"
#include "control_msg.h"
#include "../input/keycode_map.h"
#include "../input/input_transform.h"
#include "../platform/log.h"
#include <windows.h>
#include <string.h>

bool controller_init(controller_t *ctrl, control_socket_t *sock) {
    ctrl->sock = sock;
    ctrl->device_width = 0;
    ctrl->device_height = 0;
    ctrl->window_width = 0;
    ctrl->window_height = 0;
    ctrl->enabled = false;
    return true;
}

void controller_set_device_size(controller_t *ctrl, uint32_t w, uint32_t h) {
    ctrl->device_width = w;
    ctrl->device_height = h;
}

void controller_set_window_size(controller_t *ctrl, uint32_t w, uint32_t h) {
    ctrl->window_width = w;
    ctrl->window_height = h;
}

void controller_set_enabled(controller_t *ctrl, bool enabled) {
    ctrl->enabled = enabled;
}

static void send_keycode(controller_t *ctrl, uint32_t kc, bool down) {
    uint8_t buf[64];
    uint32_t args[4] = {down ? 0 : 1, kc, 0, get_android_metastate()};
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_INJECT_KEYCODE, args, buf, sizeof(buf));
    if (len > 0) control_socket_send_msg(ctrl->sock, buf, len);
}

static void send_display_power(controller_t *ctrl, bool on) {
    uint8_t buf[64];
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_SET_DISPLAY_POWER, &on, buf, sizeof(buf));
    if (len > 0) control_socket_send_msg(ctrl->sock, buf, len);
}

void controller_on_key_event(controller_t *ctrl, uint32_t vk, bool down) {
    if (!ctrl->enabled) return;

    bool alt = GetKeyState(VK_MENU) & 0x8000;
    bool shift = GetKeyState(VK_SHIFT) & 0x8000;

    /* Alt + key shortcuts */
    if (alt && down) {
        switch (vk) {
            case 'P':
                send_keycode(ctrl, 26, true);
                send_keycode(ctrl, 26, false);
                log_info("Alt+P: Power");
                return;
            case 'O':
                send_display_power(ctrl, !shift);
                log_info("Alt+%sO: Display %s", shift ? "Shift+" : "", shift ? "off" : "on");
                return;
            case VK_UP:
                send_keycode(ctrl, 24, true);
                send_keycode(ctrl, 24, false);
                log_info("Alt+Up: Volume up");
                return;
            case VK_DOWN:
                send_keycode(ctrl, 25, true);
                send_keycode(ctrl, 25, false);
                log_info("Alt+Down: Volume down");
                return;
            case 'M':
                send_keycode(ctrl, 82, true);
                send_keycode(ctrl, 82, false);
                log_info("Alt+M: Menu");
                return;
            case 'A':
                send_keycode(ctrl, 187, true);
                send_keycode(ctrl, 187, false);
                log_info("Alt+A: App switch");
                return;
            case VK_BACK:
                {
                    uint8_t buf[64];
                    uint32_t action = 0;
                    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_BACK_OR_SCREEN_ON, &action, buf, sizeof(buf));
                    if (len > 0) control_socket_send_msg(ctrl->sock, buf, len);
                    action = 1;
                    len = control_msg_serialize(CONTROL_MSG_TYPE_BACK_OR_SCREEN_ON, &action, buf, sizeof(buf));
                    if (len > 0) control_socket_send_msg(ctrl->sock, buf, len);
                    log_info("Alt+Backspace: Back or screen on");
                }
                return;
        }
    }

    /* Regular key injection */
    if (!alt) {
        uint32_t kc = vk_to_android_keycode(vk);
        if (kc == 0) return;
        send_keycode(ctrl, kc, down);
    }
}

void controller_on_mouse_event(controller_t *ctrl, int32_t x, int32_t y,
                               uint32_t buttons, uint32_t action) {
    if (!ctrl->enabled || !ctrl->device_width || !ctrl->device_height) return;
    if (action == 2 && buttons == 0) return;

    int32_t dx, dy;
    input_transform_coords(x, y, &dx, &dy, ctrl->window_width, ctrl->window_height,
                           ctrl->device_width, ctrl->device_height);

    uint32_t android_action = (action == 1) ? 0 : (action == 0) ? 1 : 2;

    uint32_t action_button;
    if (buttons & 1) action_button = 1;
    else if (buttons & 2) action_button = 2;
    else action_button = 1;

    uint16_t pressure = (action == 0) ? 0 : 0xFFFF;
    uint32_t buttons_state = (action == 0) ? 0 : action_button;

    uint8_t buf[64];
    uint32_t args[10] = {
        android_action, 0xFFFFFFFF, 0xFFFFFFFF,
        (uint32_t)dx, (uint32_t)dy,
        (uint32_t)ctrl->device_width, (uint32_t)ctrl->device_height,
        pressure, action_button, buttons_state
    };
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT, args, buf, sizeof(buf));
    if (len > 0) control_socket_send_msg(ctrl->sock, buf, len);
}

void controller_on_wheel_event(controller_t *ctrl, int32_t x, int32_t y,
                               int32_t delta) {
    if (!ctrl->enabled || !ctrl->device_width || !ctrl->device_height) return;
    int32_t dx, dy;
    input_transform_coords(x, y, &dx, &dy, ctrl->window_width, ctrl->window_height,
                           ctrl->device_width, ctrl->device_height);
    uint8_t buf[64];
    int32_t args[7] = {dx, dy, (int32_t)ctrl->device_width, (int32_t)ctrl->device_height, 0, delta / WHEEL_DELTA, 0};
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT, args, buf, sizeof(buf));
    if (len > 0) control_socket_send_msg(ctrl->sock, buf, len);
}
