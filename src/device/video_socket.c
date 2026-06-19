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
    sock->pending = NULL;
    sock->pending_size = 0;
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
    sock->pending = NULL;
    sock->pending_size = 0;
    return true;
}

static int recv_all(SOCKET_T fd, void *buf, int len) {
    int done = 0;
    while (done < len) {
        int n = recv(fd, (char *)buf + done, len - done, 0);
        if (n <= 0) {
            if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
            return -1;
        }
        done += n;
    }
    return 0;
}

/* Read `len` bytes, first from pending buffer, then from socket */
static int recv_all_pending(video_socket_t *sock, void *buf, int len) {
    int done = 0;
    /* Consume pending bytes first */
    if (sock->pending && sock->pending_size > 0) {
        int avail = (int)sock->pending_size;
        int take = avail < len ? avail : len;
        memcpy((uint8_t *)buf + done, sock->pending, take);
        done += take;
        /* Shift remaining pending */
        int remain = avail - take;
        if (remain > 0) {
            memmove(sock->pending, sock->pending + take, remain);
            sock->pending_size = (uint32_t)remain;
        } else {
            free(sock->pending);
            sock->pending = NULL;
            sock->pending_size = 0;
        }
    }
    /* Read remaining from socket */
    while (done < len) {
        int n = recv(sock->fd, (char *)buf + done, len - done, 0);
        if (n <= 0) {
            if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
            return -1;
        }
        done += n;
    }
    return 0;
}

bool video_socket_read_packet(video_socket_t *sock, uint8_t **data, uint32_t *size) {
    /* Read 12-byte packet header (big-endian):
     * bytes 0-7: PTS + flags (uint64 BE)
     *   bit 63: session flag (0x80 → header[0] & 0x80)
     *   bit 62: CONFIG flag (SPS/PPS)
     *   bit 61: KEY_FRAME flag
     *   bits 60-0: PTS value
     * bytes 8-11: payload size (uint32 BE)
     *
     * Session packets (bit 63 set): 12 bytes total, no payload.
     *   bytes 4-7: video width (big-endian)
     *   bytes 8-11: video height (big-endian) */
    uint8_t header[12];
    if (recv_all_pending(sock, header, 12) < 0) {
        log_error("Failed to read video packet header");
        return false;
    }

    /* Check for session packet (bit 63 set → header[0] & 0x80).
     * Session packets are header-only (12 bytes, no payload).
     * They contain video width/height for dynamic resize events. */
    if (header[0] & 0x80) {
        uint32_t w = read32be(header + 4);
        uint32_t h = read32be(header + 8);
        if (w > 0 && h > 0) {
            sock->width = w;
            sock->height = h;
            log_info("Session: %ux%u", w, h);
        }
        *data = NULL;
        *size = 0;
        return true;
    }

    /* Media packet: parse PTS and size (big-endian) */
    uint64_t pts_raw = ((uint64_t)header[0] << 56) | ((uint64_t)header[1] << 48) |
                       ((uint64_t)header[2] << 40) | ((uint64_t)header[3] << 32) |
                       ((uint64_t)header[4] << 24) | ((uint64_t)header[5] << 16) |
                       ((uint64_t)header[6] << 8) | (uint64_t)header[7];
    bool is_config = (pts_raw & (UINT64_C(1) << 62)) != 0;
    (void)is_config;
    uint32_t packet_size = read32be(header + 8);

    if (packet_size == 0) {
        *data = NULL;
        *size = 0;
        return true;
    }

    /* Sanity check: if the packet size is unreasonably large (>10MB),
     * the header might be misaligned (raw Annex B data). */
    if (packet_size > 10 * 1024 * 1024) {
        log_error("Video packet too large: %u bytes — possible header misalignment", packet_size);
        *data = NULL;
        *size = 0;
        return false;
    }

    /* Allocate and read payload */
    *data = malloc(packet_size);
    if (!*data) {
        log_error("Failed to allocate video packet buffer (%u bytes)", packet_size);
        return false;
    }

    if (recv_all_pending(sock, *data, packet_size) < 0) {
        log_error("Failed to read video packet data");
        free(*data);
        *data = NULL;
        return false;
    }

    *size = packet_size;
    return true;
}

void video_socket_destroy(video_socket_t *sock) {
    if (sock->pending) {
        free(sock->pending);
        sock->pending = NULL;
        sock->pending_size = 0;
    }
    if (sock->fd != INVALID_SOCKFD) {
        CLOSESOCKET(sock->fd);
        sock->fd = INVALID_SOCKFD;
    }
}
