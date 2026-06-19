#include "video_decoder.h"
#include "../platform/log.h"
#include <libavcodec/avcodec.h>
#include <stdlib.h>
#include <string.h>

struct video_decoder {
    AVCodecContext *codec_ctx;
    AVCodecParserContext *parser;
    AVFrame *frame;
    AVPacket *packet;
    bool got_keyframe; /* Whether we've received a keyframe yet */
};

video_decoder_t *video_decoder_create(void) {
    video_decoder_t *decoder = calloc(1, sizeof(video_decoder_t));
    if (!decoder) return NULL;
    decoder->frame = av_frame_alloc();
    decoder->packet = av_packet_alloc();
    if (!decoder->frame || !decoder->packet) {
        video_decoder_destroy(decoder);
        return NULL;
    }
    decoder->got_keyframe = false;
    return decoder;
}

bool video_decoder_init(video_decoder_t *decoder, uint32_t codec_id,
                        uint32_t width, uint32_t height) {
    const AVCodec *codec;
    switch (codec_id) {
        case 0x68323634: codec = avcodec_find_decoder(AV_CODEC_ID_H264); break;
        case 0x68323635: codec = avcodec_find_decoder(AV_CODEC_ID_HEVC); break;
        case 0x00617631: codec = avcodec_find_decoder(AV_CODEC_ID_AV1); break;
        default:
            log_error("Unsupported video codec: 0x%08x", codec_id);
            return false;
    }
    if (!codec) { log_error("Failed to find video codec"); return false; }

    decoder->parser = av_parser_init(codec->id);
    if (!decoder->parser) {
        log_warn("Failed to init parser, using raw mode");
    }

    decoder->codec_ctx = avcodec_alloc_context3(codec);
    if (!decoder->codec_ctx) { log_error("Failed to allocate codec context"); return false; }
    decoder->codec_ctx->width = width;
    decoder->codec_ctx->height = height;
    decoder->codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;

    if (avcodec_open2(decoder->codec_ctx, codec, NULL) < 0) {
        log_error("Failed to open codec");
        avcodec_free_context(&decoder->codec_ctx);
        return false;
    }
    return true;
}

/* Convert decoded YUV420P frame to NV12 */
static bool frame_to_nv12(const AVFrame *src, video_frame_t *dst) {
    uint32_t w = src->width;
    uint32_t h = src->height;
    dst->width = w;
    dst->height = h;
    dst->format = 0; /* NV12 */

    uint32_t nv12_size = w * h + w * (h / 2);
    dst->data = malloc(nv12_size);
    if (!dst->data) return false;

    /* Copy Y plane */
    const uint8_t *y_src = src->data[0];
    for (uint32_t row = 0; row < h; row++)
        memcpy(dst->data + row * w, y_src + row * src->linesize[0], w);

    /* Interleave UV planes into NV12 */
    uint8_t *uv_dst = dst->data + w * h;
    const uint8_t *u_src = src->data[1];
    const uint8_t *v_src = src->data[2];
    int uv_stride = src->linesize[1];
    for (uint32_t row = 0; row < h / 2; row++)
        for (uint32_t col = 0; col < w / 2; col++) {
            uv_dst[row * w + col * 2 + 0] = u_src[row * uv_stride + col];
            uv_dst[row * w + col * 2 + 1] = v_src[row * uv_stride + col];
        }

    return true;
}

/* Send packet to decoder and try to extract frames */
static bool decode_packet(video_decoder_t *decoder, const uint8_t *data, int size, video_frame_t *frame) {
    decoder->packet->data = (uint8_t *)data;
    decoder->packet->size = size;

    int ret = avcodec_send_packet(decoder->codec_ctx, decoder->packet);
    if (ret < 0 && ret != AVERROR(EAGAIN)) return false;

    /* Extract all available frames, keep the latest one */
    bool got_frame = false;
    while (true) {
        ret = avcodec_receive_frame(decoder->codec_ctx, decoder->frame);
        if (ret < 0) break;

        /* Free previous frame data if we got a newer one */
        if (got_frame) {
            video_frame_free(frame);
        }

        if (frame_to_nv12(decoder->frame, frame)) {
            got_frame = true;
        }
    }

    return got_frame;
}

bool video_decoder_decode(video_decoder_t *decoder, const uint8_t *data,
                          uint32_t size, video_frame_t *frame) {
    if (!decoder || !decoder->codec_ctx) return false;

    if (decoder->parser) {
        /* Use parser for proper NAL framing */
        const uint8_t *buf = data;
        int remaining = (int)size;

        while (remaining > 0) {
            uint8_t *parsed_data = NULL;
            int parsed_size = 0;
            int consumed = av_parser_parse2(decoder->parser, decoder->codec_ctx,
                                            &parsed_data, &parsed_size,
                                            buf, remaining, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (consumed < 0) break;

            buf += consumed;
            remaining -= consumed;

            /* If parser produced a complete packet, decode it */
            if (parsed_size > 0) {
                if (decode_packet(decoder, parsed_data, parsed_size, frame)) {
                    return true; /* Got a frame */
                }
            }
        }

        return false;
    }

    /* Fallback: raw mode without parser */
    return decode_packet(decoder, data, (int)size, frame);
}

void video_frame_free(video_frame_t *frame) {
    if (frame && frame->data) { free(frame->data); frame->data = NULL; }
}

void video_decoder_destroy(video_decoder_t *decoder) {
    if (!decoder) return;
    if (decoder->parser) av_parser_close(decoder->parser);
    if (decoder->codec_ctx) avcodec_free_context(&decoder->codec_ctx);
    if (decoder->frame) av_frame_free(&decoder->frame);
    if (decoder->packet) av_packet_free(&decoder->packet);
    free(decoder);
}
