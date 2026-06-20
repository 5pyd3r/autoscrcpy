#include "audio_pipeline.h"
#include "../platform/log.h"
#include <string.h>
#include <stdlib.h>

static DWORD WINAPI audio_thread_func(LPVOID arg) {
    audio_pipeline_t *ap = (audio_pipeline_t *)arg;

    while (ap->running) {
        /* 1. Read one audio packet */
        uint8_t *data = NULL;
        uint32_t size = 0;
        if (!audio_socket_read_packet(ap->sock, &data, &size)) {
            if (ap->running) log_error("Audio socket read failed");
            break;
        }

        /* 2. Decode */
        audio_frame_t aframe;
        memset(&aframe, 0, sizeof(aframe));
        if (!audio_decoder_decode(ap->decoder, data, size, &aframe)) {
            free(data);
            continue;  /* skip on decode failure */
        }
        free(data);

        /* 3. Write to WASAPI */
        if (aframe.data) {
            audio_player_write(ap->player, aframe.data, aframe.size);
            free(aframe.data);
        }
    }
    return 0;
}

bool audio_pipeline_init(audio_pipeline_t *ap, audio_decoder_t *decoder,
                         audio_player_t *player, audio_socket_t *sock) {
    ap->decoder = decoder;
    ap->player = player;
    ap->sock = sock;
    ap->thread = NULL;
    ap->running = false;
    return true;
}

bool audio_pipeline_start(audio_pipeline_t *ap) {
    ap->running = true;
    ap->thread = CreateThread(NULL, 0, audio_thread_func, ap, 0, NULL);
    if (!ap->thread) {
        log_error("Failed to create audio thread");
        ap->running = false;
        return false;
    }
    return true;
}

void audio_pipeline_stop(audio_pipeline_t *ap) {
    ap->running = false;
    if (ap->thread) {
        /* Graceful shutdown: close socket to unblock recv() */
        if (ap->sock->fd != INVALID_SOCKFD) {
            shutdown(ap->sock->fd, SD_BOTH);
        }
        WaitForSingleObject(ap->thread, 2000);
        CloseHandle(ap->thread);
        ap->thread = NULL;
    }
}

void audio_pipeline_destroy(audio_pipeline_t *ap) {
    audio_pipeline_stop(ap);
}
