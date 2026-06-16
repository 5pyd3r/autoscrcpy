#ifndef AUDIO_SOCKET_H
#define AUDIO_SOCKET_H

#include <stdint.h>
#include <stdbool.h>
#include "../platform/platform.h"

typedef struct {
    SOCKET_T fd;
    uint32_t codec_id;
    uint32_t sample_rate;
    uint32_t channels;
} audio_socket_t;

bool audio_socket_init(audio_socket_t *sock, SOCKET_T fd);
bool audio_socket_accept(audio_socket_t *sock, SOCKET_T listen_fd);
bool audio_socket_read_packet(audio_socket_t *sock, uint8_t **data, uint32_t *size);
void audio_socket_destroy(audio_socket_t *sock);

#endif /* AUDIO_SOCKET_H */
