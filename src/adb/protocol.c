#include "protocol.h"
#include "tls.h"
#include "../platform/log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

uint32_t adb_checksum(const uint8_t *data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

int adb_send_msg(SOCKET_T fd, uint32_t cmd, uint32_t arg0, uint32_t arg1,
                 const uint8_t *data, uint32_t data_len, int skip_checksum) {
    adb_message_t msg;
    msg.command = cmd;
    msg.arg0 = arg0;
    msg.arg1 = arg1;
    msg.data_length = data_len;
    msg.data_check = skip_checksum ? 0 : adb_checksum(data, data_len);
    msg.magic = cmd ^ 0xffffffff;

    uint8_t *buf = (uint8_t *)&msg;
    size_t total = ADB_MSG_HEADER_SIZE;
    size_t sent = 0;

    while (sent < total) {
        int n = send(fd, (const char *)buf + sent, (int)(total - sent), MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
            return -1;
        }
        sent += n;
    }

    if (data_len > 0 && data != NULL) {
        sent = 0;
        while (sent < data_len) {
            int n = send(fd, (const char *)data + sent, (int)(data_len - sent), MSG_NOSIGNAL);
            if (n <= 0) {
                if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
                return -1;
            }
            sent += n;
        }
    }

    return 0;
}

int adb_send_msg_tls(void *tls, SOCKET_T fd, uint32_t cmd, uint32_t arg0, uint32_t arg1,
                     const uint8_t *data, uint32_t data_len, int skip_checksum) {
    (void)fd; /* unused when TLS is provided */

    if (!tls) {
        return adb_send_msg(fd, cmd, arg0, arg1, data, data_len, skip_checksum);
    }

    adb_message_t msg;
    msg.command = cmd;
    msg.arg0 = arg0;
    msg.arg1 = arg1;
    msg.data_length = data_len;
    msg.data_check = (data && !skip_checksum) ? adb_checksum(data, data_len) : 0;
    msg.magic = cmd ^ 0xFFFFFFFF;

    /* Combine header + data into a single buffer for one SSL_write.
     * Separate writes cause mbedTLS to split into separate TLS records,
     * which can cause the receiver to not process the message correctly. */
    uint32_t total_len = ADB_MSG_HEADER_SIZE + data_len;
    uint8_t *wbuf = malloc(total_len);
    if (!wbuf) return -1;
    memcpy(wbuf, &msg, ADB_MSG_HEADER_SIZE);
    if (data && data_len > 0) memcpy(wbuf + ADB_MSG_HEADER_SIZE, data, data_len);

    uint32_t sent = 0;
    int retries = 0;
    while (sent < total_len && retries < 500) {
        int n = tls_send(tls, wbuf + sent, (int)(total_len - sent));
        if (n > 0) { sent += n; retries = 0; continue; }
        if (n < 0) { free(wbuf); return -1; }
        /* WANT_WRITE — sleep briefly and retry */
        Sleep(10);
        retries++;
    }
    free(wbuf);
    return (sent == total_len) ? 0 : -1;
}

int adb_recv_msg(SOCKET_T fd, adb_message_t *out_hdr, uint8_t *out_payload,
                 int max_payload, int skip_checksum) {
    /* Read header */
    uint8_t *buf = (uint8_t *)out_hdr;
    size_t total = ADB_MSG_HEADER_SIZE;
    size_t received = 0;

    while (received < total) {
        int n = recv(fd, buf + received, (int)(total - received), 0);
        if (n <= 0) {
            if (n < 0) {
                int err = SOCKET_ERRNO;
#ifdef _WIN32
                if (err == WSAETIMEDOUT) return -1; /* timeout */
#endif
                if (err == WOULDBLOCK_ERR) return -1;
            }
            return -1;
        }
        received += n;
    }

    /* Validate magic */
    if (out_hdr->magic != (out_hdr->command ^ 0xffffffff)) {
        log_error("Invalid ADB message magic");
        return -1;
    }

    /* Read payload if any */
    if (out_hdr->data_length > 0) {
        if ((int)out_hdr->data_length > max_payload) {
            log_error("ADB payload too large: %u > %d", out_hdr->data_length, max_payload);
            return -1;
        }

        received = 0;
        while (received < out_hdr->data_length) {
            int n = recv(fd, out_payload + received, (int)(out_hdr->data_length - received), 0);
            if (n <= 0) {
                if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) return -1;
                return -1;
            }
            received += n;
        }

        /* Validate checksum */
        if (!skip_checksum) {
            uint32_t check = adb_checksum(out_payload, out_hdr->data_length);
            if (check != out_hdr->data_check) {
                log_error("ADB checksum mismatch: expected %u, got %u", out_hdr->data_check, check);
                return -1;
            }
        }
    }

    return 1;
}

int adb_recv_msg_tls(void *tls, SOCKET_T fd, adb_message_t *out_hdr,
                     uint8_t *out_payload, int max_payload, int skip_checksum) {
    (void)fd; /* unused when TLS is provided */

    /* Read header via TLS — return 0 on WANT_READ so caller can retry */
    uint8_t *buf = (uint8_t *)out_hdr;
    size_t total = ADB_MSG_HEADER_SIZE;
    size_t received = 0;

    while (received < total) {
        int n = tls_recv(tls, buf + received, (int)(total - received));
        if (n < 0) return -1;
        if (n == 0) return 0; /* WANT_READ — let caller retry */
        received += n;
    }

    /* Validate magic */
    if (out_hdr->magic != (out_hdr->command ^ 0xffffffff)) {
        log_error("Invalid ADB message magic (TLS)");
        return -1;
    }

    /* Read payload if any */
    if (out_hdr->data_length > 0) {
        if ((int)out_hdr->data_length > max_payload) {
            log_error("ADB payload too large: %u > %d", out_hdr->data_length, max_payload);
            return -1;
        }

        received = 0;
        while (received < out_hdr->data_length) {
            int n = tls_recv(tls, out_payload + received, (int)(out_hdr->data_length - received));
            if (n < 0) return -1;
            if (n == 0) return 0; /* WANT_READ — let caller retry */
            received += n;
        }

        /* Validate checksum */
        if (!skip_checksum) {
            uint32_t check = adb_checksum(out_payload, out_hdr->data_length);
            if (check != out_hdr->data_check) {
                log_error("ADB checksum mismatch (TLS): expected %u, got %u",
                          out_hdr->data_check, check);
                return -1;
            }
        }
    }

    return 1;
}

int adb_send_msg_conn(adb_connection_t *conn, uint32_t cmd, uint32_t arg0,
                      uint32_t arg1, const uint8_t *data, uint32_t data_len,
                      int skip_checksum) {
    if (conn->tls_ctx) {
        return adb_send_msg_tls(conn->tls_ctx, conn->fd, cmd, arg0, arg1,
                                data, data_len, skip_checksum);
    }
    return adb_send_msg(conn->fd, cmd, arg0, arg1, data, data_len, skip_checksum);
}

int adb_recv_msg_conn(adb_connection_t *conn, adb_message_t *out_hdr,
                      uint8_t *out_payload, int max_payload, int skip_checksum) {
    if (conn->tls_ctx) {
        return adb_recv_msg_tls(conn->tls_ctx, conn->fd, out_hdr,
                                out_payload, max_payload, skip_checksum);
    }
    return adb_recv_msg(conn->fd, out_hdr, out_payload, max_payload, skip_checksum);
}
