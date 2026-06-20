#include "audio_decoder.h"
#include "../platform/log.h"
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <stdlib.h>
#include <string.h>

struct audio_decoder {
    AVCodecContext *codec_ctx;
    AVFrame *frame;
    AVPacket *packet;
    SwrContext *swr_ctx;
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
        case 0x00616163: // aac
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
    av_channel_layout_default(&decoder->codec_ctx->ch_layout, channels);
    decoder->codec_ctx->sample_fmt = AV_SAMPLE_FMT_FLT;

    if (avcodec_open2(decoder->codec_ctx, codec, NULL) < 0) {
        log_error("Failed to open codec");
        avcodec_free_context(&decoder->codec_ctx);
        return false;
    }

    /* Set up swresample to convert decoder output to interleaved float.
     * Opus decoder outputs planar (FLTP) even when FLT is requested.
     * This handles any input format → FLT interleaved conversion. */
    AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
    swr_alloc_set_opts2(&decoder->swr_ctx,
                        &out_layout, AV_SAMPLE_FMT_FLT, sample_rate,
                        &decoder->codec_ctx->ch_layout,
                        decoder->codec_ctx->sample_fmt,
                        decoder->codec_ctx->sample_rate,
                        0, NULL);
    if (!decoder->swr_ctx || swr_init(decoder->swr_ctx) < 0) {
        log_error("Failed to init swresample");
        if (decoder->swr_ctx) swr_free(&decoder->swr_ctx);
        decoder->swr_ctx = NULL;
        /* Non-fatal: will try direct copy as fallback */
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

    int nb_samples = decoder->frame->nb_samples;
    int out_channels = 2; /* Always output stereo */
    int bytes_per_sample = av_get_bytes_per_sample(AV_SAMPLE_FMT_FLT);
    uint32_t output_size = nb_samples * out_channels * bytes_per_sample;

    frame->data = malloc(output_size);
    if (!frame->data) {
        log_error("Failed to allocate audio frame buffer");
        return false;
    }

    if (decoder->swr_ctx) {
        /* Convert decoder output (possibly planar) to interleaved float */
        int converted = swr_convert(decoder->swr_ctx,
                                    &frame->data, nb_samples,
                                    (const uint8_t **)decoder->frame->data,
                                    nb_samples);
        if (converted < 0) {
            log_error("swr_convert failed");
            free(frame->data);
            frame->data = NULL;
            return false;
        }
        frame->size = converted * out_channels * bytes_per_sample;
    } else {
        /* Fallback: direct copy (only works if format already matches) */
        memcpy(frame->data, decoder->frame->data[0], output_size);
        frame->size = output_size;
    }

    frame->sample_rate = decoder->codec_ctx->sample_rate;
    frame->channels = out_channels;

    return true;
}

void audio_decoder_destroy(audio_decoder_t *decoder) {
    if (!decoder) return;

    if (decoder->swr_ctx) {
        swr_free(&decoder->swr_ctx);
    }
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
