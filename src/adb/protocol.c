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

    fprintf(stderr, "DEBUG adb_send_msg: cmd=0x%08x fd=%d data_len=%u\n", cmd, (int)fd, data_len);
    fflush(stderr);

    /* Send header */
    uint8_t *buf = (uint8_t *)&msg;
    size_t total = ADB_MSG_HEADER_SIZE;
    size_t sent = 0;

    while (sent < total) {
        int n = send(fd, (const char *)buf + sent, (int)(total - sent), MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
            fprintf(stderr, "DEBUG adb_send_msg: header send failed, n=%d errno=%d\n", n, SOCKET_ERRNO);
            fflush(stderr);
            return -1;
        }
        sent += n;
    }
    fprintf(stderr, "DEBUG adb_send_msg: header sent %u bytes\n", (unsigned)sent);
    fflush(stderr);

    /* Send payload if any */
    if (data_len > 0 && data != NULL) {
        sent = 0;
        while (sent < data_len) {
            int n = send(fd, (const char *)data + sent, (int)(data_len - sent), MSG_NOSIGNAL);
            if (n <= 0) {
                if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
                fprintf(stderr, "DEBUG adb_send_msg: payload send failed\n");
                fflush(stderr);
                return -1;
            }
            sent += n;
        }
        fprintf(stderr, "DEBUG adb_send_msg: payload sent %u bytes\n", (unsigned)sent);
        fflush(stderr);
    }

    return 0;
}

int adb_send_msg_tls(void *tls, SOCKET_T fd, uint32_t cmd, uint32_t arg0, uint32_t arg1,
                     const uint8_t *data, uint32_t data_len, int skip_checksum) {
    (void)fd; /* unused when TLS is provided */
    adb_message_t msg;
    msg.command = cmd;
    msg.arg0 = arg0;
    msg.arg1 = arg1;
    msg.data_length = data_len;
    msg.data_check = skip_checksum ? 0 : adb_checksum(data, data_len);
    msg.magic = cmd ^ 0xffffffff;

    /* Send header via TLS */
    uint8_t *buf = (uint8_t *)&msg;
    size_t total = ADB_MSG_HEADER_SIZE;
    size_t sent = 0;

    while (sent < total) {
        int n = tls_send(tls, buf + sent, (int)(total - sent));
        if (n <= 0) {
            return -1;
        }
        sent += n;
    }

    /* Send payload if any */
    if (data_len > 0 && data != NULL) {
        sent = 0;
        while (sent < data_len) {
            int n = tls_send(tls, data + sent, (int)(data_len - sent));
            if (n <= 0) {
                return -1;
            }
            sent += n;
        }
    }

    return 0;
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

    /* Read header via TLS — retry on WANT_READ/WANT_WRITE (tls_recv returns 0) */
    uint8_t *buf = (uint8_t *)out_hdr;
    size_t total = ADB_MSG_HEADER_SIZE;
    size_t received = 0;

    while (received < total) {
        int n = tls_recv(tls, buf + received, (int)(total - received));
        if (n < 0) return -1;
        if (n == 0) continue; /* WANT_READ — retry */
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
            if (n == 0) continue; /* WANT_READ — retry */
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
        int ret = adb_send_msg_tls(conn->tls_ctx, conn->fd, cmd, arg0, arg1,
                                data, data_len, skip_checksum);
        if (ret < 0) fprintf(stderr, "DEBUG adb_send_msg_conn: TLS send failed for cmd=0x%08x\n", cmd);
        return ret;
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
