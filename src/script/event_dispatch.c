#include "event_dispatch.h"
#include "platform/log.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Main → Scheme: dispatch events from main thread to script engine  */
/* ------------------------------------------------------------------ */

void script_dispatch_key_event(script_engine_t *engine, uint32_t vk, bool down)
{
    script_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_EVENT_KEY;
    int32_t down_val = down ? 1 : 0;
    memcpy(msg.data, &vk, sizeof(uint32_t));
    memcpy(msg.data + sizeof(uint32_t), &down_val, sizeof(int32_t));
    msg.data_size = sizeof(uint32_t) + sizeof(int32_t);
    script_msg_queue_send(&engine->to_scheme, &msg);
}

void script_dispatch_mouse_event(script_engine_t *engine, int32_t x, int32_t y,
                                  uint32_t buttons, uint32_t action)
{
    script_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_EVENT_MOUSE;
    memcpy(msg.data, &x, sizeof(int32_t));
    memcpy(msg.data + sizeof(int32_t), &y, sizeof(int32_t));
    memcpy(msg.data + 2 * sizeof(int32_t), &buttons, sizeof(uint32_t));
    memcpy(msg.data + 3 * sizeof(int32_t), &action, sizeof(uint32_t));
    msg.data_size = 4 * sizeof(int32_t); /* 16 bytes */
    script_msg_queue_send(&engine->to_scheme, &msg);
}

void script_dispatch_frame_event(script_engine_t *engine, uint32_t width, uint32_t height)
{
    script_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_EVENT_FRAME;
    memcpy(msg.data, &width, sizeof(uint32_t));
    memcpy(msg.data + sizeof(uint32_t), &height, sizeof(uint32_t));
    msg.data_size = 2 * sizeof(uint32_t);
    script_msg_queue_send(&engine->to_scheme, &msg);
}

void script_dispatch_connect(script_engine_t *engine)
{
    script_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_EVENT_CONNECTED;
    msg.data_size = 0;
    script_msg_queue_send(&engine->to_scheme, &msg);
}

void script_dispatch_disconnect(script_engine_t *engine)
{
    script_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_EVENT_DISCONNECTED;
    msg.data_size = 0;
    script_msg_queue_send(&engine->to_scheme, &msg);
}

/* ------------------------------------------------------------------ */
/*  Scheme → Main: process messages from script engine                */
/* ------------------------------------------------------------------ */

int script_process_messages(script_engine_t *engine,
                            /* Command callbacks */
                            void (*on_inject_keycode)(int keycode, int down, void *ctx),
                            void (*on_inject_text)(const char *text, void *ctx),
                            void (*on_inject_touch)(int x, int y, int action, void *ctx),
                            void (*on_inject_scroll)(int x, int y, int dx, int dy, void *ctx),
                            void (*on_set_clipboard)(const char *text, void *ctx),
                            void (*on_expand_notification)(void *ctx),
                            void (*on_collapse_panels)(void *ctx),
                            void (*on_set_display_power)(int on, void *ctx),
                            void (*on_rotate_device)(void *ctx),
                            void (*on_start_app)(const char *package, void *ctx),
                            /* Query callbacks */
                            void (*on_query_device_info)(script_engine_t *engine, void *ctx),
                            void (*on_query_video_size)(script_engine_t *engine, void *ctx),
                            void (*on_query_window_size)(script_engine_t *engine, void *ctx),
                            void (*on_query_clipboard)(script_engine_t *engine, void *ctx),
                            void (*on_query_frame_capture)(script_engine_t *engine, void *ctx),
                            void *ctx)
{
    int count = 0;
    script_msg_t msg;

    while (script_msg_queue_try_recv(&engine->to_main, &msg)) {
        count++;
        switch (msg.type) {
        case MSG_INJECT_KEYCODE: {
            if (on_inject_keycode && msg.data_size >= 8) {
                int keycode, down;
                memcpy(&keycode, msg.data, sizeof(int));
                memcpy(&down, msg.data + sizeof(int), sizeof(int));
                on_inject_keycode(keycode, down, ctx);
            }
            break;
        }
        case MSG_INJECT_TEXT: {
            if (on_inject_text) {
                /* Ensure null-terminated */
                char buf[SCRIPT_MSG_MAX_DATA_SIZE];
                size_t len = msg.data_size < SCRIPT_MSG_MAX_DATA_SIZE ? msg.data_size : SCRIPT_MSG_MAX_DATA_SIZE - 1;
                memcpy(buf, msg.data, len);
                buf[len] = '\0';
                on_inject_text(buf, ctx);
            }
            break;
        }
        case MSG_INJECT_TOUCH: {
            if (on_inject_touch && msg.data_size >= 12) {
                int x, y, action;
                memcpy(&x, msg.data, sizeof(int));
                memcpy(&y, msg.data + sizeof(int), sizeof(int));
                memcpy(&action, msg.data + 2 * sizeof(int), sizeof(int));
                on_inject_touch(x, y, action, ctx);
            }
            break;
        }
        case MSG_INJECT_SCROLL: {
            if (on_inject_scroll && msg.data_size >= 16) {
                int x, y, dx, dy;
                memcpy(&x, msg.data, sizeof(int));
                memcpy(&y, msg.data + sizeof(int), sizeof(int));
                memcpy(&dx, msg.data + 2 * sizeof(int), sizeof(int));
                memcpy(&dy, msg.data + 3 * sizeof(int), sizeof(int));
                on_inject_scroll(x, y, dx, dy, ctx);
            }
            break;
        }
        case MSG_SET_CLIPBOARD: {
            if (on_set_clipboard) {
                char buf[SCRIPT_MSG_MAX_DATA_SIZE];
                size_t len = msg.data_size < SCRIPT_MSG_MAX_DATA_SIZE ? msg.data_size : SCRIPT_MSG_MAX_DATA_SIZE - 1;
                memcpy(buf, msg.data, len);
                buf[len] = '\0';
                on_set_clipboard(buf, ctx);
            }
            break;
        }
        case MSG_EXPAND_NOTIFICATION: {
            if (on_expand_notification) {
                on_expand_notification(ctx);
            }
            break;
        }
        case MSG_COLLAPSE_PANELS: {
            if (on_collapse_panels) {
                on_collapse_panels(ctx);
            }
            break;
        }
        case MSG_SET_DISPLAY_POWER: {
            if (on_set_display_power && msg.data_size >= sizeof(int)) {
                int on;
                memcpy(&on, msg.data, sizeof(int));
                on_set_display_power(on, ctx);
            }
            break;
        }
        case MSG_ROTATE_DEVICE: {
            if (on_rotate_device) {
                on_rotate_device(ctx);
            }
            break;
        }
        case MSG_START_APP: {
            if (on_start_app) {
                char buf[SCRIPT_MSG_MAX_DATA_SIZE];
                size_t len = msg.data_size < SCRIPT_MSG_MAX_DATA_SIZE ? msg.data_size : SCRIPT_MSG_MAX_DATA_SIZE - 1;
                memcpy(buf, msg.data, len);
                buf[len] = '\0';
                on_start_app(buf, ctx);
            }
            break;
        }
        /* Synchronous queries — callbacks send responses to engine->response_q */
        case MSG_QUERY_DEVICE_INFO: {
            if (on_query_device_info) on_query_device_info(engine, ctx);
            break;
        }
        case MSG_QUERY_VIDEO_SIZE: {
            if (on_query_video_size) on_query_video_size(engine, ctx);
            break;
        }
        case MSG_QUERY_WINDOW_SIZE: {
            if (on_query_window_size) on_query_window_size(engine, ctx);
            break;
        }
        case MSG_QUERY_CLIPBOARD: {
            if (on_query_clipboard) on_query_clipboard(engine, ctx);
            break;
        }
        case MSG_QUERY_FRAME_CAPTURE: {
            if (on_query_frame_capture) on_query_frame_capture(engine, ctx);
            break;
        }
        default:
            log_warn("script_process_messages: unknown message type %d", (int)msg.type);
            break;
        }
    }

    return count;
}
