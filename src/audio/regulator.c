#include "regulator.h"
#include "../platform/log.h"
#include <stdlib.h>
#include <string.h>

/* Buffer 50ms of audio */
#define BUFFER_DURATION_MS 50

bool audio_regulator_init(audio_regulator_t *ar, uint32_t sample_rate,
                           uint32_t channels, uint32_t bytes_per_sample) {
    ar->sample_rate = sample_rate;
    ar->channels = channels;
    ar->bytes_per_sample = bytes_per_sample;
    ar->underflow_count = 0;

    /* Calculate buffer size for BUFFER_DURATION_MS */
    uint32_t bytes_per_frame = channels * bytes_per_sample;
    uint32_t frames_per_buffer = (sample_rate * BUFFER_DURATION_MS) / 1000;
    ar->buf_size = frames_per_buffer * bytes_per_frame;

    ar->buf = malloc(ar->buf_size);
    if (!ar->buf) {
        log_error("Failed to allocate audio regulator buffer");
        return false;
    }

    ar->write_pos = 0;
    ar->read_pos = 0;
    ar->buffered = 0;

    mutex_init(&ar->mutex);
    return true;
}

void audio_regulator_destroy(audio_regulator_t *ar) {
    if (ar->buf) {
        free(ar->buf);
        ar->buf = NULL;
    }
    mutex_destroy(&ar->mutex);
}

bool audio_regulator_push(audio_regulator_t *ar, const uint8_t *data, uint32_t size) {
    mutex_lock(&ar->mutex);

    /* If buffer would overflow, drop oldest data */
    while (ar->buffered + size > ar->buf_size) {
        uint32_t bytes_per_frame = ar->channels * ar->bytes_per_sample;
        ar->read_pos = (ar->read_pos + bytes_per_frame) % ar->buf_size;
        ar->buffered -= bytes_per_frame;
    }

    /* Write data to ring buffer */
    uint32_t first_chunk = ar->buf_size - ar->write_pos;
    if (first_chunk > size) first_chunk = size;

    memcpy(ar->buf + ar->write_pos, data, first_chunk);
    if (first_chunk < size) {
        memcpy(ar->buf, data + first_chunk, size - first_chunk);
    }

    ar->write_pos = (ar->write_pos + size) % ar->buf_size;
    ar->buffered += size;

    mutex_unlock(&ar->mutex);
    return true;
}

void audio_regulator_pull(audio_regulator_t *ar, uint8_t *out, uint32_t samples) {
    uint32_t bytes_needed = samples * ar->channels * ar->bytes_per_sample;

    mutex_lock(&ar->mutex);

    if (ar->buffered >= bytes_needed) {
        /* Enough data available */
        uint32_t first_chunk = ar->buf_size - ar->read_pos;
        if (first_chunk > bytes_needed) first_chunk = bytes_needed;

        memcpy(out, ar->buf + ar->read_pos, first_chunk);
        if (first_chunk < bytes_needed) {
            memcpy(out + first_chunk, ar->buf, bytes_needed - first_chunk);
        }

        ar->read_pos = (ar->read_pos + bytes_needed) % ar->buf_size;
        ar->buffered -= bytes_needed;
    } else {
        /* Underflow: copy what we have, fill rest with silence */
        if (ar->buffered > 0) {
            uint32_t first_chunk = ar->buf_size - ar->read_pos;
            if (first_chunk > ar->buffered) first_chunk = ar->buffered;

            memcpy(out, ar->buf + ar->read_pos, first_chunk);
            if (first_chunk < ar->buffered) {
                memcpy(out + first_chunk, ar->buf, ar->buffered - first_chunk);
            }
        }

        /* Fill remainder with silence */
        if (ar->buffered < bytes_needed) {
            memset(out + ar->buffered, 0, bytes_needed - ar->buffered);
        }

        ar->read_pos = 0;
        ar->write_pos = 0;
        ar->buffered = 0;
        ar->underflow_count++;
    }

    mutex_unlock(&ar->mutex);
}

uint32_t audio_regulator_buffered_samples(audio_regulator_t *ar) {
    mutex_lock(&ar->mutex);
    uint32_t samples = ar->buffered / (ar->channels * ar->bytes_per_sample);
    mutex_unlock(&ar->mutex);
    return samples;
}
