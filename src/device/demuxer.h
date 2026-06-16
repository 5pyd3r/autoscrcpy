#ifndef DEMUXER_H
#define DEMUXER_H

#include <stdint.h>
#include <stdbool.h>
#include "../platform/platform.h"

#define SC_PACKET_HEADER_SIZE 12
#define SC_PACKET_FLAG_CONFIG    (UINT64_C(1) << 62)
#define SC_PACKET_FLAG_KEY_FRAME (UINT64_C(1) << 61)
#define SC_PACKET_PTS_MASK (SC_PACKET_FLAG_KEY_FRAME - 1)

/* Codec IDs from scrcpy protocol */
#define SC_CODEC_ID_H264 UINT32_C(0x68323634)
#define SC_CODEC_ID_H265 UINT32_C(0x68323635)
#define SC_CODEC_ID_AV1  UINT32_C(0x00617631)
#define SC_CODEC_ID_OPUS UINT32_C(0x6f707573)
#define SC_CODEC_ID_AAC  UINT32_C(0x00616163)
#define SC_CODEC_ID_FLAC UINT32_C(0x666c6163)
#define SC_CODEC_ID_RAW  UINT32_C(0x00726177)

typedef struct {
    uint8_t *data;
    uint32_t size;
    int64_t pts;
    bool is_config;
    bool is_key_frame;
} sc_packet_t;

/* Read codec ID (4 bytes) from socket */
bool demuxer_recv_codec_id(SOCKET_T fd, uint32_t *codec_id);

/* Read one packet from socket. Caller must free packet->data. */
bool demuxer_recv_packet(SOCKET_T fd, sc_packet_t *packet);

/* Free packet data */
void demuxer_packet_free(sc_packet_t *packet);

#endif /* DEMUXER_H */
