#include "demuxer.h"
#include "../adb/binary.h"
#include "../platform/log.h"
#include <stdlib.h>
#include <string.h>

bool demuxer_recv_codec_id(SOCKET_T fd, uint32_t *codec_id) {
    uint8_t data[4];
    size_t received = 0;
    while (received < 4) {
        int n = recv(fd, (char *)(data + received), 4 - (int)received, 0);
        if (n <= 0) {
            if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
            log_error("Failed to recv codec id");
            return false;
        }
        received += n;
    }
    *codec_id = read32be(data);
    return true;
}

bool demuxer_recv_packet(SOCKET_T fd, sc_packet_t *packet) {
    /* Read 12-byte header */
    uint8_t header[SC_PACKET_HEADER_SIZE];
    size_t received = 0;
    while (received < SC_PACKET_HEADER_SIZE) {
        int n = recv(fd, (char *)(header + received),
                     SC_PACKET_HEADER_SIZE - (int)received, 0);
        if (n <= 0) {
            if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
            log_error("Failed to recv packet header");
            return false;
        }
        received += n;
    }

    /* Parse header:
     * bytes 0-7: PTS (8 bytes, big-endian)
     * bytes 8-11: size + flags (4 bytes, big-endian)
     *
     * If MSB (bit 63) is set: session/config packet (size is in lower 31 bits)
     * If bit 62 is set: keyframe
     * Otherwise: normal data packet
     */
    uint64_t pts_raw = ((uint64_t)header[0] << 56) | ((uint64_t)header[1] << 48) |
                       ((uint64_t)header[2] << 40) | ((uint64_t)header[3] << 32) |
                       ((uint64_t)header[4] << 24) | ((uint64_t)header[5] << 16) |
                       ((uint64_t)header[6] << 8) | (uint64_t)header[7];

    uint32_t size_field = read32be(header + 8);

    /* Check if this is a session/config packet (bit 63 set) */
    bool is_config = (pts_raw & SC_PACKET_FLAG_CONFIG) != 0;
    bool is_key_frame = (pts_raw & SC_PACKET_FLAG_KEY_FRAME) != 0;

    /* Extract PTS (lower 61 bits) */
    int64_t pts = (int64_t)(pts_raw & SC_PACKET_PTS_MASK);

    /* For config packets, size is in the lower 31 bits of size_field */
    uint32_t payload_size;
    if (is_config) {
        payload_size = size_field & 0x7FFFFFFF;
    } else {
        payload_size = size_field;
    }

    /* Allocate and read payload */
    packet->data = NULL;
    packet->size = 0;
    packet->pts = pts;
    packet->is_config = is_config;
    packet->is_key_frame = is_key_frame;

    if (payload_size > 0) {
        packet->data = malloc(payload_size);
        if (!packet->data) {
            log_error("Failed to allocate packet buffer (%u bytes)", payload_size);
            return false;
        }

        received = 0;
        while (received < payload_size) {
            int n = recv(fd, (char *)(packet->data + received),
                         (int)(payload_size - received), 0);
            if (n <= 0) {
                if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
                log_error("Failed to recv packet payload");
                free(packet->data);
                packet->data = NULL;
                return false;
            }
            received += n;
        }
        packet->size = payload_size;
    }

    return true;
}

void demuxer_packet_free(sc_packet_t *packet) {
    if (packet->data) {
        free(packet->data);
        packet->data = NULL;
    }
    packet->size = 0;
}
