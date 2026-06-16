#ifndef MUXER_H
#define MUXER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct muxer muxer_t;

muxer_t *muxer_create(void);
bool muxer_init(muxer_t *mux, const char *filename, const char *format);
int muxer_add_video_stream(muxer_t *mux, uint32_t codec_id, uint32_t width, uint32_t height);
int muxer_add_audio_stream(muxer_t *mux, uint32_t codec_id, uint32_t sample_rate, uint32_t channels);
bool muxer_write_header(muxer_t *mux);
bool muxer_write_packet(muxer_t *mux, int stream_index, const uint8_t *data, uint32_t size, int64_t pts);
bool muxer_write_trailer(muxer_t *mux);
void muxer_destroy(muxer_t *mux);

#endif /* MUXER_H */
