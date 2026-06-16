#include "muxer.h"
#include "../platform/log.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdlib.h>
#include <string.h>

struct muxer {
    AVFormatContext *fmt_ctx;
    AVStream *video_stream;
    AVStream *audio_stream;
};

muxer_t *muxer_create(void) {
    muxer_t *mux = calloc(1, sizeof(muxer_t));
    return mux;
}

bool muxer_init(muxer_t *mux, const char *filename, const char *format) {
    int ret = avformat_alloc_output_context2(&mux->fmt_ctx, NULL, format, filename);
    if (ret < 0) {
        log_error("Failed to allocate output context");
        return false;
    }

    if (!(mux->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&mux->fmt_ctx->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            log_error("Failed to open output file");
            return false;
        }
    }

    return true;
}

int muxer_add_video_stream(muxer_t *mux, uint32_t codec_id, uint32_t width, uint32_t height) {
    AVStream *stream = avformat_new_stream(mux->fmt_ctx, NULL);
    if (!stream) {
        log_error("Failed to create video stream");
        return -1;
    }

    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codecpar->width = width;
    stream->codecpar->height = height;

    switch (codec_id) {
        case 0x68323634: // h264
            stream->codecpar->codec_id = AV_CODEC_ID_H264;
            break;
        case 0x68323635: // h265
            stream->codecpar->codec_id = AV_CODEC_ID_HEVC;
            break;
        case 0x00415631: // av01
            stream->codecpar->codec_id = AV_CODEC_ID_AV1;
            break;
        default:
            log_error("Unsupported video codec: 0x%08x", codec_id);
            return -1;
    }

    mux->video_stream = stream;
    return stream->index;
}

int muxer_add_audio_stream(muxer_t *mux, uint32_t codec_id, uint32_t sample_rate, uint32_t channels) {
    AVStream *stream = avformat_new_stream(mux->fmt_ctx, NULL);
    if (!stream) {
        log_error("Failed to create audio stream");
        return -1;
    }

    stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    stream->codecpar->sample_rate = sample_rate;
    stream->codecpar->ch_layout.nb_channels = channels;

    switch (codec_id) {
        case 0x6f707573: // opus
            stream->codecpar->codec_id = AV_CODEC_ID_OPUS;
            break;
        case 0x61616320: // aac
            stream->codecpar->codec_id = AV_CODEC_ID_AAC;
            break;
        case 0x666c6163: // flac
            stream->codecpar->codec_id = AV_CODEC_ID_FLAC;
            break;
        default:
            log_error("Unsupported audio codec: 0x%08x", codec_id);
            return -1;
    }

    mux->audio_stream = stream;
    return stream->index;
}

bool muxer_write_header(muxer_t *mux) {
    int ret = avformat_write_header(mux->fmt_ctx, NULL);
    if (ret < 0) {
        log_error("Failed to write header");
        return false;
    }
    return true;
}

bool muxer_write_packet(muxer_t *mux, int stream_index, const uint8_t *data, uint32_t size, int64_t pts) {
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        log_error("Failed to allocate packet");
        return false;
    }

    pkt->data = (uint8_t *)data;
    pkt->size = size;
    pkt->pts = pts;
    pkt->dts = pts;
    pkt->stream_index = stream_index;

    int ret = av_interleaved_write_frame(mux->fmt_ctx, pkt);
    av_packet_free(&pkt);

    if (ret < 0) {
        log_error("Failed to write frame");
        return false;
    }

    return true;
}

bool muxer_write_trailer(muxer_t *mux) {
    int ret = av_write_trailer(mux->fmt_ctx);
    if (ret < 0) {
        log_error("Failed to write trailer");
        return false;
    }
    return true;
}

void muxer_destroy(muxer_t *mux) {
    if (!mux) return;

    if (mux->fmt_ctx) {
        if (!(mux->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&mux->fmt_ctx->pb);
        }
        avformat_free_context(mux->fmt_ctx);
    }

    free(mux);
}
