#include "session.h"
#include "protocol.h"
#include "crypto.h"
#include "tls.h"
#include "../platform/log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define ADB_BANNER "host::features=shell_v2"

SOCKET_T session_connect(const char *host, int port) {
    SOCKET_T fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKFD) return INVALID_SOCKFD;

    /* Non-blocking connect (reference implementation approach) */
    SET_NONBLOCK(fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    int ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        int err = SOCKET_ERRNO;
        if (err != INPROGRESS_ERR && err != WOULDBLOCK_ERR) {
            CLOSESOCKET(fd);
            return INVALID_SOCKFD;
        }
    }

    /* Wait for connect to complete with 5s timeout */
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    struct timeval tv = {5, 0};
    ret = select(0, NULL, &wfds, NULL, &tv);
    if (ret <= 0) {
        CLOSESOCKET(fd);
        return INVALID_SOCKFD;
    }

    /* Restore blocking mode */
    SET_BLOCK(fd);
    return fd;
}

void session_send_cnxn(adb_connection_t *conn) {
    if (conn->cnxn_sent) return;
    conn->cnxn_sent = 1;
    conn->state = ADB_STATE_AUTH_SENT;
    uint32_t blen = (uint32_t)strlen(ADB_BANNER) + 1; /* include null terminator */
    /* Use TLS-aware send — dispatches to TLS if conn->tls_ctx is set */
    adb_send_msg_conn(conn, ADB_CNXN, ADB_VERSION, ADB_MAX_PAYLOAD,
                      (const uint8_t *)ADB_BANNER, blen, 1);
}

int session_start_auth(adb_connection_t *conn) {
    /* Send CNXN to initiate the handshake.
     * The device will respond with STLS (TLS required) or CNXN/AUTH. */
    session_send_cnxn(conn);
    return 0;
}

adb_channel_t *session_open_channel(adb_connection_t *conn, const char *service) {
    if (conn->channel_count >= MAX_CHANNELS) {
        log_error("Too many open channels");
        return NULL;
    }

    adb_channel_t *chan = &conn->channels[conn->channel_count++];
    chan->local_id = conn->next_local_id++;
    chan->remote_id = 0;
    strncpy(chan->service, service, SERVICE_NAME_MAX - 1);
    chan->service[SERVICE_NAME_MAX - 1] = '\0';
    chan->state = CHAN_OPENING;
    chan->local_fd = INVALID_SOCKFD;

    int ret = adb_send_msg_conn(conn, ADB_OPEN, chan->local_id, 0,
                           (const uint8_t *)service, (uint32_t)(strlen(service) + 1), 1);
    if (ret < 0) {
        log_error("Failed to send OPEN message");
        conn->channel_count--;
        return NULL;
    }

    return chan;
}

void session_close_channel(adb_connection_t *conn, adb_channel_t *chan) {
    if (chan->state == CHAN_CLOSED) return;

    if (chan->remote_id != 0) {
        adb_send_msg_conn(conn, ADB_CLSE, chan->local_id, chan->remote_id, NULL, 0, 1);
    }

    chan->state = CHAN_CLOSED;
}

void session_handle_message(adb_connection_t *conn, const adb_message_t *msg,
                            const uint8_t *payload) {
    log_info("ADB msg: cmd=0x%08x arg0=%u arg1=%u data_len=%u",
             msg->command, msg->arg0, msg->arg1, msg->data_length);
    switch (msg->command) {
        case ADB_CNXN:
            log_info("Device CNXN: banner=%s, version=0x%08x, max_payload=%u",
                     payload, msg->arg0, msg->arg1);
            if (msg->data_length > 0 && payload) {
                int cl = (int)msg->data_length;
                if (cl >= BANNER_MAX) cl = BANNER_MAX - 1;
                memcpy(conn->banner, payload, cl);
                conn->banner[cl] = '\0';
            }
            conn->protocol_version = (int)(msg->arg0 < (uint32_t)ADB_VERSION
                                           ? msg->arg0 : (uint32_t)ADB_VERSION);
            conn->max_payload = (size_t)(msg->arg1 < (uint32_t)ADB_MAX_PAYLOAD
                                         ? msg->arg1 : (uint32_t)ADB_MAX_PAYLOAD);
            conn->state = ADB_STATE_CONNECTED;
            if (conn->on_connected) {
                conn->on_connected(conn);
            }
            break;

        case ADB_STLS:
            if (conn->tls_ctx) {
                /* Already in TLS — adbd re-sent STLS after receiving our CNXN.
                 * Per reference implementation, ignore to avoid corrupting session. */
                log_info("Post-CNXN STLS ignored (already in TLS)");
                break;
            }
            /* First STLS — initiate TLS upgrade */
            log_info("Server requested TLS (version=0x%08x)", msg->arg0);
            conn->state = ADB_STATE_TLS_NEGOTIATING;
            /* Reply STLS before TLS handshake — skip_checksum=0 per reference */
            adb_send_msg_conn(conn, ADB_STLS, ADB_STLS_VERSION, 0, NULL, 0, 0);
            conn->stls_sent = 1;

            /* TLS handshake (blocking) */
            conn->tls_ctx = tls_handshake(conn->fd);
            if (!conn->tls_ctx) {
                log_error("TLS handshake failed");
                break;
            }
            log_info("TLS handshake successful");

            /* Blocking CNXN exchange — read adbd's CNXN, handle AUTH tokens.
             * Do NOT send CNXN reply: sending CNXN triggers adbd's handle_offline
             * which sets t->online=false, causing subsequent OPEN to be rejected.
             * adbd stays "online" from adbd_auth_verified, so OPEN works.
             * NOTE: Use heap allocation for rpl to avoid 1MB stack overflow. */
            {
                uint8_t *rpl = malloc(ADB_MAX_PAYLOAD);
                if (!rpl) { log_error("Out of memory"); break; }
                for (int t = 0; t < 50 && conn->state != ADB_STATE_CONNECTED; t++) {
                    adb_message_t rm;
                    memset(&rm, 0, sizeof(rm));
                    int ret = adb_recv_msg_conn(conn, &rm, rpl, ADB_MAX_PAYLOAD, 1);
                    if (ret < 0) {
                        platform_sleep_ms(100);
                        continue;
                    }

                    if (rm.command == ADB_AUTH && rm.arg0 == ADB_AUTH_TYPE_TOKEN) {
                        /* Sign AUTH token */
                        uint8_t sig[512]; int sl = 0;
                        if (crypto_sign_token(rpl, rm.data_length, sig, &sl) == 0) {
                            adb_send_msg_conn(conn, ADB_AUTH, ADB_AUTH_TYPE_RSAKEY, 0,
                                              sig, (uint32_t)sl, 1);
                        }
                        continue;
                    }

                    if (rm.command == ADB_STLS) {
                        /* adbd re-sent STLS — reply and continue */
                        adb_send_msg_conn(conn, ADB_STLS, ADB_STLS_VERSION, 0, NULL, 0, 0);
                        continue;
                    }

                    if (rm.command == ADB_CNXN) {
                        conn->protocol_version = (int)(rm.arg0 < (uint32_t)ADB_VERSION
                                                       ? rm.arg0 : (uint32_t)ADB_VERSION);
                        conn->max_payload = (size_t)(rm.arg1 < (uint32_t)ADB_MAX_PAYLOAD
                                                     ? rm.arg1 : (uint32_t)ADB_MAX_PAYLOAD);
                        conn->state = ADB_STATE_CONNECTED;
                        if (rm.data_length > 0) {
                            int cl = (int)rm.data_length;
                            if (cl >= BANNER_MAX) cl = BANNER_MAX - 1;
                            memcpy(conn->banner, rpl, cl);
                            conn->banner[cl] = '\0';
                        }
                        log_info("ADB connected (TLS): %s", conn->banner);
                        /* Do NOT send CNXN reply — see comment above */
                        break;
                    }
                }
                free(rpl);
            }
            if (conn->state != ADB_STATE_CONNECTED) {
                log_error("TLS CNXN exchange failed");
            }
            break;

        case ADB_AUTH:
            if (msg->arg0 == ADB_AUTH_TYPE_TOKEN) {
                /* Sign token with RSA key */
                uint8_t sig[256];
                int sig_len;
                if (crypto_sign_token(payload, msg->data_length, sig, &sig_len) == 0) {
                    adb_send_msg_conn(conn, ADB_AUTH, ADB_AUTH_TYPE_RSAKEY, 0,
                                 sig, (uint32_t)sig_len, 1);
                    conn->state = ADB_STATE_AUTH_SENT;
                }
            }
            break;

        case ADB_OPEN: {
            /* Find channel by local_id */
            uint32_t remote_id = msg->arg0;
            uint32_t local_id = msg->arg1;
            for (int i = 0; i < conn->channel_count; i++) {
                if (conn->channels[i].local_id == local_id) {
                    conn->channels[i].remote_id = remote_id;
                    conn->channels[i].state = CHAN_OPEN;
                    break;
                }
            }
            /* Send OKAY */
            adb_send_msg_conn(conn, ADB_OKAY, local_id, remote_id, NULL, 0, 1);
            break;
        }

        case ADB_OKAY: {
            /* ADB OKAY: arg0 = remote (device) local_id, arg1 = local (client) local_id */
            uint32_t remote_id = msg->arg0;
            uint32_t local_id = msg->arg1;
            for (int i = 0; i < conn->channel_count; i++) {
                if (conn->channels[i].local_id == local_id) {
                    conn->channels[i].remote_id = remote_id;
                    conn->channels[i].state = CHAN_OPEN;
                    break;
                }
            }
            break;
        }

        case ADB_WRTE: {
            uint32_t remote_id = msg->arg0;
            uint32_t local_id = msg->arg1;
            for (int i = 0; i < conn->channel_count; i++) {
                if (conn->channels[i].local_id == local_id &&
                    conn->channels[i].remote_id == remote_id) {
                    if (conn->on_shell_output) {
                        conn->on_shell_output(payload, msg->data_length,
                                              conn->on_shell_output_arg);
                    }
                    /* Send OKAY to acknowledge */
                    adb_send_msg_conn(conn, ADB_OKAY, local_id, remote_id, NULL, 0, 1);
                    break;
                }
            }
            break;
        }

        case ADB_CLSE: {
            uint32_t local_id = msg->arg0;
            for (int i = 0; i < conn->channel_count; i++) {
                if (conn->channels[i].local_id == local_id) {
                    conn->channels[i].state = CHAN_CLOSED;
                    break;
                }
            }
            break;
        }

        default:
            log_warn("Unknown ADB command: 0x%08x", msg->command);
            break;
    }
}

void session_disconnect(adb_connection_t *conn) {
    for (int i = 0; i < conn->channel_count; i++) {
        session_close_channel(conn, &conn->channels[i]);
    }

    if (conn->fd != INVALID_SOCKFD) {
        CLOSESOCKET(conn->fd);
        conn->fd = INVALID_SOCKFD;
    }

    conn->state = ADB_STATE_DISCONNECTED;
}

int session_recv_msg(adb_connection_t *conn, adb_message_t *out_hdr,
                     uint8_t *out_payload, int max_payload) {
    return adb_recv_msg_conn(conn, out_hdr, out_payload, max_payload,
                             conn->protocol_version >= ADB_VERSION_SKIP_CHECKSUM);
}

int session_poll(adb_connection_t *conn, int timeout_ms) {
    if (conn->fd == INVALID_SOCKFD) return -1;

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(conn->fd, &read_fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(0, &read_fds, NULL, NULL, &tv);
    if (ret <= 0) return ret;

    adb_message_t hdr;
    uint8_t *payload = malloc(ADB_MAX_PAYLOAD);
    if (!payload) return -1;
    int skip = conn->protocol_version >= ADB_VERSION_SKIP_CHECKSUM;
    int n = adb_recv_msg_conn(conn, &hdr, payload, ADB_MAX_PAYLOAD, skip);
    if (n < 0) { free(payload); return -1; }

    session_handle_message(conn, &hdr, payload);
    free(payload);
    return 1;
}
