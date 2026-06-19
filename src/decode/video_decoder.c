#include "video_decoder.h"
#include "../platform/log.h"
#include "../adb/binary.h"
#include <libavcodec/avcodec.h>
#include <stdlib.h>
#include <string.h>

struct video_decoder {
    AVCodecContext *codec_ctx;
    AVCodecParserContext *parser;
    AVFrame *frame;
    AVPacket *packet;
    bool got_sps; /* Whether we've received SPS */
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
    decoder->got_sps = false;
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

/* Check if data starts with H.264 Annex B start code */
static bool is_annex_b(const uint8_t *data, uint32_t size) {
    if (size < 4) return false;
    return (data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1);
}

/* Detect if data is AVCC format (4-byte length prefix) */
static bool is_avcc(const uint8_t *data, uint32_t size) {
    if (size < 4) return false;
    uint32_t len = read32be(data);
    /* Reasonable NAL size: 1 byte to 10MB */
    return (len > 0 && len < 10 * 1024 * 1024 && len <= size - 4);
}

bool video_decoder_decode(video_decoder_t *decoder, const uint8_t *data,
                          uint32_t size, video_frame_t *frame) {
    if (!decoder || !decoder->codec_ctx) return false;

    frame->data = NULL;

    /* Check if this is Annex B format (has start code) */
    if (is_annex_b(data, size)) {
        /* Annex B format - use parser */
        if (decoder->parser) {
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

                if (parsed_size > 0) {
                    decoder->packet->data = parsed_data;
                    decoder->packet->size = parsed_size;
                    avcodec_send_packet(decoder->codec_ctx, decoder->packet);
                }
            }
        } else {
            /* No parser - send raw */
            decoder->packet->data = (uint8_t *)data;
            decoder->packet->size = (int)size;
            avcodec_send_packet(decoder->codec_ctx, decoder->packet);
        }
    }
    /* Check if this is AVCC format (length-prefixed) */
    else if (is_avcc(data, size)) {
        /* AVCC format - convert to Annex B and send */
        const uint8_t *buf = data;
        uint32_t remaining = size;

        while (remaining >= 4) {
            uint32_t nal_len = read32be(buf);
            buf += 4;
            remaining -= 4;

            if (nal_len > remaining) break; /* Invalid length */

            /* Send NAL unit directly (decoder can handle raw NAL) */
            decoder->packet->data = (uint8_t *)buf;
            decoder->packet->size = (int)nal_len;
            avcodec_send_packet(decoder->codec_ctx, decoder->packet);

            buf += nal_len;
            remaining -= nal_len;
        }
    }
    else {
        /* Unknown format - try sending raw */
        decoder->packet->data = (uint8_t *)data;
        decoder->packet->size = (int)size;
        avcodec_send_packet(decoder->codec_ctx, decoder->packet);
    }

    /* Try to receive decoded frame */
    if (avcodec_receive_frame(decoder->codec_ctx, decoder->frame) >= 0) {
        return frame_to_nv12(decoder->frame, frame);
    }

    return false;
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
