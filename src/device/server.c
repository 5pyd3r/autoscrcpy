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

/* Forward relay context: each forwarded connection has its own TLS to adbd */
typedef struct {
    adb_connection_t *adb_conn;   /* Independent TLS connection to adbd */
    adb_channel_t    *chan;       /* ADB channel for localabstract:scrcpy */
    SOCKET_T          local_fd;   /* Local socket (client side) */
    volatile int      running;
} fwd_relay_t;

/* Bidirectional relay: ADB channel ↔ local socket.
 * Device→Local: WRTE from adbd → send() to local → OKAY ack
 * Local→Device: recv() from local → WRTE to adbd */
static DWORD WINAPI fwd_relay_thread(LPVOID arg) {
    fwd_relay_t *r = (fwd_relay_t *)arg;
    uint8_t *pl = malloc(ADB_MAX_PAYLOAD);
    if (!pl) return 0;
    fprintf(stderr, "RELAY: started, remote_id=%u\n", r->chan->remote_id);
    fflush(stderr);

    while (r->running) {
        /* Read from ADB connection directly (tls_recv returns 0 on WANT_READ) */
        adb_message_t msg;
        memset(&msg, 0, sizeof(msg));
        int ret = adb_recv_msg_conn(r->adb_conn, &msg, pl, ADB_MAX_PAYLOAD, 1);
        if (ret == 0) {
            /* WANT_READ — check local socket, then retry */
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(r->local_fd, &rfds);
            struct timeval tv = {0, 0};
            if (select(0, &rfds, NULL, NULL, &tv) > 0) {
                uint8_t buf[65536];
                int n = recv(r->local_fd, (char *)buf, sizeof(buf), 0);
                if (n > 0) {
                    adb_send_msg_conn(r->adb_conn, ADB_WRTE, r->chan->local_id,
                                      r->chan->remote_id, buf, (uint32_t)n, 1);
                }
            }
            Sleep(1);
            continue;
        }
        if (ret == 1) {
            if (msg.command == ADB_WRTE && msg.arg0 == r->chan->remote_id) {
                /* Data from device → relay to local socket */
                static int cnt = 0;
                cnt++;
                if (cnt <= 5 || cnt % 100 == 0) {
                    fprintf(stderr, "RELAY: #%d, %u bytes\n", cnt, msg.data_length);
                    fflush(stderr);
                }
                if (msg.data_length > 0) {
                    send(r->local_fd, (const char *)pl, msg.data_length, 0);
                }
                int ack = adb_send_msg_conn(r->adb_conn, ADB_OKAY, r->chan->local_id,
                                  r->chan->remote_id, NULL, 0, 1);
                if (cnt <= 5) {
                    fprintf(stderr, "RELAY: OKAY ack=%d\n", ack);
                    fflush(stderr);
                }
            } else if (msg.command == ADB_CLSE) {
                break;
            } else if (msg.command == ADB_OKAY) {
                for (int i = 0; i < r->adb_conn->channel_count; i++) {
                    if (r->adb_conn->channels[i].local_id == msg.arg1) {
                        r->adb_conn->channels[i].remote_id = msg.arg0;
                        r->adb_conn->channels[i].state = CHAN_OPEN;
                        break;
                    }
                }
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

    /* Start scrcpy-server */
    {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
                 "CLASSPATH=/data/local/tmp/scrcpy-server.jar "
                 "app_process / com.genymobile.scrcpy.Server 3.3.2 "
                 "tunnel_forward=true "
                 "send_device_meta=true send_frame_meta=true "
                 "video=%s audio=%s control=%s",
                 srv->config.video ? "true" : "false",
                 srv->config.audio ? "true" : "false",
                 srv->config.control ? "true" : "false");
        log_info("Starting scrcpy-server...");
        if (!adb_shell(conn, cmd)) { log_error("Shell failed"); return false; }

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

    /* Create local listener for forward connections */
    SOCKET_T listen_fd = create_listener(srv->config.local_port);
    if (listen_fd == INVALID_SOCKFD) {
        log_error("Failed to create listener on port %u", srv->config.local_port);
        return false;
    }

    /* Helper: create a forwarded connection to scrcpy-server.
     * 1. Connect to local listener (client side)
     * 2. Accept on listener (server side)
     * 3. Create new TLS connection to adbd
     * 4. Open "localabstract:scrcpy" channel
     * 5. Start bidirectional relay thread
     * Returns client side socket for the caller. */
    #define CREATE_FWD(label, out_relay) do { \
        SOCKET_T _client = socket(AF_INET, SOCK_STREAM, 0); \
        struct sockaddr_in _a; memset(&_a, 0, sizeof(_a)); \
        _a.sin_family = AF_INET; _a.sin_port = htons(srv->config.local_port); \
        _a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); \
        if (connect(_client, (struct sockaddr *)&_a, sizeof(_a)) < 0) { \
            log_error("Forward " label ": connect failed"); CLOSESOCKET(_client); \
            CLOSESOCKET(listen_fd); return false; \
        } \
        struct sockaddr_in _ca; int _cl = sizeof(_ca); \
        SOCKET_T _server = accept(listen_fd, (struct sockaddr *)&_ca, &_cl); \
        if (_server == INVALID_SOCKFD) { \
            log_error("Forward " label ": accept failed"); CLOSESOCKET(_client); \
            CLOSESOCKET(listen_fd); return false; \
        } \
        adb_connection_t *_fwd_conn = do_adb_connect(device_host, device_port); \
        if (!_fwd_conn) { \
            log_error("Forward " label ": adbd connect failed"); \
            CLOSESOCKET(_client); CLOSESOCKET(_server); \
            CLOSESOCKET(listen_fd); return false; \
        } \
        adb_channel_t *_chan = session_open_channel(_fwd_conn, "localabstract:scrcpy"); \
        if (!_chan) { \
            log_error("Forward " label ": channel open failed"); \
            adb_disconnect(_fwd_conn); CLOSESOCKET(_client); CLOSESOCKET(_server); \
            CLOSESOCKET(listen_fd); return false; \
        } \
        for (int _i = 0; _i < 200; _i++) { \
            fd_set _rfds; FD_ZERO(&_rfds); FD_SET(_fwd_conn->fd, &_rfds); \
            struct timeval _tv = {0, 100000}; \
            if (select(0, &_rfds, NULL, NULL, &_tv) > 0) { \
                adb_message_t _m; uint8_t _p[4096]; memset(&_m, 0, sizeof(_m)); \
                if (adb_recv_msg_conn(_fwd_conn, &_m, _p, sizeof(_p), 1) == 1) { \
                    session_handle_message(_fwd_conn, &_m, _p); \
                    if (_chan->state == CHAN_OPEN) break; \
                } \
            } \
        } \
        if (_chan->state != CHAN_OPEN) { \
            log_error("Forward " label ": channel did not open"); \
            adb_disconnect(_fwd_conn); CLOSESOCKET(_client); CLOSESOCKET(_server); \
            CLOSESOCKET(listen_fd); return false; \
        } \
        log_info(label " connected (remote_id=%u)", _chan->remote_id); \
        out_relay = calloc(1, sizeof(fwd_relay_t)); \
        out_relay->adb_conn = _fwd_conn; \
        out_relay->chan = _chan; \
        out_relay->local_fd = _server; \
        out_relay->running = 1; \
        CreateThread(NULL, 0, fwd_relay_thread, out_relay, 0, NULL); \
        /* Return client side via the macro caller's variable */ \
        _fwd_client_fd = _client; \
    } while(0)

    /* Connect ALL streams first — scrcpy-server waits for all connections
     * before sending any data. Order: video, audio, control. */
    SOCKET_T _fwd_client_fd = INVALID_SOCKFD;
    fwd_relay_t *video_relay = NULL, *audio_relay = NULL, *ctrl_relay = NULL;

    CREATE_FWD("Video", video_relay);
    video_sock->fd = _fwd_client_fd;

    if (srv->config.audio) {
        CREATE_FWD("Audio", audio_relay);
        audio_sock->fd = _fwd_client_fd;
    }

    CREATE_FWD("Control", ctrl_relay);
    control_sock->fd = _fwd_client_fd;
    /* TCP_NODELAY on control socket for low-latency input */
    { int opt = 1; setsockopt(control_sock->fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&opt, sizeof(opt)); }

    CLOSESOCKET(listen_fd);
    log_info("All sockets connected, reading metadata...");

    /* Now read video metadata — server starts sending after all sockets connect.
     * Protocol: 1-byte dummy (forward mode liveness check, discarded) + 64-byte device name */
    uint8_t dummy;
    recv_exact(video_sock->fd, &dummy, 1); /* discard liveness byte */

    uint8_t devname[65] = {0};
    if (recv_exact(video_sock->fd, devname, 64) < 0) {
        log_error("Failed to read device name");
        return false;
    }
    log_info("Device: %s", devname);

    /* Read codec ID (4 bytes, big-endian) */
    uint8_t codec_buf[4];
    if (recv_exact(video_sock->fd, codec_buf, 4) < 0) {
        log_error("Failed to read codec ID");
        return false;
    }
    video_sock->codec_id = ((uint32_t)codec_buf[0]<<24)|((uint32_t)codec_buf[1]<<16)|
                            ((uint32_t)codec_buf[2]<<8)|(uint32_t)codec_buf[3];

    /* Read stream metadata: width(4BE) + height(4BE).
     * scrcpy-server 3.3.2 sends codec(4)+width(4)+height(4) = 12 bytes total.
     * No separate session header marker. */
    uint8_t meta[8];
    if (recv_exact(video_sock->fd, meta, 8) < 0) {
        log_error("Failed to read stream metadata");
        return false;
    }
    video_sock->width = ((uint32_t)meta[0]<<24)|((uint32_t)meta[1]<<16)|
                         ((uint32_t)meta[2]<<8)|(uint32_t)meta[3];
    video_sock->height = ((uint32_t)meta[4]<<24)|((uint32_t)meta[5]<<16)|
                          ((uint32_t)meta[6]<<8)|(uint32_t)meta[7];

    const char *cn = "unknown";
    if (video_sock->codec_id == 0x68323634) cn = "H.264";
    else if (video_sock->codec_id == 0x68323635) cn = "H.265";
    else if (video_sock->codec_id == 0x00617631) cn = "AV1";
    log_info("Video: %s, %ux%u", cn, video_sock->width, video_sock->height);

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
