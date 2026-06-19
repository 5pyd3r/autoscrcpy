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

/* Send control message to scrcpy-server.
 * scrcpy protocol: raw bytes, no size header. Type(1) + data directly. */
bool control_socket_send_msg(control_socket_t *sock, const uint8_t *data, uint32_t size) {
    if (sock->fd == INVALID_SOCKFD) return false;
    size_t sent = 0;
    while (sent < size) {
        int n = send(sock->fd, (const char *)(data + sent), size - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
            log_error("Failed to send control message");
            return false;
        }
        sent += n;
    }
    return true;
}

/* Receive control message from scrcpy-server.
 * scrcpy protocol: first byte = type, then type-specific data.
 * We read the type byte first, then determine the remaining size. */
bool control_socket_recv_msg(control_socket_t *sock, uint8_t **data, uint32_t *size) {
    /* Read type byte */
    uint8_t type_byte;
    int n = recv(sock->fd, (char *)&type_byte, 1, 0);
    if (n <= 0) {
        if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) return false;
        log_error("Failed to receive control message type");
        return false;
    }

    /* Determine payload size based on type */
    uint32_t payload_size;
    switch (type_byte) {
        case 0:  payload_size = 0; break; /* CLIPBOARD — variable, read header */
        case 1:  payload_size = 8; break; /* ACK_CLIPBOARD */
        case 2:  payload_size = 4; break; /* UHID_OUTPUT — variable, read header */
        default: payload_size = 0; break;
    }

    /* For variable-size messages, read the length header */
    if (type_byte == 0) { /* CLIPBOARD */
        uint8_t len_buf[4];
        if (recv(sock->fd, (char *)len_buf, 4, 0) != 4) return false;
        payload_size = ((uint32_t)len_buf[0] << 24) | ((uint32_t)len_buf[1] << 16) |
                       ((uint32_t)len_buf[2] << 8) | (uint32_t)len_buf[3];
    } else if (type_byte == 2) { /* UHID_OUTPUT */
        uint8_t hdr[4];
        if (recv(sock->fd, (char *)hdr, 4, 0) != 4) return false;
        payload_size = ((uint32_t)hdr[2] << 8) | (uint32_t)hdr[3]; /* size field */
    }

    /* Allocate and read */
    *size = 1 + payload_size; /* type + payload */
    *data = malloc(*size);
    if (!*data) return false;
    (*data)[0] = type_byte;

    if (payload_size > 0) {
        int received = 0;
        while (received < (int)payload_size) {
            n = recv(sock->fd, (char *)(*data + 1 + received), payload_size - received, 0);
            if (n <= 0) { free(*data); *data = NULL; return false; }
            received += n;
        }
    }

    return true;
}

void control_socket_destroy(control_socket_t *sock) {
    if (sock->fd != INVALID_SOCKFD) {
        CLOSESOCKET(sock->fd);
        sock->fd = INVALID_SOCKFD;
    }
}
