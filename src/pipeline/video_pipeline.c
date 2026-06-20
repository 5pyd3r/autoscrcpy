#include "video_pipeline.h"
#include "../platform/log.h"
#include <string.h>
#include <stdlib.h>

static DWORD WINAPI video_thread_func(LPVOID arg) {
    video_pipeline_t *vp = (video_pipeline_t *)arg;
    uint8_t buf[128 * 1024];

    while (vp->running) {
        int n = recv(vp->sock->fd, (char *)buf, sizeof(buf), 0);
        if (n <= 0) {
            if (vp->running) log_error("Video socket read failed");
            break;
        }

        video_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        if (video_decoder_decode(vp->decoder, buf, (uint32_t)n, &frame)) {
            frame_data_t *fd = calloc(1, sizeof(frame_data_t));
            if (fd) {
                fd->data = frame.data;
                fd->width = frame.width;
                fd->height = frame.height;
                frame.data = NULL;
                shared_frame_submit(vp->shared_frame, fd);
            } else {
                video_frame_free(&frame);
            }
        }
    }
    return 0;
}

bool video_pipeline_init(video_pipeline_t *vp, video_decoder_t *decoder,
                         video_socket_t *sock, shared_frame_t *shared_frame) {
    vp->decoder = decoder;
    vp->sock = sock;
    vp->shared_frame = shared_frame;
    vp->thread = NULL;
    vp->running = false;
    return true;
}

bool video_pipeline_start(video_pipeline_t *vp) {
    vp->running = true;
    vp->thread = CreateThread(NULL, 0, video_thread_func, vp, 0, NULL);
    if (!vp->thread) {
        log_error("Failed to create video thread");
        vp->running = false;
        return false;
    }
    return true;
}

void video_pipeline_stop(video_pipeline_t *vp) {
    vp->running = false;
    if (vp->thread) {
        /* Graceful shutdown: close socket to unblock recv() */
        if (vp->sock->fd != INVALID_SOCKFD) {
            shutdown(vp->sock->fd, SD_BOTH);
        }
        WaitForSingleObject(vp->thread, 3000);
        CloseHandle(vp->thread);
        vp->thread = NULL;
    }
}

void video_pipeline_destroy(video_pipeline_t *vp) {
    video_pipeline_stop(vp);
}
