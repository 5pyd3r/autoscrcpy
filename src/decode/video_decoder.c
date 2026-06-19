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
    AVPacket *parsed_pkt; /* Packet from parser */
};

video_decoder_t *video_decoder_create(void) {
    video_decoder_t *decoder = calloc(1, sizeof(video_decoder_t));
    if (!decoder) return NULL;
    decoder->frame = av_frame_alloc();
    decoder->packet = av_packet_alloc();
    decoder->parsed_pkt = av_packet_alloc();
    if (!decoder->frame || !decoder->packet || !decoder->parsed_pkt) {
        video_decoder_destroy(decoder);
        return NULL;
    }
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
    decoder->codec_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    decoder->codec_ctx->flags2 |= AV_CODEC_FLAG2_FAST;

    if (avcodec_open2(decoder->codec_ctx, codec, NULL) < 0) {
        log_error("Failed to open codec");
        avcodec_free_context(&decoder->codec_ctx);
        return false;
    }
    return true;
}

/* Try to receive decoded frames, returns true if a frame was received */
static bool try_receive_frame(video_decoder_t *decoder) {
    int ret = avcodec_receive_frame(decoder->codec_ctx, decoder->frame);
    return (ret >= 0);
}

/* Send a properly framed packet to the decoder */
static bool send_packet(video_decoder_t *decoder, const uint8_t *data, uint32_t size) {
    decoder->packet->data = (uint8_t *)data;
    decoder->packet->size = (int)size;
    int ret = avcodec_send_packet(decoder->codec_ctx, decoder->packet);
    return (ret >= 0 || ret == AVERROR(EAGAIN));
}

bool video_decoder_decode(video_decoder_t *decoder, const uint8_t *data,
                          uint32_t size, video_frame_t *frame) {
    if (!decoder || !decoder->codec_ctx) return false;

    /* Use parser if available for proper NAL framing */
    if (decoder->parser) {
        const uint8_t *buf = data;
        int remaining = (int)size;

        while (remaining > 0) {
            int parsed = av_parser_parse2(decoder->parser, decoder->codec_ctx,
                                          &decoder->parsed_pkt->data, &decoder->parsed_pkt->size,
                                          buf, remaining, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (parsed < 0) break;

            buf += parsed;
            remaining -= parsed;

            /* If parser produced a complete packet, send it */
            if (decoder->parsed_pkt->size > 0) {
                send_packet(decoder, decoder->parsed_pkt->data, decoder->parsed_pkt->size);

                /* Try to get a decoded frame */
                if (try_receive_frame(decoder)) {
                    goto got_frame;
                }
            }
        }

        /* Flush any remaining frames from the decoder */
        send_packet(decoder, NULL, 0);
        if (try_receive_frame(decoder)) {
            goto got_frame;
        }

        return false;
    }

    /* Fallback: raw mode without parser */
    if (!send_packet(decoder, data, size)) return false;
    if (!try_receive_frame(decoder)) return false;

got_frame:
    {
        uint32_t w = decoder->frame->width;
        uint32_t h = decoder->frame->height;
        frame->width = w;
        frame->height = h;
        frame->format = 0; /* NV12 */

        uint32_t nv12_size = w * h + w * (h / 2);
        frame->data = malloc(nv12_size);
        if (!frame->data) return false;

        /* Copy Y plane */
        const uint8_t *y_src = decoder->frame->data[0];
        for (uint32_t row = 0; row < h; row++)
            memcpy(frame->data + row * w, y_src + row * decoder->frame->linesize[0], w);

        /* Interleave UV planes into NV12 */
        uint8_t *uv_dst = frame->data + w * h;
        const uint8_t *u_src = decoder->frame->data[1];
        const uint8_t *v_src = decoder->frame->data[2];
        int uv_stride = decoder->frame->linesize[1];
        for (uint32_t row = 0; row < h / 2; row++)
            for (uint32_t col = 0; col < w / 2; col++) {
                uv_dst[row * w + col * 2 + 0] = u_src[row * uv_stride + col];
                uv_dst[row * w + col * 2 + 1] = v_src[row * uv_stride + col];
            }

        return true;
    }
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
    if (decoder->parsed_pkt) av_packet_free(&decoder->parsed_pkt);
    free(decoder);
}
