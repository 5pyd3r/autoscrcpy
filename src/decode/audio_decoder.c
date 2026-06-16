#include "audio_decoder.h"
#include "../platform/log.h"
#include <libavcodec/avcodec.h>
#include <stdlib.h>
#include <string.h>

struct audio_decoder {
    AVCodecContext *codec_ctx;
    AVFrame *frame;
    AVPacket *packet;
};

audio_decoder_t *audio_decoder_create(void) {
    audio_decoder_t *decoder = calloc(1, sizeof(audio_decoder_t));
    if (!decoder) return NULL;

    decoder->frame = av_frame_alloc();
    decoder->packet = av_packet_alloc();

    if (!decoder->frame || !decoder->packet) {
        audio_decoder_destroy(decoder);
        return NULL;
    }

    return decoder;
}

bool audio_decoder_init(audio_decoder_t *decoder, uint32_t codec_id,
                        uint32_t sample_rate, uint32_t channels) {
    const AVCodec *codec;

    switch (codec_id) {
        case 0x6f707573: // opus
            codec = avcodec_find_decoder(AV_CODEC_ID_OPUS);
            break;
        case 0x61616320: // aac
            codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
            break;
        case 0x666c6163: // flac
            codec = avcodec_find_decoder(AV_CODEC_ID_FLAC);
            break;
        default:
            log_error("Unsupported audio codec: 0x%08x", codec_id);
            return false;
    }

    if (!codec) {
        log_error("Failed to find audio codec");
        return false;
    }

    decoder->codec_ctx = avcodec_alloc_context3(codec);
    if (!decoder->codec_ctx) {
        log_error("Failed to allocate codec context");
        return false;
    }

    decoder->codec_ctx->sample_rate = sample_rate;
    decoder->codec_ctx->ch_layout.nb_channels = channels;
    decoder->codec_ctx->sample_fmt = AV_SAMPLE_FMT_FLT;

    if (avcodec_open2(decoder->codec_ctx, codec, NULL) < 0) {
        log_error("Failed to open codec");
        return false;
    }

    return true;
}

bool audio_decoder_decode(audio_decoder_t *decoder, const uint8_t *data,
                          uint32_t size, audio_frame_t *frame) {
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

    // Calculate output size
    int nb_samples = decoder->frame->nb_samples;
    int bytes_per_sample = av_get_bytes_per_sample(decoder->codec_ctx->sample_fmt);
    uint32_t output_size = nb_samples * decoder->codec_ctx->ch_layout.nb_channels * bytes_per_sample;

    // Allocate output buffer
    frame->data = malloc(output_size);
    if (!frame->data) {
        log_error("Failed to allocate audio frame buffer");
        return false;
    }

    // Copy audio data
    memcpy(frame->data, decoder->frame->data[0], output_size);
    frame->size = output_size;
    frame->sample_rate = decoder->codec_ctx->sample_rate;
    frame->channels = decoder->codec_ctx->ch_layout.nb_channels;

    return true;
}

void audio_decoder_destroy(audio_decoder_t *decoder) {
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
