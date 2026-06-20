#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "../device/control_socket.h"
#include "../app/window.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    control_socket_t *sock;
    uint32_t device_width;
    uint32_t device_height;
    uint32_t window_width;
    uint32_t window_height;
    bool enabled;
} controller_t;

bool controller_init(controller_t *ctrl, control_socket_t *sock);
void controller_set_device_size(controller_t *ctrl, uint32_t w, uint32_t h);
void controller_set_window_size(controller_t *ctrl, uint32_t w, uint32_t h);
void controller_set_enabled(controller_t *ctrl, bool enabled);

/* Callback handlers — call these from window callbacks */
void controller_on_key_event(controller_t *ctrl, uint32_t vk, bool down);
void controller_on_mouse_event(controller_t *ctrl, int32_t x, int32_t y,
                               uint32_t buttons, uint32_t action);
void controller_on_wheel_event(controller_t *ctrl, int32_t x, int32_t y,
                               int32_t delta);

#endif /* CONTROLLER_H */
