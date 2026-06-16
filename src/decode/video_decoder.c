#include "video_decoder.h"
#include "../platform/log.h"
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <stdlib.h>
#include <string.h>

struct video_decoder {
    AVCodecContext *codec_ctx;
    AVFrame *frame;
    AVPacket *packet;
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

    return decoder;
}

bool video_decoder_init(video_decoder_t *decoder, uint32_t codec_id,
                        uint32_t width, uint32_t height) {
    const AVCodec *codec;

    switch (codec_id) {
        case 0x68323634: // h264
            codec = avcodec_find_decoder(AV_CODEC_ID_H264);
            break;
        case 0x68323635: // h265
            codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
            break;
        case 0x00415631: // av01
            codec = avcodec_find_decoder(AV_CODEC_ID_AV1);
            break;
        default:
            log_error("Unsupported video codec: 0x%08x", codec_id);
            return false;
    }

    if (!codec) {
        log_error("Failed to find video codec");
        return false;
    }

    decoder->codec_ctx = avcodec_alloc_context3(codec);
    if (!decoder->codec_ctx) {
        log_error("Failed to allocate codec context");
        return false;
    }

    decoder->codec_ctx->width = width;
    decoder->codec_ctx->height = height;
    decoder->codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;

    if (avcodec_open2(decoder->codec_ctx, codec, NULL) < 0) {
        log_error("Failed to open codec");
        return false;
    }

    return true;
}

bool video_decoder_decode(video_decoder_t *decoder, const uint8_t *data,
                          uint32_t size, video_frame_t *frame) {
    decoder->packet->data = (uint8_t *)data;
    decoder->packet->size = size;

    int ret = avcodec_send_packet(decoder->codec_ctx, decoder->packet);
    if (ret < 0) {
        log_error("Failed to send packet to decoder");
        return false;
    }

    ret = avcodec_receive_frame(decoder->codec_ctx, decoder->frame);
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) return false;
        log_error("Failed to receive frame from decoder");
        return false;
    }

    // Convert frame to output format
    frame->width = decoder->frame->width;
    frame->height = decoder->frame->height;
    frame->format = 1; // BGRA

    // Allocate output buffer
    uint32_t bgra_size = frame->width * frame->height * 4;
    frame->data = malloc(bgra_size);
    if (!frame->data) {
        log_error("Failed to allocate frame buffer");
        return false;
    }

    // Convert YUV to BGRA
    // TODO: Use swscale for proper conversion
    for (uint32_t y = 0; y < frame->height; y++) {
        for (uint32_t x = 0; x < frame->width; x++) {
            uint32_t idx = (y * frame->width + x) * 4;
            frame->data[idx + 0] = 0; // B
            frame->data[idx + 1] = 0; // G
            frame->data[idx + 2] = 0; // R
            frame->data[idx + 3] = 255; // A
        }
    }

    return true;
}

void video_decoder_destroy(video_decoder_t *decoder) {
    if (!decoder) return;

    if (decoder->codec_ctx) {
        avcodec_free_context(&decoder->codec_ctx);
    }
    if (decoder->frame) {
        av_frame_free(&decoder->frame);
    }
    if (decoder->packet) {
        av_packet_free(&decoder->packet);
    }

    free(decoder);
}
