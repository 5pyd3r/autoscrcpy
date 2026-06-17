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

/* ADB Forward context: manages port forwarding from localhost to device */
typedef struct {
    adb_connection_t *fwd_conn;   /* TLS connection for the forward channel */
    SOCKET_T          listen_fd;  /* Local listener socket */
    uint16_t          local_port;
    char              remote_spec[256];
} adb_forward_t;

/* Connect to an abstract socket on the device via ADB.
 * Opens a channel with the given service (e.g., "localabstract:scrcpy").
 * Returns the channel or NULL on failure. */
static adb_channel_t *adb_connect_abstract(adb_connection_t *conn,
                                            const char *service) {
    adb_channel_t *chan = session_open_channel(conn, service);
    if (!chan) {
        log_error("Failed to open %s channel", service);
        return NULL;
    }

    /* Wait for OKAY */
    for (int i = 0; i < 100; i++) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(conn->fd, &rfds);
        struct timeval tv = {0, 100000};
        if (select(0, &rfds, NULL, NULL, &tv) > 0) {
            adb_message_t msg; uint8_t pl[4096];
            memset(&msg, 0, sizeof(msg));
            int r = adb_recv_msg_conn(conn, &msg, pl, sizeof(pl), 1);
            if (r == 1) {
                session_handle_message(conn, &msg, pl);
                if (chan->state == CHAN_OPEN) {
                    log_info("Connected to %s", service);
                    return chan;
                }
            }
        }
    }

    log_error("Channel %s did not open", service);
    return NULL;
}

/* Accept a forwarded connection: connect to localhost:PORT.
 * The connection is forwarded through ADB to the device's abstract socket.
 * Returns the data socket or INVALID_SOCKFD on failure. */
static SOCKET_T accept_forwarded_connection(adb_forward_t *fwd,
                                             const char *label) {
    /* Connect to localhost:PORT (which adbd forwards to the device) */
    SOCKET_T fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKFD) return INVALID_SOCKFD;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(fwd->local_port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("Forward: failed to connect to localhost:%u", fwd->local_port);
        CLOSESOCKET(fd);
        return INVALID_SOCKFD;
    }

    log_info("%s connected via forward", label);
    return fd;
}

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

static SOCKET_T accept_device_connection(SOCKET_T listen_fd, const char *label) {
    struct sockaddr_in ca; int cl = sizeof(ca);
    SOCKET_T fd = accept(listen_fd, (struct sockaddr *)&ca, &cl);
    if (fd == INVALID_SOCKFD) return INVALID_SOCKFD;
    uint8_t dummy;
    if (recv(fd, (char *)&dummy, 1, 0) != 1) {
        CLOSESOCKET(fd); return INVALID_SOCKFD;
    }
    log_info("%s connected", label);
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
                 "audio=false control=true");
        log_info("Starting scrcpy-server...");
        if (!adb_shell(conn, cmd)) { log_error("Shell failed"); return false; }

        /* Drain pending messages (OKAY for shell channel, WRTE with server output)
         * to keep the ADB flow going and allow the server to start. */
        log_info("Waiting for scrcpy-server to start...");
        for (int t = 0; t < 50; t++) {
            fd_set rfds; FD_ZERO(&rfds); FD_SET(conn->fd, &rfds);
            struct timeval tv = {0, 100000}; /* 100ms */
            int sel = select(0, &rfds, NULL, NULL, &tv);
            if (sel > 0) {
                adb_message_t dmsg; uint8_t dpl[4096];
                memset(&dmsg, 0, sizeof(dmsg));
                int r = adb_recv_msg_conn(conn, &dmsg, dpl, sizeof(dpl), 1);
                if (r == 1) {
                    session_handle_message(conn, &dmsg, dpl);
                }
            }
        }
    }

    /* Connect to scrcpy-server via abstract socket */
    log_info("Connecting to scrcpy-server...");
    adb_channel_t *video_chan = adb_connect_abstract(conn, "localabstract:scrcpy");
    if (!video_chan) {
        log_error("Failed to connect to scrcpy-server");
        return false;
    }

    /* Read video metadata from the ADB channel.
     * The scrcpy-server sends: dummy(1) + device_name(64) + codec(4) + width(4) + height(4)
     * Data arrives as WRTE messages processed by session_handle_message. */
    log_info("Video channel connected (local_id=%u, remote_id=%u)",
             video_chan->local_id, video_chan->remote_id);

    /* Wait for data to arrive on the video channel */
    log_info("Waiting for video data...");
    for (int t = 0; t < 100; t++) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(conn->fd, &rfds);
        struct timeval tv = {0, 100000}; /* 100ms */
        int sel = select(0, &rfds, NULL, NULL, &tv);
        if (sel > 0) {
            adb_message_t dmsg; uint8_t dpl[4096];
            memset(&dmsg, 0, sizeof(dmsg));
            int r = adb_recv_msg_conn(conn, &dmsg, dpl, sizeof(dpl), 1);
            if (r == 1) {
                session_handle_message(conn, &dmsg, dpl);
                /* Check if this is a WRTE on the video channel */
                if (dmsg.command == ADB_WRTE && dmsg.arg1 == video_chan->local_id) {
                    log_info("Video data received: %u bytes", dmsg.data_length);
                }
            }
        }
    }
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
