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
    uint8_t *config_data;   /* buffered SPS/PPS from config packets */
    uint32_t config_size;
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
    /* For H.264: config packets (SPS/PPS) need to be set as extradata.
     * Buffer config data and prepend to the next keyframe. */
    if (size >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 &&
        (data[3] == 1 || data[3] == 0)) {
        /* Looks like Annex B NAL units — check if it's SPS/PPS (NAL type 7 or 8) */
        int nal_type = data[3] == 0 ? (data[4] & 0x1F) : (data[3] & 0x1F);
        if (nal_type == 7 || nal_type == 8) {
            /* Config packet (SPS or PPS) — buffer it */
            free(decoder->config_data);
            decoder->config_data = malloc(size);
            if (decoder->config_data) {
                memcpy(decoder->config_data, data, size);
                decoder->config_size = size;
            }
            return false; /* No frame to output yet */
        }
    }

    /* If we have buffered config data, prepend it to this packet */
    uint8_t *combined = NULL;
    if (decoder->config_data && decoder->config_size > 0) {
        combined = malloc(decoder->config_size + size);
        if (combined) {
            memcpy(combined, decoder->config_data, decoder->config_size);
            memcpy(combined + decoder->config_size, data, size);
            decoder->packet->data = combined;
            decoder->packet->size = decoder->config_size + size;
        }
    }
    if (!combined) {
        decoder->packet->data = (uint8_t *)data;
        decoder->packet->size = size;
    }

    int ret = avcodec_send_packet(decoder->codec_ctx, decoder->packet);
    free(combined);
    if (ret < 0) {
        /* Config packets may not produce a frame — that's OK */
        return false;
    }

    ret = avcodec_receive_frame(decoder->codec_ctx, decoder->frame);
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) return false;
        log_error("Failed to receive frame from decoder");
        return false;
    }

    uint32_t w = decoder->frame->width;
    uint32_t h = decoder->frame->height;

    frame->width = w;
    frame->height = h;
    frame->format = 0; /* NV12 */

    /* NV12 buffer: Y plane (w*h) + UV plane (w*h/2) */
    uint32_t nv12_size = w * h + w * (h / 2);
    frame->data = malloc(nv12_size);
    if (!frame->data) {
        log_error("Failed to allocate NV12 frame buffer");
        return false;
    }

    /* Copy Y plane */
    uint8_t *y_dst = frame->data;
    const uint8_t *y_src = decoder->frame->data[0];
    for (uint32_t row = 0; row < h; row++) {
        memcpy(y_dst + row * w, y_src + row * decoder->frame->linesize[0], w);
    }

    /* Interleave UV planes into NV12 format */
    uint8_t *uv_dst = frame->data + w * h;
    const uint8_t *u_src = decoder->frame->data[1];
    const uint8_t *v_src = decoder->frame->data[2];
    int uv_stride = decoder->frame->linesize[1];
    for (uint32_t row = 0; row < h / 2; row++) {
        for (uint32_t col = 0; col < w / 2; col++) {
            uv_dst[row * w + col * 2 + 0] = u_src[row * uv_stride + col];
            uv_dst[row * w + col * 2 + 1] = v_src[row * uv_stride + col];
        }
    }

    return true;
}

void video_frame_free(video_frame_t *frame) {
    if (frame && frame->data) {
        free(frame->data);
        frame->data = NULL;
    }
}

void video_decoder_destroy(video_decoder_t *decoder) {
    if (!decoder) return;

    free(decoder->config_data);
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
