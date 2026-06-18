#include "video_socket.h"
#include "../adb/binary.h"
#include "../platform/log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

bool video_socket_init(video_socket_t *sock, SOCKET_T fd) {
    sock->fd = fd;
    sock->codec_id = 0;
    sock->width = 0;
    sock->height = 0;
    return true;
}

bool video_socket_accept(video_socket_t *sock, SOCKET_T listen_fd) {
    struct sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    SOCKET_T client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd == INVALID_SOCKFD) {
        log_error("Failed to accept video connection");
        return false;
    }

    uint8_t dummy;
    int n = recv(client_fd, (char *)&dummy, 1, 0);
    if (n != 1) {
        log_error("Failed to read video dummy byte");
        CLOSESOCKET(client_fd);
        return false;
    }

    sock->fd = client_fd;
    sock->codec_id = 0;
    sock->width = 0;
    sock->height = 0;
    return true;
}

bool video_socket_read_packet(video_socket_t *sock, uint8_t **data, uint32_t *size) {
    /* Read 12-byte packet header (big-endian):
     * bytes 0-7: PTS + flags (uint64 BE)
     *   bit 63: 0 (media packet marker)
     *   bit 62: CONFIG flag (SPS/PPS)
     *   bit 61: KEY_FRAME flag
     *   bits 60-0: PTS value
     * bytes 8-11: payload size (uint32 BE)
     *
     * Session packets (bit 7 of byte 0 set): 12 bytes total, no payload */
    uint8_t header[12];
    size_t received = 0;
    while (received < 12) {
        int n = recv(sock->fd, (char *)(header + received), 12 - (int)received, 0);
        if (n <= 0) {
            if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
            log_error("Failed to read video packet header");
            return false;
        }
        received += n;
    }

    /* Check for session packet (bit 7 of byte 0 = 1) */
    if (header[0] & 0x80) {
        /* Session packet — in scrcpy 3.3.2, has a payload (codec config).
         * bytes 8-11 = payload size (BE). Read and return the payload. */
        uint32_t session_size = read32be(header + 8);
        if (session_size == 0 || session_size > 10 * 1024 * 1024) {
            *data = NULL;
            *size = 0;
            return true;
        }
        *data = malloc(session_size);
        if (!*data) return false;
        received = 0;
        while (received < session_size) {
            int n = recv(sock->fd, (char *)(*data + received), (int)(session_size - received), 0);
            if (n <= 0) { free(*data); *data = NULL; return false; }
            received += n;
        }
        *size = session_size;
        return true;
    }

    /* Media packet: parse PTS and size (big-endian) */
    uint32_t packet_size = read32be(header + 8);

    if (packet_size == 0) {
        *data = NULL;
        *size = 0;
        return true;
    }

    if (packet_size > 10 * 1024 * 1024) {
        log_error("Video packet too large: %u bytes", packet_size);
        return false;
    }

    /* Allocate and read payload */
    *data = malloc(packet_size);
    if (!*data) {
        log_error("Failed to allocate video packet buffer (%u bytes)", packet_size);
        return false;
    }

    received = 0;
    while (received < packet_size) {
        int n = recv(sock->fd, (char *)(*data + received), (int)(packet_size - received), 0);
        if (n <= 0) {
            if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
            log_error("Failed to read video packet data");
            free(*data);
            *data = NULL;
            return false;
        }
        received += n;
    }

    *size = packet_size;
    return true;
}

void video_socket_destroy(video_socket_t *sock) {
    if (sock->fd != INVALID_SOCKFD) {
        CLOSESOCKET(sock->fd);
        sock->fd = INVALID_SOCKFD;
    }
}
