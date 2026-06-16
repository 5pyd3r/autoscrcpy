#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "adb.h"

/* Send an ADB message */
int adb_send_msg(SOCKET_T fd, uint32_t cmd, uint32_t arg0, uint32_t arg1,
                 const uint8_t *data, uint32_t data_len, int skip_checksum);

/* TLS-aware send */
int adb_send_msg_tls(void *tls, SOCKET_T fd, uint32_t cmd, uint32_t arg0, uint32_t arg1,
                     const uint8_t *data, uint32_t data_len, int skip_checksum);

/* Read ADB message */
int adb_recv_msg(SOCKET_T fd, adb_message_t *out_hdr, uint8_t *out_payload,
                 int max_payload, int skip_checksum);

/* TLS-aware recv */
int adb_recv_msg_tls(void *tls, SOCKET_T fd, adb_message_t *out_hdr,
                     uint8_t *out_payload, int max_payload, int skip_checksum);

/* Compute ADB payload checksum */
uint32_t adb_checksum(const uint8_t *data, uint32_t len);

#endif /* PROTOCOL_H */
