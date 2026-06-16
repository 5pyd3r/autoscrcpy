#ifndef VIDEO_SOCKET_H
#define VIDEO_SOCKET_H

#include <stdint.h>
#include <stdbool.h>
#include "../platform/platform.h"

typedef struct {
    SOCKET_T fd;
    uint32_t codec_id;
    uint32_t width;
    uint32_t height;
} video_socket_t;

bool video_socket_init(video_socket_t *sock, SOCKET_T fd);
bool video_socket_read_packet(video_socket_t *sock, uint8_t **data, uint32_t *size);
void video_socket_destroy(video_socket_t *sock);

#endif /* VIDEO_SOCKET_H */
