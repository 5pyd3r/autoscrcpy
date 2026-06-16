#include "session.h"
#include "protocol.h"
#include "crypto.h"
#include "../platform/log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define ADB_BANNER "host::features=shell_v2"

SOCKET_T session_connect(const char *host, int port) {
    SOCKET_T fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKFD) {
        log_error("Failed to create socket");
        return INVALID_SOCKFD;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        log_error("Invalid host address: %s", host);
        CLOSESOCKET(fd);
        return INVALID_SOCKFD;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("Failed to connect to %s:%d", host, port);
        CLOSESOCKET(fd);
        return INVALID_SOCKFD;
    }

    SET_NONBLOCK(fd);
    return fd;
}

void session_send_cnxn(adb_connection_t *conn) {
    const char *banner = ADB_BANNER;
    int ret = adb_send_msg(conn->fd, ADB_CNXN, ADB_VERSION, ADB_MAX_PAYLOAD,
                           (const uint8_t *)banner, (uint32_t)(strlen(banner) + 1), 1);
    if (ret < 0) {
        log_error("Failed to send CNXN message");
    }
    conn->cnxn_sent = 1;
}

int session_start_auth(adb_connection_t *conn) {
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

    int ret = adb_send_msg(conn->fd, ADB_OPEN, chan->local_id, 0,
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
        adb_send_msg(conn->fd, ADB_CLSE, chan->local_id, chan->remote_id, NULL, 0, 1);
    }

    chan->state = CHAN_CLOSED;
}

void session_handle_message(adb_connection_t *conn, const adb_message_t *msg,
                            const uint8_t *payload) {
    switch (msg->command) {
        case ADB_CNXN:
            log_info("Connected to device: %s", payload);
            strncpy(conn->banner, (const char *)payload, BANNER_MAX - 1);
            conn->banner[BANNER_MAX - 1] = '\0';
            conn->state = ADB_STATE_CONNECTED;
            if (conn->on_connected) {
                conn->on_connected(conn);
            }
            break;

        case ADB_AUTH:
            if (msg->arg0 == ADB_AUTH_TYPE_TOKEN) {
                /* Sign token with RSA key */
                uint8_t sig[256];
                int sig_len;
                if (crypto_sign_token(payload, msg->data_length, sig, &sig_len) == 0) {
                    adb_send_msg(conn->fd, ADB_AUTH, ADB_AUTH_TYPE_RSAKEY, 0,
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
            adb_send_msg(conn->fd, ADB_OKAY, local_id, remote_id, NULL, 0, 1);
            break;
        }

        case ADB_OKAY: {
            uint32_t local_id = msg->arg0;
            uint32_t remote_id = msg->arg1;
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
                    adb_send_msg(conn->fd, ADB_OKAY, local_id, remote_id, NULL, 0, 1);
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
