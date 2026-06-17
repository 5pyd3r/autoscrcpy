#include "server.h"
#include "../adb/adb.h"
#include "../adb/protocol.h"
#include "../adb/session.h"
#include "../adb/crypto.h"
#include "../adb/tls.h"
#include "../adb/binary.h"
#include "../platform/log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

bool server_init(server_t *srv, const struct server_config *config) {
    srv->config = *config;
    srv->listen_fd = INVALID_SOCKFD;
    srv->adb_conn = NULL;
    srv->running = false;
    return true;
}

static bool parse_serial(const char *serial, char *host, int host_size,
                         uint16_t *port) {
    if (!serial || !serial[0]) return false;
    const char *colon = strrchr(serial, ':');
    if (colon) {
        int host_len = (int)(colon - serial);
        if (host_len <= 0 || host_len >= host_size) return false;
        memcpy(host, serial, host_len);
        host[host_len] = '\0';
        *port = (uint16_t)atoi(colon + 1);
    } else {
        snprintf(host, host_size, "%s", serial);
        *port = 5555;
    }
    return true;
}

/* Connect directly to device adbd and do CNXN/STLS/TLS handshake.
 * Returns an adb_connection_t* on success, NULL on failure.
 * All logic is inline to avoid cross-module linker issues. */
static adb_connection_t *do_adb_connect(const char *host, uint16_t port) {
    adb_connection_t *conn = calloc(1, sizeof(adb_connection_t));
    if (!conn) return NULL;
    conn->next_local_id = 1;
    conn->max_payload = ADB_MAX_PAYLOAD;

    /* TCP connect (blocking) */
    SOCKET_T fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKFD) { free(conn); return NULL; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("TCP connect to %s:%u failed", host, port);
        CLOSESOCKET(fd);
        free(conn);
        return NULL;
    }
    conn->fd = fd;
    log_info("TCP connected to %s:%u", host, port);

    /* Send CNXN */
    {
        uint8_t pkt[48];
        memset(pkt, 0, 48);
        *(uint32_t *)(pkt + 0)  = ADB_CNXN;
        *(uint32_t *)(pkt + 4)  = ADB_VERSION;
        *(uint32_t *)(pkt + 8)  = ADB_MAX_PAYLOAD;
        *(uint32_t *)(pkt + 12) = 24; /* banner len + null */
        *(uint32_t *)(pkt + 16) = 0;
        *(uint32_t *)(pkt + 20) = ADB_CNXN ^ 0xffffffff;
        memcpy(pkt + 24, "host::features=shell_v2", 24);
        send(fd, (const char *)pkt, 48, 0);
    }

    /* Read device response(s) */
    for (int tries = 0; tries < 100 && conn->state != ADB_STATE_CONNECTED; tries++) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv = {1, 0};
        if (select(0, &rfds, NULL, NULL, &tv) <= 0) continue;

        uint8_t hdr[24];
        int got = 0;
        while (got < 24) {
            int n = recv(fd, (char *)hdr + got, 24 - got, 0);
            if (n <= 0) goto fail;
            got += n;
        }

        uint32_t cmd  = *(uint32_t *)(hdr + 0);
        uint32_t arg0 = *(uint32_t *)(hdr + 4);
        uint32_t dlen = *(uint32_t *)(hdr + 12);
        uint32_t mag  = *(uint32_t *)(hdr + 20);
        if (mag != (cmd ^ 0xffffffff)) goto fail;

        uint8_t payload[4096];
        if (dlen > 0 && dlen <= sizeof(payload)) {
            got = 0;
            while (got < (int)dlen) {
                int n = recv(fd, (char *)payload + got, dlen - got, 0);
                if (n <= 0) goto fail;
                got += n;
            }
        }

        if (cmd == ADB_STLS) {
            log_info("adbd requires TLS");
            /* Reply STLS — use adb_send_msg like reference */
            adb_send_msg(fd, ADB_STLS, 0x01000000, 0, NULL, 0, 0);

            conn->tls_ctx = tls_handshake(fd);
            if (!conn->tls_ctx) { log_error("TLS handshake failed"); goto fail; }
            log_info("TLS handshake OK");

            /* Per reference: after TLS handshake, device speaks first.
             * Wait for device AUTH/CNXN before sending our CNXN. */
            log_info("Waiting for device to speak first after TLS...");
            for (int t2 = 0; t2 < 50 && conn->state != ADB_STATE_CONNECTED; t2++) {
                adb_message_t rmsg;
                uint8_t rpl[4096];
                int ret2 = adb_recv_msg_conn(conn, &rmsg, rpl, sizeof(rpl), 1);
                if (ret2 < 0) { Sleep(100); continue; }

                log_info("TLS post-handshake: cmd=0x%08x arg0=0x%08x arg1=0x%08x dlen=%u",
                         rmsg.command, rmsg.arg0, rmsg.arg1, rmsg.data_length);

                if (rmsg.command == ADB_AUTH && rmsg.arg0 == ADB_AUTH_TYPE_TOKEN) {
                    /* Device sent AUTH token — sign it */
                    uint8_t sig2[512]; int sl2 = 0;
                    if (crypto_sign_token(rpl, rmsg.data_length, sig2, &sl2) == 0) {
                        adb_send_msg_conn(conn, ADB_AUTH, ADB_AUTH_TYPE_RSAKEY, 0,
                                          sig2, (uint32_t)sl2, 1);
                    }
                } else if (rmsg.command == ADB_STLS) {
                    /* adbd re-sent STLS — reply and continue */
                    adb_send_msg_conn(conn, ADB_STLS, ADB_STLS_VERSION, 0, NULL, 0, 0);
                } else if (rmsg.command == ADB_CNXN) {
                    /* Device sent CNXN — parse banner. Do NOT send CNXN reply:
                     * sending CNXN triggers adbd's handle_offline → t->online=false,
                     * causing subsequent OPEN to be rejected. */
                    conn->protocol_version = (int)(rmsg.arg0 < (uint32_t)ADB_VERSION
                                                   ? rmsg.arg0 : (uint32_t)ADB_VERSION);
                    conn->max_payload = (size_t)(rmsg.arg1 < (uint32_t)ADB_MAX_PAYLOAD
                                                 ? rmsg.arg1 : (uint32_t)ADB_MAX_PAYLOAD);
                    if (rmsg.data_length > 0) {
                        int cl = (int)rmsg.data_length;
                        if (cl >= BANNER_MAX) cl = BANNER_MAX - 1;
                        memcpy(conn->banner, rpl, cl);
                        conn->banner[cl] = '\0';
                    }
                    conn->cnxn_sent = 1;
                    conn->state = ADB_STATE_CONNECTED;
                    log_info("ADB connected (TLS): %s", conn->banner);
                    /* Do NOT send CNXN reply — see comment above */
                }
            }
            if (conn->state != ADB_STATE_CONNECTED) goto fail;
            break;

        } else if (cmd == ADB_AUTH && arg0 == ADB_AUTH_TYPE_TOKEN) {
            uint8_t sig[512]; int sl = 0;
            if (crypto_sign_token(payload, dlen, sig, &sl) == 0) {
                adb_send_msg_conn(conn, ADB_AUTH, ADB_AUTH_TYPE_RSAKEY, 0,
                                  sig, (uint32_t)sl, 1);
            }
        } else if (cmd == ADB_CNXN) {
            conn->protocol_version = (int)(arg0 < (uint32_t)ADB_VERSION
                                           ? arg0 : (uint32_t)ADB_VERSION);
            conn->max_payload = (size_t)(*(uint32_t *)(hdr + 8) < (uint32_t)ADB_MAX_PAYLOAD
                                         ? *(uint32_t *)(hdr + 8) : (uint32_t)ADB_MAX_PAYLOAD);
            conn->state = ADB_STATE_CONNECTED;
            if (dlen > 0) {
                int cl = (int)dlen;
                if (cl >= BANNER_MAX) cl = BANNER_MAX - 1;
                memcpy(conn->banner, payload, cl);
                conn->banner[cl] = '\0';
            }
            log_info("ADB connected: %s", conn->banner);
        }
    }

    if (conn->state == ADB_STATE_CONNECTED) return conn;

fail:
    CLOSESOCKET(fd);
    free(conn);
    return NULL;
}

/* Create a TCP loopback socketpair (Windows doesn't have socketpair()) */
static int create_socketpair(SOCKET_T fds[2]) {
    SOCKET_T listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == INVALID_SOCKFD) return -1;
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0); /* let OS pick port */
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        CLOSESOCKET(listen_fd); return -1;
    }
    socklen_t alen = sizeof(addr);
    getsockname(listen_fd, (struct sockaddr *)&addr, &alen);
    if (listen(listen_fd, 1) < 0) {
        CLOSESOCKET(listen_fd); return -1;
    }
    fds[0] = socket(AF_INET, SOCK_STREAM, 0);
    if (fds[0] == INVALID_SOCKFD) { CLOSESOCKET(listen_fd); return -1; }
    if (connect(fds[0], (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        CLOSESOCKET(fds[0]); CLOSESOCKET(listen_fd); return -1;
    }
    fds[1] = accept(listen_fd, NULL, NULL);
    CLOSESOCKET(listen_fd);
    if (fds[1] == INVALID_SOCKFD) { CLOSESOCKET(fds[0]); return -1; }
    return 0;
}

/* ADB channel → socket relay thread */
typedef struct {
    adb_connection_t *conn;
    adb_channel_t    *chan;
    SOCKET_T          out_fd;   /* write side of socketpair */
    volatile int      running;
} adb_relay_t;

static DWORD WINAPI adb_video_relay_thread(LPVOID arg) {
    adb_relay_t *r = (adb_relay_t *)arg;
    /* Allocate large buffer for video frames (up to ADB_MAX_PAYLOAD = 1MB) */
    uint8_t *pl = malloc(ADB_MAX_PAYLOAD);
    if (!pl) return 0;
    fprintf(stderr, "RELAY: thread started, remote_id=%u\n", r->chan->remote_id);
    fflush(stderr);

    while (r->running) {
        /* Read directly from TLS — adb_recv_msg_tls returns 0 on WANT_READ */
        adb_message_t msg;
        memset(&msg, 0, sizeof(msg));
        int ret = adb_recv_msg_conn(r->conn, &msg, pl, ADB_MAX_PAYLOAD, 1);
        if (ret == 0) {
            /* WANT_READ — no data available, sleep briefly and retry */
            Sleep(1);
            continue;
        }
        if (ret == 1) {
            if (msg.command == ADB_WRTE && msg.arg0 == r->chan->remote_id) {
                /* Relay data to the socketpair */
                static int frame_count = 0;
                frame_count++;
                if (frame_count <= 5 || frame_count % 100 == 0) {
                    fprintf(stderr, "RELAY: frame %d, %u bytes\n", frame_count, msg.data_length);
                    fflush(stderr);
                }
                if (msg.data_length > 0) {
                    int sent = send(r->out_fd, (const char *)pl, msg.data_length, 0);
                    if (sent <= 0) {
                        fprintf(stderr, "RELAY: send failed, err=%d\n", SOCKET_ERRNO);
                        fflush(stderr);
                    }
                }
                adb_send_msg_conn(r->conn, ADB_OKAY, r->chan->local_id,
                                  r->chan->remote_id, NULL, 0, 1);
            } else if (msg.command == ADB_CLSE) {
                break;
            } else {
                session_handle_message(r->conn, &msg, pl);
            }
        }
    }
    free(pl);
    r->running = 0;
    return 0;
}

/* Create a local TCP listener */
static SOCKET_T create_listener(uint16_t port) {
    SOCKET_T fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKFD) return INVALID_SOCKFD;
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        CLOSESOCKET(fd); return INVALID_SOCKFD;
    }
    if (listen(fd, 4) < 0) {
        CLOSESOCKET(fd); return INVALID_SOCKFD;
    }
    log_info("Listening on 127.0.0.1:%u", port);
    return fd;
}

/* Forward relay context: one per forwarded connection (video/audio/control).
 * Each relay has its own TLS connection to adbd and a local socket pair. */
typedef struct fwd_relay {
    adb_connection_t *adb_conn;   /* Independent TLS connection to adbd */
    adb_channel_t    *chan;       /* ADB channel for the remote spec */
    SOCKET_T          local_fd;   /* Local socket (server side of the pair) */
    volatile int      running;
} fwd_relay_t;

/* Forward relay thread: bidirectional data relay between local socket and ADB channel.
 * Device→Local: WRTE from adbd → send() to local socket → OKAY ack
 * Local→Device: recv() from local socket → WRTE to adbd */
static DWORD WINAPI fwd_relay_thread(LPVOID arg) {
    fwd_relay_t *relay = (fwd_relay_t *)arg;
    adb_connection_t *conn = relay->adb_conn;
    adb_channel_t *chan = relay->chan;
    SOCKET_T local_fd = relay->local_fd;

    while (relay->running) {
        /* Try to read from ADB connection (non-blocking via SO_RCVTIMEO) */

        /* ADB connection readable → read message → relay to local */
        {
            adb_message_t msg;
            uint8_t pl[65536];
            memset(&msg, 0, sizeof(msg));
            int r = adb_recv_msg_conn(conn, &msg, pl, sizeof(pl), 1);
            if (r > 0) {
                if (msg.command == ADB_WRTE && msg.arg0 == chan->remote_id) {
                    /* Data from device → send to local socket */
                    if (msg.data_length > 0) {
                        fprintf(stderr, "RELAY: WRTE %u bytes → local\n", msg.data_length);
                        fflush(stderr);
                        send(local_fd, (const char *)pl, msg.data_length, 0);
                    }
                    /* ACK with OKAY */
                    int ack = adb_send_msg_conn(conn, ADB_OKAY, chan->local_id,
                                      chan->remote_id, NULL, 0, 1);
                    fprintf(stderr, "RELAY: OKAY ack=%d\n", ack); fflush(stderr);
                } else if (msg.command == ADB_CLSE) {
                    break; /* device closed channel */
                } else if (msg.command == ADB_OKAY) {
                    /* Update remote_id if needed */
                    for (int i = 0; i < conn->channel_count; i++) {
                        if (conn->channels[i].local_id == msg.arg1) {
                            conn->channels[i].remote_id = msg.arg0;
                            conn->channels[i].state = CHAN_OPEN;
                            break;
                        }
                    }
                }
            }
        }

        /* Try to read from local socket (non-blocking) */
        {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(local_fd, &rfds);
            struct timeval tv = {0, 0};
            if (select(0, &rfds, NULL, NULL, &tv) > 0) {
                uint8_t buf[65536];
                int n = recv(local_fd, (char *)buf, sizeof(buf), 0);
                if (n > 0) {
                    adb_send_msg_conn(conn, ADB_WRTE, chan->local_id, chan->remote_id,
                                      buf, (uint32_t)n, 1);
                }
            }
        }
    }

    relay->running = 0;
    return 0;
}

/* Create a forwarded connection: connect to local listener, create new TLS
 * connection to adbd, open channel with remote_spec, start relay thread.
 * Returns the client-side socket for the caller to use. */
static SOCKET_T create_forwarded_connection(const char *host, uint16_t adb_port,
                                             SOCKET_T listen_fd, uint16_t local_port,
                                             const char *remote_spec,
                                             const char *label,
                                             fwd_relay_t **out_relay) {
    /* 1. Connect to local listener (client side) */
    SOCKET_T client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == INVALID_SOCKFD) return INVALID_SOCKFD;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(local_port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(client_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        CLOSESOCKET(client_fd);
        return INVALID_SOCKFD;
    }

    /* 2. Accept on listener (server side) → this is the local socket for relay */
    struct sockaddr_in ca; int cl = sizeof(ca);
    SOCKET_T server_fd = accept(listen_fd, (struct sockaddr *)&ca, &cl);
    if (server_fd == INVALID_SOCKFD) {
        CLOSESOCKET(client_fd);
        return INVALID_SOCKFD;
    }

    /* 3. Create new TLS connection to adbd */
    adb_connection_t *adb_conn = do_adb_connect(host, adb_port);
    if (!adb_conn) {
        log_error("Forward: failed to connect to adbd for %s", label);
        CLOSESOCKET(client_fd);
        CLOSESOCKET(server_fd);
        return INVALID_SOCKFD;
    }

    /* 4. Open channel with remote_spec (e.g., "localabstract:scrcpy") */
    adb_channel_t *chan = session_open_channel(adb_conn, remote_spec);
    if (!chan) {
        log_error("Forward: failed to open channel for %s", label);
        adb_disconnect(adb_conn);
        CLOSESOCKET(client_fd);
        CLOSESOCKET(server_fd);
        return INVALID_SOCKFD;
    }

    /* 5. Wait for OKAY */
    for (int i = 0; i < 200; i++) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(adb_conn->fd, &rfds);
        struct timeval tv = {0, 100000};
        if (select(0, &rfds, NULL, NULL, &tv) > 0) {
            adb_message_t msg; uint8_t pl[4096];
            memset(&msg, 0, sizeof(msg));
            int r = adb_recv_msg_conn(adb_conn, &msg, pl, sizeof(pl), 1);
            if (r == 1) {
                session_handle_message(adb_conn, &msg, pl);
                if (chan->state == CHAN_OPEN) break;
            }
        }
    }
    if (chan->state != CHAN_OPEN) {
        log_error("Forward: %s channel did not open", label);
        adb_disconnect(adb_conn);
        CLOSESOCKET(client_fd);
        CLOSESOCKET(server_fd);
        return INVALID_SOCKFD;
    }
    log_info("%s connected (remote_id=%u)", label, chan->remote_id);

    /* 6. Set socket timeout for the relay thread (100ms) */
    DWORD tv_ms = 100;
    setsockopt(adb_conn->fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv_ms, sizeof(tv_ms));

    /* 7. Start relay thread */
    fwd_relay_t *relay = calloc(1, sizeof(fwd_relay_t));
    relay->adb_conn = adb_conn;
    relay->chan = chan;
    relay->local_fd = server_fd;
    relay->running = 1;
    CreateThread(NULL, 0, fwd_relay_thread, relay, 0, NULL);
    *out_relay = relay;

    /* Return the client side socket for the caller to use */
    return client_fd;
}

static void server_shell_output_cb(const uint8_t *data, uint32_t len, void *arg) {
    (void)arg;
    /* Print shell output for debugging (may contain server error messages) */
    fprintf(stderr, "[shell] %.*s", (int)len, (const char *)data);
    fflush(stderr);
}

static int recv_exact(SOCKET_T fd, void *buf, int n) {
    int done = 0;
    while (done < n) {
        int r = recv(fd, (char *)buf + done, n - done, 0);
        if (r <= 0) return -1;
        done += r;
    }
    return 0;
}

bool server_start(server_t *srv, video_socket_t *video_sock,
                  audio_socket_t *audio_sock, control_socket_t *control_sock) {
    char device_host[256];
    uint16_t device_port;
    if (!parse_serial(srv->config.serial, device_host, sizeof(device_host), &device_port)) {
        log_error("Invalid serial");
        return false;
    }

    /* Connect to device adbd */
    adb_connection_t *conn = do_adb_connect(device_host, device_port);
    if (!conn) {
        log_error("Failed to connect to adbd at %s:%u", device_host, device_port);
        return false;
    }
    srv->adb_conn = conn;

    /* Set shell output callback to capture server error messages */
    conn->on_shell_output = server_shell_output_cb;
    conn->on_shell_output_arg = NULL;

    /* Drain any post-CNXN STLS messages before proceeding.
     * Per reference implementation, device sends STLS after receiving our CNXN. */
    log_info("Draining post-CNXN messages...");
    for (int drain = 0; drain < 10; drain++) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(conn->fd, &rfds);
        struct timeval tv = {0, 200000}; /* 200ms */
        int sel = select(0, &rfds, NULL, NULL, &tv);
        if (sel <= 0) break;
        adb_message_t dmsg; uint8_t dpl[4096];
        memset(&dmsg, 0, sizeof(dmsg));
        if (adb_recv_msg_conn(conn, &dmsg, dpl, sizeof(dpl), 1) == 1) {
            log_info("Drained msg: cmd=0x%08x", dmsg.command);
            if (dmsg.command == ADB_STLS && conn->tls_ctx) {
                log_info("Ignored post-CNXN STLS");
            }
        }
    }

    /* Push scrcpy-server using inline sync protocol */
    if (srv->config.server_path) {
        log_info("Pushing %s...", srv->config.server_path);

        FILE *fp = fopen(srv->config.server_path, "rb");
        if (!fp) { log_error("Failed to open server file"); return false; }

        /* Open sync channel */
        adb_channel_t *chan = session_open_channel(conn, "sync:");
        if (!chan) { log_error("Failed to open sync channel"); fclose(fp); return false; }

        /* Wait for channel to become open using inline TLS-aware read */
        int retries = 200;
        while (chan->state == CHAN_OPENING && retries > 0) {
            fd_set rfds; FD_ZERO(&rfds); FD_SET(conn->fd, &rfds);
            struct timeval tv = {0, 100000};
            int sel = select(0, &rfds, NULL, NULL, &tv);
            if (sel > 0) {
                adb_message_t msg; uint8_t pl[4096];
                memset(&msg, 0, sizeof(msg));
                int n = adb_recv_msg_conn(conn, &msg, pl, sizeof(pl), 1);
                if (n == 1) {
                    session_handle_message(conn, &msg, pl);
                }
            }
            retries--;
        }
        if (chan->state != CHAN_OPEN) {
            log_error("Sync channel did not open"); fclose(fp); return false;
        }

        /* Send SEND command (sync protocol: ID + path_length + path,mode) */
        uint8_t cmd_buf[4 + 4 + 512];
        memcpy(cmd_buf, "SEND", 4);
        const char *remote = "/data/local/tmp/scrcpy-server.jar";
        char path_mode[512];
        snprintf(path_mode, sizeof(path_mode), "%s,0644", remote);
        uint32_t path_len = (uint32_t)strlen(path_mode);
        write32le(cmd_buf + 4, path_len);
        memcpy(cmd_buf + 8, path_mode, path_len);
        adb_send_msg_conn(conn, ADB_WRTE, chan->local_id, chan->remote_id,
                          cmd_buf, 8 + path_len, 1);

        /* Drain any pending response */
        { fd_set rfds; FD_ZERO(&rfds); FD_SET(conn->fd, &rfds);
          struct timeval tv = {0, 200000};
          if (select(0, &rfds, NULL, NULL, &tv) > 0) {
            adb_message_t msg; uint8_t pl[4096];
            if (adb_recv_msg_conn(conn, &msg, pl, sizeof(pl), 1) == 1)
                session_handle_message(conn, &msg, pl);
          }
        }

        /* Stream file data */
        #define SYNC_CHUNK (64 * 1024)
        uint8_t chunk_buf[4 + 4 + SYNC_CHUNK];
        while (!feof(fp)) {
            size_t nread = fread(chunk_buf + 8, 1, SYNC_CHUNK, fp);
            if (nread == 0) break;
            memcpy(chunk_buf, "DATA", 4);
            write32le(chunk_buf + 4, (uint32_t)nread);
            adb_send_msg_conn(conn, ADB_WRTE, chan->local_id, chan->remote_id,
                              chunk_buf, 8 + (uint32_t)nread, 1);
        }

        /* Send DONE */
        memcpy(chunk_buf, "DONE", 4);
        write32le(chunk_buf + 4, 0);
        adb_send_msg_conn(conn, ADB_WRTE, chan->local_id, chan->remote_id,
                          chunk_buf, 8, 1);

        /* Wait for sync response (OKAY or FAIL) */
        bool push_ok = false;
        retries = 200;
        while (retries > 0) {
            fd_set rfds; FD_ZERO(&rfds); FD_SET(conn->fd, &rfds);
            struct timeval tv = {0, 100000};
            if (select(0, &rfds, NULL, NULL, &tv) > 0) {
                adb_message_t msg; uint8_t pl[4096];
                int n = adb_recv_msg_conn(conn, &msg, pl, sizeof(pl), 1);
                if (n == 1) {
                    /* Check if this is a sync response on the sync channel */
                    if (msg.command == ADB_WRTE && msg.arg1 == chan->local_id &&
                        msg.data_length >= 4) {
                        /* Sync IDs are little-endian: OKAY=0x59414b4f, FAIL=0x4c494146 */
                        uint32_t sync_id = read32le(pl);
                        if (sync_id == 0x59414b4f) { /* "OKAY" in LE */
                            push_ok = true;
                            session_handle_message(conn, &msg, pl);
                            break;
                        } else if (sync_id == 0x4c494146) { /* "FAIL" in LE */
                            log_error("Push failed: %.*s", (int)(msg.data_length - 4), pl + 4);
                            session_handle_message(conn, &msg, pl);
                            break;
                        }
                    }
                    session_handle_message(conn, &msg, pl);
                }
            }
            retries--;
        }
        if (!push_ok) {
            log_error("Push failed: no OKAY response");
            session_close_channel(conn, chan);
            fclose(fp);
            return false;
        }

        session_close_channel(conn, chan);
        fclose(fp);
        log_info("Push complete");
    }

    /* Kill old instances */
    adb_shell(conn, "pkill -f com.genymobile.scrcpy.Server");
    Sleep(1000);

    /* Create local listener for forward connections */
    SOCKET_T listen_fd = create_listener(srv->config.local_port);
    if (listen_fd == INVALID_SOCKFD) {
        log_error("Failed to create listener on port %u", srv->config.local_port);
        return false;
    }

    /* Start scrcpy-server */
    {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
                 "CLASSPATH=/data/local/tmp/scrcpy-server.jar "
                 "app_process / com.genymobile.scrcpy.Server 3.3.2 "
                 "tunnel_forward=true "
                 "send_device_meta=true send_frame_meta=true "
                 "audio=false control=true");
        log_info("Starting scrcpy-server...");
        if (!adb_shell(conn, cmd)) { log_error("Shell failed"); CLOSESOCKET(listen_fd); return false; }

        /* Drain pending messages (OKAY for shell channel, WRTE with server output) */
        log_info("Waiting for scrcpy-server to start...");
        for (int t = 0; t < 50; t++) {
            fd_set rfds; FD_ZERO(&rfds); FD_SET(conn->fd, &rfds);
            struct timeval tv = {0, 100000};
            if (select(0, &rfds, NULL, NULL, &tv) > 0) {
                adb_message_t dmsg; uint8_t dpl[4096];
                memset(&dmsg, 0, sizeof(dmsg));
                int r = adb_recv_msg_conn(conn, &dmsg, dpl, sizeof(dpl), 1);
                if (r == 1) session_handle_message(conn, &dmsg, dpl);
            }
        }
    }

    /* Connect to scrcpy-server via ADB channel on the main connection.
     * Open "localabstract:scrcpy" channel — adbd connects to the abstract socket
     * and relays data as WRTE messages. We read them directly. */
    adb_channel_t *video_chan = session_open_channel(conn, "localabstract:scrcpy");
    if (!video_chan) {
        log_error("Failed to open video channel");
        return false;
    }

    /* Wait for OKAY */
    for (int i = 0; i < 200; i++) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(conn->fd, &rfds);
        struct timeval tv = {0, 100000};
        if (select(0, &rfds, NULL, NULL, &tv) > 0) {
            adb_message_t msg; uint8_t pl[4096];
            memset(&msg, 0, sizeof(msg));
            int r = adb_recv_msg_conn(conn, &msg, pl, sizeof(pl), 1);
            if (r == 1) {
                session_handle_message(conn, &msg, pl);
                if (video_chan->state == CHAN_OPEN) break;
            }
        }
    }
    if (video_chan->state != CHAN_OPEN) {
        log_error("Video channel did not open");
        return false;
    }
    log_info("Video channel open (local_id=%u, remote_id=%u)",
             video_chan->local_id, video_chan->remote_id);

    /* Open control channel (scrcpy-server expects multiple connections) */
    adb_channel_t *ctrl_chan = session_open_channel(conn, "localabstract:scrcpy");
    if (!ctrl_chan) {
        log_error("Failed to open control channel");
        return false;
    }
    for (int i = 0; i < 200; i++) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(conn->fd, &rfds);
        struct timeval tv = {0, 100000};
        if (select(0, &rfds, NULL, NULL, &tv) > 0) {
            adb_message_t msg; uint8_t pl[4096];
            memset(&msg, 0, sizeof(msg));
            int r = adb_recv_msg_conn(conn, &msg, pl, sizeof(pl), 1);
            if (r == 1) {
                session_handle_message(conn, &msg, pl);
                if (ctrl_chan->state == CHAN_OPEN) break;
            }
        }
    }
    if (ctrl_chan->state == CHAN_OPEN) {
        log_info("Control channel open (local_id=%u, remote_id=%u)",
                 ctrl_chan->local_id, ctrl_chan->remote_id);
    }

    /* Read video metadata directly from the ADB channel.
     * scrcpy-server sends: dummy(1) + device_name(64) + codec(4) + width(4) + height(4)
     * Data arrives as WRTE messages. We buffer extra bytes across reads. */
    uint8_t vbuf[65536];
    int vbuf_len = 0, vbuf_pos = 0;

    /* Helper: read one WRTE from the video channel, buffer the payload */
    #define ADB_FILL_BUF(conn, chan, vbuf, vbuf_len, vbuf_pos) do { \
        while ((vbuf_len) - (vbuf_pos) <= 0) { \
            fd_set rfds; FD_ZERO(&rfds); FD_SET((conn)->fd, &rfds); \
            struct timeval tv = {5, 0}; \
            if (select(0, &rfds, NULL, NULL, &tv) > 0) { \
                adb_message_t msg; uint8_t *pl = malloc(ADB_MAX_PAYLOAD); \
                memset(&msg, 0, sizeof(msg)); \
                int r = adb_recv_msg_conn((conn), &msg, pl, ADB_MAX_PAYLOAD, 1); \
                if (r == 1 && msg.command == ADB_WRTE && msg.arg0 == (chan)->remote_id) { \
                    memcpy(vbuf, pl, msg.data_length); \
                    (vbuf_len) = (int)msg.data_length; \
                    (vbuf_pos) = 0; \
                    adb_send_msg_conn((conn), ADB_OKAY, (chan)->local_id, (chan)->remote_id, NULL, 0, 1); \
                } else if (r == 1) { \
                    session_handle_message((conn), &msg, pl); \
                } \
                free(pl); \
            } else { \
                log_error("ADB read timeout"); return false; \
            } \
        } \
    } while(0)

    /* Helper: copy n bytes from the buffer, refill as needed */
    #define ADB_READ_N(conn, chan, dest, n, vbuf, vbuf_len, vbuf_pos) do { \
        int _done = 0; \
        while (_done < (n)) { \
            ADB_FILL_BUF(conn, chan, vbuf, vbuf_len, vbuf_pos); \
            int _avail = (vbuf_len) - (vbuf_pos); \
            int _copy = _avail < ((n) - _done) ? _avail : ((n) - _done); \
            memcpy((dest) + _done, vbuf + (vbuf_pos), _copy); \
            (vbuf_pos) += _copy; \
            _done += _copy; \
        } \
    } while(0)

    /* Read device name (64 bytes) — no dummy byte in scrcpy-server 3.3.2 */
    char devname[65] = {0};
    ADB_READ_N(conn, video_chan, (uint8_t *)devname, 64, vbuf, vbuf_len, vbuf_pos);
    log_info("Device: %s", devname);

    /* Read stream header: codec(4) + width(4) + height(4) */
    uint8_t shdr[12];
    ADB_READ_N(conn, video_chan, shdr, 12, vbuf, vbuf_len, vbuf_pos);
    video_sock->codec_id = ((uint32_t)shdr[0]<<24)|((uint32_t)shdr[1]<<16)|
                            ((uint32_t)shdr[2]<<8)|(uint32_t)shdr[3];
    video_sock->width = ((uint32_t)shdr[4]<<24)|((uint32_t)shdr[5]<<16)|
                         ((uint32_t)shdr[6]<<8)|(uint32_t)shdr[7];
    video_sock->height = ((uint32_t)shdr[8]<<24)|((uint32_t)shdr[9]<<16)|
                          ((uint32_t)shdr[10]<<8)|(uint32_t)shdr[11];
    const char *cn = "unknown";
    if (video_sock->codec_id == 0x68323634) cn = "H.264";
    else if (video_sock->codec_id == 0x68323635) cn = "H.265";
    else if (video_sock->codec_id == 0x00617631) cn = "AV1";
    log_info("Video: %s, %ux%u", cn, video_sock->width, video_sock->height);

    /* Create socketpair to bridge ADB channel → video_socket_read_packet() */
    SOCKET_T sp[2];
    if (create_socketpair(sp) < 0) {
        log_error("Failed to create socketpair for video bridge");
        return false;
    }
    /* sp[0] = read side (for video_socket_read_packet), sp[1] = write side (for relay) */
    video_sock->fd = sp[0];

    /* Flush any remaining buffered data from metadata reads */
    if (vbuf_len > vbuf_pos) {
        send(sp[1], (const char *)vbuf + vbuf_pos, vbuf_len - vbuf_pos, 0);
    }

    /* Start ADB video relay thread */
    static adb_relay_t video_relay;
    video_relay.conn = conn;
    video_relay.chan = video_chan;
    video_relay.out_fd = sp[1];
    video_relay.running = 1;
    CreateThread(NULL, 0, adb_video_relay_thread, &video_relay, 0, NULL);
    log_info("Video relay started (ADB channel → socketpair)");

    srv->running = true;
    log_info("Server started successfully");
    return true;
}

void server_kill(server_t *srv) {
    if (srv->adb_conn) {
        adb_shell((adb_connection_t *)srv->adb_conn,
                  "pkill -f com.genymobile.scrcpy.Server");
    }
    srv->running = false;
}

void server_destroy(server_t *srv) {
    if (srv->adb_conn) {
        adb_disconnect((adb_connection_t *)srv->adb_conn);
        srv->adb_conn = NULL;
    }
}
