#ifndef RECORDER_H
#define RECORDER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const char *filename;
    const char *format; // mp4, mkv
    uint32_t video_codec;
    uint32_t audio_codec;
    uint32_t width;
    uint32_t height;
    uint32_t sample_rate;
    uint32_t channels;
} recorder_config_t;

typedef struct recorder recorder_t;

recorder_t *recorder_create(void);
bool recorder_init(recorder_t *rec, const recorder_config_t *config);
bool recorder_write_video(recorder_t *rec, const uint8_t *data, uint32_t size, int64_t pts);
bool recorder_write_audio(recorder_t *rec, const uint8_t *data, uint32_t size, int64_t pts);
void recorder_destroy(recorder_t *rec);

#endif /* RECORDER_H */
