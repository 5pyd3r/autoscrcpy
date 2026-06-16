#include "control_socket.h"
#include "../platform/log.h"
#include <stdlib.h>
#include <string.h>

bool control_socket_init(control_socket_t *sock, SOCKET_T fd) {
    sock->fd = fd;
    return true;
}

bool control_socket_accept(control_socket_t *sock, SOCKET_T listen_fd) {
    struct sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    SOCKET_T client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd == INVALID_SOCKFD) {
        log_error("Failed to accept control connection");
        return false;
    }

    uint8_t dummy;
    int n = recv(client_fd, (char *)&dummy, 1, 0);
    if (n != 1) {
        log_error("Failed to read control dummy byte");
        CLOSESOCKET(client_fd);
        return false;
    }

    sock->fd = client_fd;
    return true;
}

bool control_socket_send_msg(control_socket_t *sock, const uint8_t *data, uint32_t size) {
    // Send size header
    uint32_t net_size = htonl(size);
    size_t sent = 0;
    while (sent < 4) {
        int n = send(sock->fd, ((uint8_t *)&net_size) + sent, 4 - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
            log_error("Failed to send control message size");
            return false;
        }
        sent += n;
    }

    // Send data
    sent = 0;
    while (sent < size) {
        int n = send(sock->fd, data + sent, size - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
            log_error("Failed to send control message data");
            return false;
        }
        sent += n;
    }

    return true;
}

bool control_socket_recv_msg(control_socket_t *sock, uint8_t **data, uint32_t *size) {
    // Read size header
    uint32_t net_size;
    size_t received = 0;
    while (received < 4) {
        int n = recv(sock->fd, ((uint8_t *)&net_size) + received, 4 - received, 0);
        if (n <= 0) {
            if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
            log_error("Failed to receive control message size");
            return false;
        }
        received += n;
    }

    *size = ntohl(net_size);
    if (*size > 1024 * 1024) { // 1MB max
        log_error("Control message too large: %u", *size);
        return false;
    }

    // Allocate buffer
    *data = malloc(*size);
    if (!*data) {
        log_error("Failed to allocate control message buffer");
        return false;
    }

    // Read data
    received = 0;
    while (received < *size) {
        int n = recv(sock->fd, *data + received, *size - received, 0);
        if (n <= 0) {
            if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
            log_error("Failed to receive control message data");
            free(*data);
            return false;
        }
        received += n;
    }

    return true;
}

void control_socket_destroy(control_socket_t *sock) {
    if (sock->fd != INVALID_SOCKFD) {
        CLOSESOCKET(sock->fd);
        sock->fd = INVALID_SOCKFD;
    }
}
