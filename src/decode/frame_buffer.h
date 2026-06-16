#ifndef FRAME_BUFFER_H
#define FRAME_BUFFER_H

#include <stdbool.h>
#include <stdint.h>
#include "../platform/thread.h"

/* Forward declaration */
typedef struct AVFrame AVFrame;

/**
 * Single-frame buffer for latency control.
 * Always provides the latest frame, dropping old ones.
 */
typedef struct {
    AVFrame *pending_frame;
    bool has_frame;
    mutex_t mutex;
} frame_buffer_t;

bool frame_buffer_init(frame_buffer_t *fb);
void frame_buffer_destroy(frame_buffer_t *fb);

/* Push a new frame (drops previous if not consumed) */
bool frame_buffer_push(frame_buffer_t *fb, const AVFrame *frame);

/* Check if a frame is available */
bool frame_buffer_has_frame(frame_buffer_t *fb);

/* Consume the pending frame (caller must allocate dst) */
bool frame_buffer_consume(frame_buffer_t *fb, AVFrame *dst);

#endif /* FRAME_BUFFER_H */
