#include "recorder.h"
#include "muxer.h"
#include "../platform/log.h"
#include <stdlib.h>

struct recorder {
    muxer_t *muxer;
    int video_stream;
    int audio_stream;
};

recorder_t *recorder_create(void) {
    recorder_t *rec = calloc(1, sizeof(recorder_t));
    if (!rec) return NULL;

    rec->muxer = muxer_create();
    if (!rec->muxer) {
        free(rec);
        return NULL;
    }

    return rec;
}

bool recorder_init(recorder_t *rec, const recorder_config_t *config) {
    if (!muxer_init(rec->muxer, config->filename, config->format)) {
        log_error("Failed to initialize muxer");
        return false;
    }

    // Add video stream
    rec->video_stream = muxer_add_video_stream(rec->muxer, config->video_codec,
                                                config->width, config->height);
    if (rec->video_stream < 0) {
        log_error("Failed to add video stream");
        return false;
    }

    // Add audio stream if needed
    if (config->audio_codec != 0) {
        rec->audio_stream = muxer_add_audio_stream(rec->muxer, config->audio_codec,
                                                    config->sample_rate, config->channels);
        if (rec->audio_stream < 0) {
            log_error("Failed to add audio stream");
            return false;
        }
    }

    if (!muxer_write_header(rec->muxer)) {
        log_error("Failed to write header");
        return false;
    }

    return true;
}

bool recorder_write_video(recorder_t *rec, const uint8_t *data, uint32_t size, int64_t pts) {
    return muxer_write_packet(rec->muxer, rec->video_stream, data, size, pts);
}

bool recorder_write_audio(recorder_t *rec, const uint8_t *data, uint32_t size, int64_t pts) {
    return muxer_write_packet(rec->muxer, rec->audio_stream, data, size, pts);
}

void recorder_destroy(recorder_t *rec) {
    if (!rec) return;

    if (rec->muxer) {
        muxer_write_trailer(rec->muxer);
        muxer_destroy(rec->muxer);
    }

    free(rec);
}
