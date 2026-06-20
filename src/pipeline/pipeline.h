#ifndef PIPELINE_H
#define PIPELINE_H

#include <stdint.h>
#include <stdlib.h>
#include <windows.h>

/* Complete frame data — allocated by producer, freed by consumer */
typedef struct {
    uint8_t *data;
    uint32_t width;
    uint32_t height;
} frame_data_t;

/* Thread-safe shared frame — atomic pointer swap */
typedef struct {
    frame_data_t *current;  /* Atomic pointer swap */
    volatile LONG ready;    /* 1 = frame available, 0 = consumed */
} shared_frame_t;

/* Initialize shared frame */
static inline void shared_frame_init(shared_frame_t *sf) {
    sf->current = NULL;
    InterlockedExchange(&sf->ready, 0);
}

/* Producer: submit frame (atomically swaps pointer) */
static inline void shared_frame_submit(shared_frame_t *sf, frame_data_t *frame) {
    frame_data_t *old = InterlockedExchangePointer(
        (volatile PVOID *)&sf->current, frame);
    InterlockedExchange(&sf->ready, 1);
    if (old) {
        if (old->data) free(old->data);
        free(old);
    }
}

/* Consumer: acquire frame (returns NULL if no frame available) */
static inline frame_data_t *shared_frame_acquire(shared_frame_t *sf) {
    if (!InterlockedCompareExchange(&sf->ready, 0, 1)) {
        return NULL;
    }
    frame_data_t *frame = InterlockedExchangePointer(
        (volatile PVOID *)&sf->current, NULL);
    return frame;
}

/* Free frame data */
static inline void frame_data_free(frame_data_t *frame) {
    if (frame) {
        if (frame->data) free(frame->data);
        free(frame);
    }
}

#endif /* PIPELINE_H */
