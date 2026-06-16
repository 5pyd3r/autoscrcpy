#ifndef CONTROL_SOCKET_H
#define CONTROL_SOCKET_H

#include <stdint.h>
#include <stdbool.h>
#include "../platform/platform.h"

typedef struct {
    SOCKET_T fd;
} control_socket_t;

bool control_socket_init(control_socket_t *sock, SOCKET_T fd);
bool control_socket_accept(control_socket_t *sock, SOCKET_T listen_fd);
bool control_socket_send_msg(control_socket_t *sock, const uint8_t *data, uint32_t size);
bool control_socket_recv_msg(control_socket_t *sock, uint8_t **data, uint32_t *size);
void control_socket_destroy(control_socket_t *sock);

#endif /* CONTROL_SOCKET_H */
