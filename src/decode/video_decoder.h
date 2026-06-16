#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t *data;
    uint32_t width;
    uint32_t height;
    int format; // 0=NV12, 1=BGRA
} video_frame_t;

typedef struct video_decoder video_decoder_t;

video_decoder_t *video_decoder_create(void);
bool video_decoder_init(video_decoder_t *decoder, uint32_t codec_id,
                        uint32_t width, uint32_t height);
bool video_decoder_decode(video_decoder_t *decoder, const uint8_t *data,
                          uint32_t size, video_frame_t *frame);
void video_frame_free(video_frame_t *frame);
void video_decoder_destroy(video_decoder_t *decoder);

#endif /* VIDEO_DECODER_H */
