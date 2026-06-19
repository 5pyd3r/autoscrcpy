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
#include <process.h>

bool server_init(server_t *srv, const struct server_config *config) {
    srv->config = *config;
    srv->listen_fd = INVALID_SOCKFD;
    srv->adb_conn = NULL;
    srv->video_chan = NULL;
    srv->video_read_fd = INVALID_SOCKFD;
    srv->video_write_fd = INVALID_SOCKFD;
    srv->reader_thread = NULL;
    srv->reader_running = NULL;
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
    /* Disable Nagle's algorithm so small packets (OKAY ACKs) are sent immediately */
    {
        int flag = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(flag));
    }
    log_info("TCP connected to %s:%u", host, port);

    /* Send CNXN */
    {
        uint8_t pkt[48];
        memset(pkt, 0, 48);
        write32be(pkt + 0, ADB_CNXN);
        write32be(pkt + 4, ADB_VERSION);
        write32be(pkt + 8, (uint32_t)ADB_MAX_PAYLOAD);
        write32be(pkt + 12, 24); /* banner len + null */
        write32be(pkt + 16, 0);
        write32be(pkt + 20, ADB_CNXN ^ 0xffffffff);
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

        uint32_t cmd  = read32be(hdr + 0);
        uint32_t arg0 = read32be(hdr + 4);
        uint32_t dlen = read32be(hdr + 12);
        uint32_t mag  = read32be(hdr + 20);
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
            log_info("TLS handshake OK, tls_ctx=%p", conn->tls_ctx);

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
            conn->max_payload = (size_t)(read32be(hdr + 8) < (uint32_t)ADB_MAX_PAYLOAD
                                         ? read32be(hdr + 8) : (uint32_t)ADB_MAX_PAYLOAD);
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

    if (conn->state == ADB_STATE_CONNECTED) {
        log_info("do_adb_connect returning: tls_ctx=%p", conn->tls_ctx);
        return conn;
    }

fail:
    if (conn->tls_ctx) { tls_free(conn->tls_ctx); conn->tls_ctx = NULL; }
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
    /* Increase buffers to hold several seconds of video data.
     * At 1080p30 with H.264, typical bitrate is ~4Mbps = 500KB/s.
     * 64MB buffer = ~2 minutes of data — should never fill up. */
    {
        int bufsize = 64 * 1024 * 1024; /* 64MB */
        setsockopt(fds[1], SOL_SOCKET, SO_SNDBUF, (const char *)&bufsize, sizeof(bufsize));
        setsockopt(fds[0], SOL_SOCKET, SO_RCVBUF, (const char *)&bufsize, sizeof(bufsize));
    }
    return 0;
}

/* ADB I/O thread: single-threaded event loop for reading and writing.
 * Uses non-blocking TLS. ssl_write WANT_READ is handled by skipping
 * the write and retrying on the next iteration (after ssl_read drains
 * any pending TLS control records). */
typedef struct {
    adb_connection_t *conn;
    adb_channel_t    *video_chan;
    SOCKET_T          video_write_fd; /* write end of socketpair */
    volatile int      running;
} adb_reader_t;

static DWORD WINAPI adb_reader_thread(LPVOID arg) {
    adb_reader_t *r = (adb_reader_t *)arg;
    uint8_t *pl = malloc(ADB_MAX_PAYLOAD);
    if (!pl) return 0;

    /* Per-channel stats for diagnostics */
    uint32_t video_bytes = 0, video_count = 0;
    uint32_t other_bytes = 0, other_count = 0;
    uint32_t total_msgs = 0;
    DWORD last_log = GetTickCount();

    while (r->running) {
        adb_message_t msg;
        memset(&msg, 0, sizeof(msg));
        int ret = adb_recv_msg_conn(r->conn, &msg, pl, ADB_MAX_PAYLOAD, 1);
        if (ret <= 0) break;
        total_msgs++;

        /* Periodic stats every 3 seconds */
        DWORD now = GetTickCount();
        if (now - last_log > 3000) {
            fprintf(stderr, "[reader] stats: total=%u video=%u/%uB other=%u/%uB\n",
                    total_msgs, video_count, video_bytes, other_count, other_bytes);
            fflush(stderr);
            last_log = now;
        }

        if (msg.command == ADB_WRTE) {
            /* Update remote_id from first WRTE if not yet set by OKAY */
            if (r->video_chan->remote_id == 0) {
                r->video_chan->remote_id = msg.arg0;
            }
            /* Send OKAY FIRST so device can continue sending.
             * Then write to socketpair (blocking, but 64MB buffer). */
            adb_send_msg_conn(r->conn, ADB_OKAY,
                              msg.arg1 /* our local_id */,
                              msg.arg0 /* device's remote_id */,
                              NULL, 0, 1);
            /* Dispatch video data to socketpair */
            if (msg.arg0 == r->video_chan->remote_id && msg.data_length > 0) {
                send(r->video_write_fd, (const char *)pl, msg.data_length, 0);
                video_count++;
                video_bytes += msg.data_length;
            }
        } else if (msg.command == ADB_OKAY) {
            /* Handle OKAY — update channel state WITHOUT sending duplicate OKAY.
             * session_handle_message sends its own OKAY which would corrupt TLS,
             * so we handle the state update directly here. */
            uint32_t remote_id = msg.arg0;
            uint32_t local_id = msg.arg1;
            for (int i = 0; i < r->conn->channel_count; i++) {
                if (r->conn->channels[i].local_id == local_id) {
                    r->conn->channels[i].remote_id = remote_id;
                    r->conn->channels[i].state = CHAN_OPEN;
                    break;
                }
            }
            if (local_id == r->video_chan->local_id) {
                fprintf(stderr, "[reader] Video channel OKAY: remote_id=%u\n", remote_id);
                fflush(stderr);
            }
        } else {
            session_handle_message(r->conn, &msg, pl);
        }
    }

    free(pl);
    r->running = 0;
    return 0;
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
    log_info("max_payload=%zu", conn->max_payload);
    log_info("TLS context: %s, protocol version: 0x%08x, skip_checksum: %d",
             conn->tls_ctx ? "SET" : "NULL",
             conn->protocol_version,
             conn->protocol_version >= ADB_VERSION_SKIP_CHECKSUM);

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
        const char *remote = "/data/local/tmp/scrcpy-server";
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

    /* Skip killing — test with existing server */
    // adb_shell(conn, "pkill -f com.genymobile.scrcpy.Server");
    // Sleep(1000);

    /* Start scrcpy-server */
    {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
                 "CLASSPATH=/data/local/tmp/scrcpy-server "
                 "app_process / com.genymobile.scrcpy.Server 3.3.2 "
                 "tunnel_forward=true "
                 "video=%s audio=%s control=%s",
                 srv->config.video ? "true" : "false",
                 "false", /* audio off */
                 "false"  /* control off */);
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

    /* Open ALL channels that the server expects.
     * scrcpy-server with tunnel_forward=true does blocking accept() for each
     * channel in order: video → audio → control. We must open all of them
     * or the server blocks on accept() and never sends data. */
    adb_channel_t *video_chan = session_open_channel(conn, "localabstract:scrcpy");
    if (!video_chan) {
        log_error("Failed to open video channel");
        return false;
    }

    adb_channel_t *audio_chan = NULL; /* audio disabled */

    adb_channel_t *ctrl_chan = NULL; /* control disabled */

    /* Create socketpair for video data relay */
    SOCKET_T sp[2];
    if (create_socketpair(sp) < 0) {
        log_error("Failed to create video socketpair");
        return false;
    }
    video_sock->fd = sp[0];

    /* Start reader thread — it handles ALL ADB messages:
     * - OKAY for OPEN (sets channel state)
     * - WRTE for video (dispatches to socketpair)
     * - OKAY for WRTE (flow control)
     * - Shell/control messages */
    static adb_reader_t reader;
    reader.conn = conn;
    reader.video_chan = video_chan;
    reader.video_write_fd = sp[1];
    reader.running = 1;
    srv->reader_thread = CreateThread(NULL, 0, adb_reader_thread, &reader, 0, NULL);
    log_info("ADB reader thread started");

    /* Wait for video channel to open (reader thread processes OKAY) */
    for (int i = 0; i < 300; i++) {
        if (video_chan->state == CHAN_OPEN) break;
        Sleep(100);
    }
    if (video_chan->state != CHAN_OPEN) {
        log_error("Video channel did not open");
        return false;
    }
    log_info("Video channel open (remote_id=%u)", video_chan->remote_id);

    /* Wait for control channel */
    if (ctrl_chan) {
        for (int i = 0; i < 300; i++) {
            if (ctrl_chan->state == CHAN_OPEN) break;
            Sleep(100);
        }
        if (ctrl_chan->state == CHAN_OPEN) {
            log_info("Control channel open (remote_id=%u)", ctrl_chan->remote_id);
        } else {
            log_warn("Control channel did not open");
        }
    }

    /* Read video metadata from socketpair (sent by reader thread).
     * scrcpy-server sends (all defaults enabled):
     *   dummy(1B) + device_name(64B) + codec_id(4B, BE)
     * Then the first scrcpy packet is a session header (12B) with width/height,
     * which will be read by video_socket_read_packet(). */
    uint8_t dummy_byte;
    if (recv_exact(video_sock->fd, &dummy_byte, 1) < 0) {
        log_error("Failed to read video dummy byte");
        return false;
    }

    uint8_t devname_buf[64];
    if (recv_exact(video_sock->fd, devname_buf, 64) < 0) {
        log_error("Failed to read device name");
        return false;
    }
    devname_buf[63] = '\0';
    log_info("Device: %s", devname_buf);

    /* After device_name(64B), scrcpy-server sends codec_id(4B) + width(4B) + height(4B)
     * as a single 12-byte block, followed by a 12-byte session header (bit 63 set). */
    uint8_t codec_dim_buf[12];
    if (recv_exact(video_sock->fd, codec_dim_buf, 12) < 0) {
        log_error("Failed to read codec/dimensions");
        return false;
    }
    video_sock->codec_id = ((uint32_t)codec_dim_buf[0]<<24)|((uint32_t)codec_dim_buf[1]<<16)|
                            ((uint32_t)codec_dim_buf[2]<<8)|(uint32_t)codec_dim_buf[3];
    /* Width/height from the codec+dimensions block (may be overridden by session header) */
    uint32_t dim_w = ((uint32_t)codec_dim_buf[4]<<24)|((uint32_t)codec_dim_buf[5]<<16)|
                      ((uint32_t)codec_dim_buf[6]<<8)|(uint32_t)codec_dim_buf[7];
    uint32_t dim_h = ((uint32_t)codec_dim_buf[8]<<24)|((uint32_t)codec_dim_buf[9]<<16)|
                      ((uint32_t)codec_dim_buf[10]<<8)|(uint32_t)codec_dim_buf[11];

    const char *cn = "unknown";
    if (video_sock->codec_id == 0x68323634) cn = "H.264";
    else if (video_sock->codec_id == 0x68323635) cn = "H.265";
    else if (video_sock->codec_id == 0x00617631) cn = "AV1";
    log_info("Video codec: %s (0x%08x), dimensions hint: %ux%u", cn, video_sock->codec_id, dim_w, dim_h);

    /* Use dimensions from the codec block — these are always correct */
    video_sock->width = dim_w;
    video_sock->height = dim_h;

    /* Read the session header (12 bytes, first byte = 0x80).
     * The session header may report different dimensions than the codec block;
     * we trust the codec block dimensions. After the session header, the device
     * sends video packets directly. */
    {
        uint8_t session_hdr[12];
        if (recv_exact(video_sock->fd, session_hdr, 12) < 0) {
            log_error("Failed to read session header");
            return false;
        }
        if (session_hdr[0] & 0x80) {
            uint32_t sw = ((uint32_t)session_hdr[4]<<24)|((uint32_t)session_hdr[5]<<16)|
                           ((uint32_t)session_hdr[6]<<8)|(uint32_t)session_hdr[7];
            uint32_t sh = ((uint32_t)session_hdr[8]<<24)|((uint32_t)session_hdr[9]<<16)|
                           ((uint32_t)session_hdr[10]<<8)|(uint32_t)session_hdr[11];
            log_info("Session header: %ux%u (codec block: %ux%u)", sw, sh, dim_w, dim_h);
        } else {
            log_info("No session header (byte0=0x%02x), using codec dimensions", session_hdr[0]);
            /* Save as pending for video receiver */
            video_sock->pending = malloc(12);
            if (video_sock->pending) {
                memcpy(video_sock->pending, session_hdr, 12);
                video_sock->pending_size = 12;
            }
        }
    }

    srv->adb_conn = conn;
    srv->video_chan = video_chan;
    srv->video_read_fd = sp[0];
    srv->video_write_fd = sp[1];
    srv->reader_running = &reader.running;

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
    /* Stop reader thread first */
    if (srv->reader_running) {
        *srv->reader_running = 0;
    }
    if (srv->reader_thread) {
        WaitForSingleObject(srv->reader_thread, 3000);
        CloseHandle(srv->reader_thread);
        srv->reader_thread = NULL;
    }
    /* Close write-end socket (reader thread's socket) */
    if (srv->video_write_fd != INVALID_SOCKFD) {
        CLOSESOCKET(srv->video_write_fd);
        srv->video_write_fd = INVALID_SOCKFD;
    }
    if (srv->adb_conn) {
        adb_disconnect((adb_connection_t *)srv->adb_conn);
        srv->adb_conn = NULL;
    }
}
