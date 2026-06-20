#include "audio_socket.h"
#include "../adb/binary.h"
#include "../platform/log.h"
#include <stdlib.h>
#include <string.h>

bool audio_socket_init(audio_socket_t *sock, SOCKET_T fd) {
    sock->fd = fd;
    sock->codec_id = 0;
    sock->sample_rate = 0;
    sock->channels = 0;
    return true;
}

bool audio_socket_accept(audio_socket_t *sock, SOCKET_T listen_fd) {
    struct sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    SOCKET_T client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd == INVALID_SOCKFD) {
        log_error("Failed to accept audio connection");
        return false;
    }

    uint8_t dummy;
    int n = recv(client_fd, (char *)&dummy, 1, 0);
    if (n != 1) {
        log_error("Failed to read audio dummy byte");
        CLOSESOCKET(client_fd);
        return false;
    }

    sock->fd = client_fd;
    sock->codec_id = 0;
    sock->sample_rate = 0;
    sock->channels = 0;
    return true;
}

bool audio_socket_read_metadata(audio_socket_t *sock) {
    /* scrcpy-server sends audio metadata as 4-byte codec_id (big-endian).
     * For audio, sample_rate and channels are hardcoded per scrcpy protocol:
     * Opus/AAC/FLAC: 48000 Hz, 2 channels (stereo). */
    uint8_t buf[4];
    size_t received = 0;
    while (received < 4) {
        int n = recv(sock->fd, (char *)buf + received, 4 - received, 0);
        if (n <= 0) {
            if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
            log_error("Failed to read audio metadata");
            return false;
        }
        received += n;
    }

    sock->codec_id = read32be(buf);

    if (sock->codec_id == 0) {
        log_warn("Audio stream disabled by server");
        return false;
    }
    if (sock->codec_id == 1) {
        log_error("Audio config error reported by server");
        return false;
    }

    /* scrcpy protocol hardcodes stereo 48kHz for all audio codecs */
    sock->sample_rate = 48000;
    sock->channels = 2;

    const char *name = "unknown";
    if (sock->codec_id == 0x6f707573) name = "Opus";
    else if (sock->codec_id == 0x00616163) name = "AAC";
    else if (sock->codec_id == 0x666c6163) name = "FLAC";
    log_info("Audio codec: %s (0x%08x), %u Hz, %u ch",
             name, sock->codec_id, sock->sample_rate, sock->channels);

    return true;
}

bool audio_socket_read_packet(audio_socket_t *sock, uint8_t **data, uint32_t *size) {
    // Read packet header (12 bytes: pts + size)
    uint8_t header[12];
    size_t received = 0;
    while (received < 12) {
        int n = recv(sock->fd, header + received, 12 - received, 0);
        if (n <= 0) {
            if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
            log_error("Failed to read audio packet header");
            return false;
        }
        received += n;
    }

    // Parse header (big-endian)
    uint64_t pts = read64be(header);
    uint32_t packet_size = read32be(header + 8);
    (void)pts; // Unused for now

#define MAX_AUDIO_PACKET_SIZE (1024 * 1024)  /* 1MB */

    if (packet_size > MAX_AUDIO_PACKET_SIZE) {
        log_error("Audio packet too large: %u bytes", packet_size);
        return false;
    }

    // Allocate buffer
    *data = malloc(packet_size);
    if (!*data) {
        log_error("Failed to allocate audio packet buffer");
        return false;
    }

    // Read packet data
    received = 0;
    while (received < packet_size) {
        int n = recv(sock->fd, *data + received, packet_size - received, 0);
        if (n <= 0) {
            if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
            log_error("Failed to read audio packet data");
            free(*data);
            return false;
        }
        received += n;
    }

    *size = packet_size;
    return true;
}

void audio_socket_destroy(audio_socket_t *sock) {
    if (sock->fd != INVALID_SOCKFD) {
        CLOSESOCKET(sock->fd);
        sock->fd = INVALID_SOCKFD;
    }
}
