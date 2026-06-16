#ifndef AUDIO_REGULATOR_H
#define AUDIO_REGULATOR_H

#include <stdbool.h>
#include <stdint.h>
#include "../platform/thread.h"

/**
 * Audio regulator for synchronization.
 * Buffers audio samples and provides them to the player at a steady rate.
 * Handles underflow by inserting silence.
 */
typedef struct {
    mutex_t mutex;

    /* Ring buffer */
    uint8_t *buf;
    uint32_t buf_size;      /* Total buffer size in bytes */
    uint32_t write_pos;
    uint32_t read_pos;
    uint32_t buffered;      /* Bytes currently buffered */

    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bytes_per_sample; /* bytes per sample per channel */

    /* Underflow tracking */
    uint32_t underflow_count;
} audio_regulator_t;

bool audio_regulator_init(audio_regulator_t *ar, uint32_t sample_rate,
                           uint32_t channels, uint32_t bytes_per_sample);
void audio_regulator_destroy(audio_regulator_t *ar);

/* Push decoded audio samples into the buffer */
bool audio_regulator_push(audio_regulator_t *ar, const uint8_t *data, uint32_t size);

/* Pull samples for playback. Inserts silence on underflow. */
void audio_regulator_pull(audio_regulator_t *ar, uint8_t *out, uint32_t samples);

/* Get number of buffered samples */
uint32_t audio_regulator_buffered_samples(audio_regulator_t *ar);

#endif /* AUDIO_REGULATOR_H */
