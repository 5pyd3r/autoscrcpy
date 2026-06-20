#ifndef VIDEO_PIPELINE_H
#define VIDEO_PIPELINE_H

#include "pipeline.h"
#include "../decode/video_decoder.h"
#include "../device/video_socket.h"
#include <stdbool.h>
#include <windows.h>

typedef struct {
    video_decoder_t *decoder;
    video_socket_t *sock;
    shared_frame_t *shared_frame;
    HANDLE thread;
    volatile bool running;
} video_pipeline_t;

bool video_pipeline_init(video_pipeline_t *vp, video_decoder_t *decoder,
                         video_socket_t *sock, shared_frame_t *shared_frame);
bool video_pipeline_start(video_pipeline_t *vp);
void video_pipeline_stop(video_pipeline_t *vp);
void video_pipeline_destroy(video_pipeline_t *vp);

#endif /* VIDEO_PIPELINE_H */
