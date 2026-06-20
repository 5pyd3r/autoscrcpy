#ifndef AUDIO_PIPELINE_H
#define AUDIO_PIPELINE_H

#include "../decode/audio_decoder.h"
#include "../audio/player.h"
#include "../device/audio_socket.h"
#include <stdbool.h>
#include <windows.h>

typedef struct {
    audio_decoder_t *decoder;
    audio_player_t *player;
    audio_socket_t *sock;
    HANDLE thread;
    volatile bool running;
} audio_pipeline_t;

bool audio_pipeline_init(audio_pipeline_t *ap, audio_decoder_t *decoder,
                         audio_player_t *player, audio_socket_t *sock);
bool audio_pipeline_start(audio_pipeline_t *ap);
void audio_pipeline_stop(audio_pipeline_t *ap);
void audio_pipeline_destroy(audio_pipeline_t *ap);

#endif /* AUDIO_PIPELINE_H */
