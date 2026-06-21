#ifndef SCRIPT_EVENT_DISPATCH_H
#define SCRIPT_EVENT_DISPATCH_H

#include "engine.h"
#include <stdint.h>
#include <stdbool.h>

/* Dispatch events from main thread to script engine */
void script_dispatch_key_event(script_engine_t *engine, uint32_t vk, bool down);
void script_dispatch_mouse_event(script_engine_t *engine, int32_t x, int32_t y,
                                  uint32_t buttons, uint32_t action);
void script_dispatch_frame_event(script_engine_t *engine, uint32_t width, uint32_t height);
void script_dispatch_connect(script_engine_t *engine);
void script_dispatch_disconnect(script_engine_t *engine);

/* Process messages from script engine in main thread.
 * Command callbacks are called for each message type. Pass NULL to skip.
 * Query callbacks handle synchronous queries and send responses.
 * Returns number of messages processed. */
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
                            void *ctx);

#endif /* SCRIPT_EVENT_DISPATCH_H */
