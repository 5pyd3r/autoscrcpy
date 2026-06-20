#include "server.h"
#include "../adb/adb.h"
#include "../adb/protocol.h"
#include "../adb/session.h"
#include "../adb/crypto.h"
#include "../adb/tls.h"
#include "../adb/binary.h"
#include "../platform/log.h"
#include "../platform/net_util.h"
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
    srv->reader = NULL;
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
    adb_channel_t    *audio_chan;      /* audio channel (may be NULL) */
    adb_channel_t    *ctrl_chan;       /* control channel (may be NULL) */
    SOCKET_T          video_write_fd;  /* write end of video socketpair */
    SOCKET_T          audio_write_fd;  /* write end of audio socketpair */
    SOCKET_T          ctrl_write_fd;   /* write end of control socketpair (app writes here) */
    CRITICAL_SECTION  send_lock;       /* Protects TLS writes */
    volatile int      running;
} adb_reader_t;

/* Control sender thread: reads from control socketpair, sends to device via ADB */
static DWORD WINAPI ctrl_sender_thread(LPVOID arg) {
    adb_reader_t *r = (adb_reader_t *)arg;
    uint8_t buf[1024];

    while (r->running) {
        int n = recv(r->ctrl_write_fd, (char *)buf, sizeof(buf), 0);
        if (n <= 0) {
            if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) {
                Sleep(1);
                continue;
            }
            break;
        }

        if (r->ctrl_chan && r->ctrl_chan->state == CHAN_OPEN && r->ctrl_chan->remote_id != 0) {
            EnterCriticalSection(&r->send_lock);
            adb_send_msg_conn(r->conn, ADB_WRTE,
                              r->ctrl_chan->local_id, r->ctrl_chan->remote_id,
                              buf, (uint32_t)n, 1);
            LeaveCriticalSection(&r->send_lock);
        }
    }

    return 0;
}

static DWORD WINAPI adb_reader_thread(LPVOID arg) {
    adb_reader_t *r = (adb_reader_t *)arg;
    uint8_t *pl = malloc(ADB_MAX_PAYLOAD);
    if (!pl) return 0;

    while (r->running) {
        adb_message_t msg;
        memset(&msg, 0, sizeof(msg));
        int ret = adb_recv_msg_conn(r->conn, &msg, pl, ADB_MAX_PAYLOAD, 1);
        if (ret <= 0) break;

        if (msg.command == ADB_WRTE) {
            /* Identify which channel this WRTE belongs to */
            if (r->video_chan && r->video_chan->remote_id == 0) {
                r->video_chan->remote_id = msg.arg0;
            }
            if (r->audio_chan && r->audio_chan->remote_id == 0) {
                r->audio_chan->remote_id = msg.arg0;
            }

            /* Send OKAY first for flow control (with lock) */
            EnterCriticalSection(&r->send_lock);
            adb_send_msg_conn(r->conn, ADB_OKAY,
                              msg.arg1, msg.arg0, NULL, 0, 1);
            LeaveCriticalSection(&r->send_lock);

            /* Dispatch data to appropriate socketpair */
            if (r->video_chan && msg.arg0 == r->video_chan->remote_id && msg.data_length > 0) {
                send(r->video_write_fd, (const char *)pl, msg.data_length, 0);
            } else if (r->audio_chan && msg.arg0 == r->audio_chan->remote_id && msg.data_length > 0) {
                send(r->audio_write_fd, (const char *)pl, msg.data_length, 0);
            } else if (r->ctrl_chan && msg.arg0 == r->ctrl_chan->remote_id && msg.data_length > 0) {
                send(r->ctrl_write_fd, (const char *)pl, msg.data_length, 0);
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
            if (r->video_chan && local_id == r->video_chan->local_id) {
                log_info("Video channel OKAY: remote_id=%u", remote_id);
            } else if (r->audio_chan && local_id == r->audio_chan->local_id) {
                log_info("Audio channel OKAY: remote_id=%u", remote_id);
            }
            /* Control channel OKAY is expected for every message — no need to log */
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

bool server_start(server_t *srv, video_socket_t *video_sock,
                  audio_socket_t *audio_sock, control_socket_t *control_sock) {
    bool result = false;
    char device_host[256];
    uint16_t device_port;
    adb_connection_t *conn = NULL;
    FILE *fp = NULL;
    adb_channel_t *sync_chan = NULL;
    adb_channel_t *video_chan = NULL;
    adb_channel_t *audio_chan = NULL;
    adb_channel_t *ctrl_chan = NULL;
    SOCKET_T sp[2] = {INVALID_SOCKFD, INVALID_SOCKFD};
    SOCKET_T audio_sp[2] = {INVALID_SOCKFD, INVALID_SOCKFD};
    SOCKET_T ctrl_sp[2] = {INVALID_SOCKFD, INVALID_SOCKFD};
    adb_reader_t *reader = NULL;
    HANDLE reader_thread = NULL;
    HANDLE ctrl_sender = NULL;

    if (!parse_serial(srv->config.serial, device_host, sizeof(device_host), &device_port)) {
        log_error("Invalid serial");
        goto cleanup;
    }

    /* Connect to device adbd */
    conn = adb_connect(device_host, device_port);
    if (!conn) {
        log_error("Failed to connect to adbd at %s:%u", device_host, device_port);
        goto cleanup;
    }
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

        fp = fopen(srv->config.server_path, "rb");
        if (!fp) { log_error("Failed to open server file"); goto cleanup; }

        /* Open sync channel */
        sync_chan = session_open_channel(conn, "sync:");
        if (!sync_chan) { log_error("Failed to open sync channel"); goto cleanup; }

        /* Wait for channel to become open using inline TLS-aware read */
        int retries = 200;
        while (sync_chan->state == CHAN_OPENING && retries > 0) {
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
        if (sync_chan->state != CHAN_OPEN) {
            log_error("Sync channel did not open"); goto cleanup;
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
        adb_send_msg_conn(conn, ADB_WRTE, sync_chan->local_id, sync_chan->remote_id,
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
            adb_send_msg_conn(conn, ADB_WRTE, sync_chan->local_id, sync_chan->remote_id,
                              chunk_buf, 8 + (uint32_t)nread, 1);
        }

        /* Send DONE */
        memcpy(chunk_buf, "DONE", 4);
        write32le(chunk_buf + 4, 0);
        adb_send_msg_conn(conn, ADB_WRTE, sync_chan->local_id, sync_chan->remote_id,
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
                    if (msg.command == ADB_WRTE && msg.arg1 == sync_chan->local_id &&
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
            goto cleanup;
        }

        /* Clean up sync resources after successful push */
        session_close_channel(conn, sync_chan);
        sync_chan = NULL;
        fclose(fp);
        fp = NULL;
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
                 "video=%s audio=%s control=%s "
                 "video_bit_rate=%u audio_bit_rate=%u max_size=%u "
                 "audio_codec=opus",
                 srv->config.video ? "true" : "false",
                 srv->config.audio ? "true" : "false",
                 srv->config.control ? "true" : "false",
                 srv->config.video_bit_rate,
                 srv->config.audio_bit_rate,
                 srv->config.max_size);
        log_info("Starting scrcpy-server (bitrate=%u, max_size=%u)...",
                 srv->config.video_bit_rate, srv->config.max_size);
        if (!adb_shell(conn, cmd)) { log_error("Shell failed"); goto cleanup; }

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

    /* Open channels that the server expects.
     * scrcpy-server with tunnel_forward=true does blocking accept() for each
     * channel in order: video → audio (if enabled) → control. */
    video_chan = session_open_channel(conn, "localabstract:scrcpy");
    if (!video_chan) {
        log_error("Failed to open video channel");
        goto cleanup;
    }

    /* Open audio channel if audio is enabled.
     * scrcpy-server tunnel_forward order: video → audio → control */
    if (srv->config.audio) {
        audio_chan = session_open_channel(conn, "localabstract:scrcpy");
        if (!audio_chan) {
            log_error("Failed to open audio channel");
            goto cleanup;
        }
    }

    if (srv->config.control) {
        ctrl_chan = session_open_channel(conn, "localabstract:scrcpy");
        if (!ctrl_chan) {
            log_error("Failed to open control channel");
            goto cleanup;
        }
    }

    /* Create socketpair for video data relay */
    if (create_socketpair(sp) < 0) {
        log_error("Failed to create video socketpair");
        goto cleanup;
    }
    video_sock->fd = sp[0];

    /* Create socketpair for audio data relay */
    if (audio_chan) {
        if (create_socketpair(audio_sp) < 0) {
            log_error("Failed to create audio socketpair");
            goto cleanup;
        }
        audio_sock->fd = audio_sp[0];
    }

    /* Create socketpair for control data relay */
    if (ctrl_chan) {
        if (create_socketpair(ctrl_sp) < 0) {
            log_error("Failed to create control socketpair");
            goto cleanup;
        }
        control_sock->fd = ctrl_sp[0];
    }

    /* Start reader thread — it handles ALL ADB messages */
    reader = calloc(1, sizeof(adb_reader_t));
    if (!reader) {
        log_error("Failed to allocate reader");
        goto cleanup;
    }
    reader->conn = conn;
    reader->video_chan = video_chan;
    reader->audio_chan = audio_chan;
    reader->ctrl_chan = ctrl_chan;
    reader->video_write_fd = sp[1];
    reader->audio_write_fd = audio_sp[1];
    reader->ctrl_write_fd = ctrl_sp[1];
    InitializeCriticalSection(&reader->send_lock);
    reader->running = 1;
    reader_thread = CreateThread(NULL, 0, adb_reader_thread, reader, 0, NULL);
    log_info("ADB reader thread started");

    /* Start control sender thread (reads from socketpair, sends to device) */
    if (ctrl_chan) {
        ctrl_sender = CreateThread(NULL, 0, ctrl_sender_thread, reader, 0, NULL);
        log_info("Control sender thread started");
    }

    /* Wait for video channel to open (reader thread processes OKAY) */
    for (int i = 0; i < 300; i++) {
        if (video_chan->state == CHAN_OPEN) break;
        Sleep(100);
    }
    if (video_chan->state != CHAN_OPEN) {
        log_error("Video channel did not open");
        goto cleanup;
    }
    log_info("Video channel open (remote_id=%u)", video_chan->remote_id);

    /* Wait for audio channel */
    if (audio_chan) {
        for (int i = 0; i < 300; i++) {
            if (audio_chan->state == CHAN_OPEN) break;
            Sleep(100);
        }
        if (audio_chan->state == CHAN_OPEN) {
            log_info("Audio channel open (remote_id=%u)", audio_chan->remote_id);
        } else {
            log_warn("Audio channel did not open");
        }
    }

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
    {
        uint8_t dummy_byte;
        if (recv_all(video_sock->fd, &dummy_byte, 1) < 0) {
            log_error("Failed to read video dummy byte");
            goto cleanup;
        }
    }

    {
        uint8_t devname_buf[64];
        if (recv_all(video_sock->fd, devname_buf, 64) < 0) {
            log_error("Failed to read device name");
            goto cleanup;
        }
        devname_buf[63] = '\0';
        log_info("Device: %s", devname_buf);
    }

    /* After device_name(64B), scrcpy-server sends codec_id(4B) + width(4B) + height(4B)
     * as a single 12-byte block, followed by a 12-byte session header (bit 63 set). */
    {
        uint8_t codec_dim_buf[12];
        if (recv_all(video_sock->fd, codec_dim_buf, 12) < 0) {
            log_error("Failed to read codec/dimensions");
            goto cleanup;
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
        uint8_t session_hdr[12];
        if (recv_all(video_sock->fd, session_hdr, 12) < 0) {
            log_error("Failed to read session header");
            goto cleanup;
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

    /* Success — transfer ownership to srv */
    srv->adb_conn = conn;
    conn = NULL; /* prevent cleanup from disconnecting */
    srv->video_chan = video_chan;
    srv->video_read_fd = sp[0];
    sp[0] = INVALID_SOCKFD; /* prevent cleanup from closing read end */
    srv->video_write_fd = sp[1];
    sp[1] = INVALID_SOCKFD; /* prevent cleanup from closing write end */
    srv->reader = reader;
    reader = NULL; /* prevent cleanup from freeing */
    srv->reader_thread = reader_thread;
    reader_thread = NULL; /* prevent cleanup from closing handle */
    srv->reader_running = &((adb_reader_t *)srv->reader)->running;

    srv->running = true;
    log_info("Server started successfully");
    result = true;

cleanup:
    if (!result) {
        /* Stop reader thread if it was started */
        if (reader) {
            reader->running = 0;
        }
        if (reader_thread) {
            WaitForSingleObject(reader_thread, 3000);
            CloseHandle(reader_thread);
        }
        if (ctrl_sender) {
            WaitForSingleObject(ctrl_sender, 3000);
            CloseHandle(ctrl_sender);
        }
        /* Free reader struct (after thread has stopped) */
        if (reader) {
            DeleteCriticalSection(&reader->send_lock);
            free(reader);
        }
        /* Close socketpairs */
        if (sp[0] != INVALID_SOCKFD) CLOSESOCKET(sp[0]);
        if (sp[1] != INVALID_SOCKFD) CLOSESOCKET(sp[1]);
        if (audio_sp[0] != INVALID_SOCKFD) CLOSESOCKET(audio_sp[0]);
        if (audio_sp[1] != INVALID_SOCKFD) CLOSESOCKET(audio_sp[1]);
        if (ctrl_sp[0] != INVALID_SOCKFD) CLOSESOCKET(ctrl_sp[0]);
        if (ctrl_sp[1] != INVALID_SOCKFD) CLOSESOCKET(ctrl_sp[1]);
        /* Close channels */
        if (conn) {
            if (ctrl_chan) session_close_channel(conn, ctrl_chan);
            if (audio_chan) session_close_channel(conn, audio_chan);
            if (video_chan) session_close_channel(conn, video_chan);
            if (sync_chan) session_close_channel(conn, sync_chan);
            adb_disconnect(conn);
        }
        /* Close file handle */
        if (fp) fclose(fp);
    }
    return result;
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
    /* Free heap-allocated reader struct */
    if (srv->reader) {
        DeleteCriticalSection(&((adb_reader_t *)srv->reader)->send_lock);
        free(srv->reader);
        srv->reader = NULL;
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
