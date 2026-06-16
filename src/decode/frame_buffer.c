#include "frame_buffer.h"
#include <libavutil/frame.h>
#include <string.h>

bool frame_buffer_init(frame_buffer_t *fb) {
    fb->pending_frame = av_frame_alloc();
    fb->has_frame = false;
    if (!fb->pending_frame) return false;
    mutex_init(&fb->mutex);
    return true;
}

void frame_buffer_destroy(frame_buffer_t *fb) {
    if (fb->pending_frame) {
        av_frame_free(&fb->pending_frame);
    }
    mutex_destroy(&fb->mutex);
}

bool frame_buffer_push(frame_buffer_t *fb, const AVFrame *frame) {
    mutex_lock(&fb->mutex);

    /* Replace the pending frame */
    av_frame_unref(fb->pending_frame);
    int ret = av_frame_ref(fb->pending_frame, frame);
    if (ret < 0) {
        mutex_unlock(&fb->mutex);
        return false;
    }
    fb->has_frame = true;

    mutex_unlock(&fb->mutex);
    return true;
}

bool frame_buffer_has_frame(frame_buffer_t *fb) {
    mutex_lock(&fb->mutex);
    bool has = fb->has_frame;
    mutex_unlock(&fb->mutex);
    return has;
}

bool frame_buffer_consume(frame_buffer_t *fb, AVFrame *dst) {
    mutex_lock(&fb->mutex);

    if (!fb->has_frame) {
        mutex_unlock(&fb->mutex);
        return false;
    }

    int ret = av_frame_ref(dst, fb->pending_frame);
    fb->has_frame = false;

    mutex_unlock(&fb->mutex);
    return ret >= 0;
}
