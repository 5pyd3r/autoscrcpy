#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t sample_rate;
    uint32_t channels;
} audio_frame_t;

typedef struct audio_decoder audio_decoder_t;

audio_decoder_t *audio_decoder_create(void);
bool audio_decoder_init(audio_decoder_t *decoder, uint32_t codec_id,
                        uint32_t sample_rate, uint32_t channels);
bool audio_decoder_decode(audio_decoder_t *decoder, const uint8_t *data,
                          uint32_t size, audio_frame_t *frame);
void audio_decoder_destroy(audio_decoder_t *decoder);

#endif /* AUDIO_DECODER_H */
