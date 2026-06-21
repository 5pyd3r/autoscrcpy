#ifndef SCRIPT_BINDINGS_H
#define SCRIPT_BINDINGS_H

#include "message_queue.h"
#include <stdbool.h>

/* Initialize all FFI bindings. Call after Sscheme_init/Sbuild_heap.
 * to_main: queue for Scheme->Main messages
 * response_q: queue for Main->Scheme query responses */
bool script_bindings_init(script_msg_queue_t *to_main, script_msg_queue_t *response_q);

/* Get device info (called from main thread to fill response) */
void script_fill_device_info(script_msg_t *resp, uint32_t w, uint32_t h,
                             const char *name, bool connected);

/* Get video size (called from main thread) */
void script_fill_video_size(script_msg_t *resp, uint32_t w, uint32_t h);

/* Get window size (called from main thread) */
void script_fill_window_size(script_msg_t *resp, uint32_t w, uint32_t h);

/* Get clipboard (called from main thread) */
void script_fill_clipboard(script_msg_t *resp, const char *text);

/* Get frame capture (called from main thread) */
void script_fill_frame_capture(script_msg_t *resp, const uint8_t *nv12_data,
                               uint32_t width, uint32_t height, uint32_t data_size);

#endif /* SCRIPT_BINDINGS_H */
