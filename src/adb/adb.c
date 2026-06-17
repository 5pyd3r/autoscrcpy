#include "adb.h"
#include "protocol.h"
#include "session.h"
#include "crypto.h"
#include "tls.h"
#include "binary.h"
#include "../platform/log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static int adb_initialized = 0;

bool adb_init(void) {
    if (adb_initialized) return true;

    if (platform_init() != 0) {
        log_error("Failed to initialize platform");
        return false;
    }

    if (tls_init() != 0) {
        log_error("Failed to initialize TLS");
        return false;
    }

    /* Try to load default ADB key */
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
#else
    const char *home = getenv("HOME");
#endif
    if (home) {
        char path[512];
        snprintf(path, sizeof(path), "%s/.android/adbkey", home);
        if (crypto_load_key(path) == 0) {
            log_info("Loaded ADB key from %s", path);
            /* Configure TLS client certificate now that key is loaded */
            tls_configure_client_cert();
        }
    }

    adb_initialized = 1;
    return true;
}

void adb_destroy(void) {
    if (adb_initialized) {
        crypto_free();
        tls_cleanup();
        platform_cleanup();
        adb_initialized = 0;
    }
}

adb_connection_t *adb_connect(const char *host, uint16_t port) {
    if (!adb_initialized) {
        log_error("ADB not initialized");
        return NULL;
    }

    adb_connection_t *conn = calloc(1, sizeof(adb_connection_t));
    if (!conn) {
        log_error("Failed to allocate connection");
        return NULL;
    }

    conn->fd = session_connect(host, port);
    if (conn->fd == INVALID_SOCKFD) {
        log_error("adb_connect: session_connect failed");
        free(conn);
        return NULL;
    }
    log_info("adb_connect: socket connected, fd=%d", (int)conn->fd);

    conn->state = ADB_STATE_CONNECTING;
    conn->next_local_id = 1;
    conn->max_payload = ADB_MAX_PAYLOAD;

    /* Send CNXN and perform handshake */
    fprintf(stderr, "DEBUG: sending CNXN...\n");
    fflush(stderr);
    session_send_cnxn(conn);
    fprintf(stderr, "DEBUG: CNXN sent, entering handshake loop\n");
    fflush(stderr);

    /* Read device response — use direct select+recv (session_poll has issues) */
    for (int tries = 0; tries < 100 && conn->state != ADB_STATE_CONNECTED; tries++) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(conn->fd, &read_fds);
        struct timeval tv = {1, 0}; /* 1 second timeout */

        int ret = select(0, &read_fds, NULL, NULL, &tv);
        if (ret <= 0) continue;

        /* Read 24-byte header */
        uint8_t hdr_buf[24];
        int received = 0;
        while (received < 24) {
            int n = recv(conn->fd, (char *)hdr_buf + received, 24 - received, 0);
            if (n <= 0) goto handshake_fail;
            received += n;
        }

        uint32_t cmd = *(uint32_t *)(hdr_buf + 0);
        uint32_t arg0 = *(uint32_t *)(hdr_buf + 4);
        uint32_t arg1 = *(uint32_t *)(hdr_buf + 8);
        uint32_t dlen = *(uint32_t *)(hdr_buf + 12);
        uint32_t magic = *(uint32_t *)(hdr_buf + 20);

        if (magic != (cmd ^ 0xffffffff)) goto handshake_fail;

        /* Read payload */
        uint8_t payload[ADB_MAX_PAYLOAD];
        if (dlen > 0 && dlen <= ADB_MAX_PAYLOAD) {
            received = 0;
            while (received < (int)dlen) {
                int n = recv(conn->fd, (char *)payload + received, dlen - received, 0);
                if (n <= 0) goto handshake_fail;
                received += n;
            }
        }

        log_info("Handshake: got cmd=0x%08x", cmd);

        if (cmd == ADB_STLS) {
            /* Reply STLS */
            uint8_t stls[24];
            memset(stls, 0, 24);
            *(uint32_t *)(stls + 0)  = ADB_STLS;
            *(uint32_t *)(stls + 4)  = 0x01000000; /* STLS version */
            *(uint32_t *)(stls + 20) = ADB_STLS ^ 0xffffffff;
            send(conn->fd, (const char *)stls, 24, 0);

            /* TLS handshake */
            conn->tls_ctx = tls_handshake(conn->fd);
            if (!conn->tls_ctx) {
                log_error("TLS handshake failed");
                goto handshake_fail;
            }
            log_info("TLS handshake successful");

            /* Send CNXN over TLS */
            session_send_cnxn(conn);
            /* Continue loop to read device's CNXN over TLS */
        } else if (cmd == ADB_AUTH) {
            if (arg0 == ADB_AUTH_TYPE_TOKEN) {
                uint8_t sig[512];
                int sig_len = 0;
                if (crypto_sign_token(payload, dlen, sig, &sig_len) == 0) {
                    adb_send_msg_conn(conn, ADB_AUTH, ADB_AUTH_TYPE_RSAKEY, 0,
                                      sig, (uint32_t)sig_len, 1);
                }
            }
        } else if (cmd == ADB_CNXN) {
            conn->protocol_version = (int)(arg0 < (uint32_t)ADB_VERSION
                                           ? arg0 : (uint32_t)ADB_VERSION);
            conn->max_payload = (size_t)(arg1 < (uint32_t)ADB_MAX_PAYLOAD
                                         ? arg1 : (uint32_t)ADB_MAX_PAYLOAD);
            conn->state = ADB_STATE_CONNECTED;
            if (dlen > 0) {
                int cl = (int)dlen;
                if (cl >= BANNER_MAX) cl = BANNER_MAX - 1;
                memcpy(conn->banner, payload, cl);
                conn->banner[cl] = '\0';
            }
            log_info("ADB connected, banner=%s", conn->banner);
        }
    }

handshake_fail:

    if (conn->state != ADB_STATE_CONNECTED) {
        log_error("ADB handshake failed, state=%d", conn->state);
        adb_disconnect(conn);
        return NULL;
    }

    log_info("ADB connected to %s:%u", host, port);
    return conn;
}

void adb_disconnect(adb_connection_t *conn) {
    if (!conn) return;

    session_disconnect(conn);

    if (conn->tls_ctx) {
        tls_free(conn->tls_ctx);
    }

    free(conn);
}

bool adb_shell(adb_connection_t *conn, const char *command) {
    if (!conn || conn->state != ADB_STATE_CONNECTED) {
        log_error("Not connected");
        return false;
    }

    char service[SERVICE_NAME_MAX];
    snprintf(service, sizeof(service), "shell:%s", command);

    adb_channel_t *chan = session_open_channel(conn, service);
    if (!chan) {
        log_error("Failed to open shell channel");
        return false;
    }

    return true;
}

#define SYNC_MAX_CHUNK (64 * 1024)

static bool adb_sync_send(adb_connection_t *conn, adb_channel_t *chan,
                          const char *remote_path, FILE *local_file) {
    /* Wait for channel to open — inline poll to avoid linking issues */
    fprintf(stderr, "DEBUG adb_sync_send: entered, chan->state=%d\n", chan->state);
    fflush(stderr);
    log_info("Waiting for sync channel to open... (chan->state=%d, local_id=%u, remote_id=%u)",
             chan->state, chan->local_id, chan->remote_id);
    int retries = 200;
    while (chan->state == CHAN_OPENING && retries > 0) {
        /* Inline: select + recv_msg + handle */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(conn->fd, &rfds);
        struct timeval tv = {0, 100000}; /* 100ms */
        int sel = select(0, &rfds, NULL, NULL, &tv);
        fprintf(stderr, "DEBUG adb_sync_send: select=%d retries=%d\n", sel, retries);
        fflush(stderr);
        if (sel > 0) {
            adb_message_t msg;
            uint8_t pl[ADB_MAX_PAYLOAD];
            int skip = conn->protocol_version >= ADB_VERSION_SKIP_CHECKSUM;
            int n = adb_recv_msg_conn(conn, &msg, pl, sizeof(pl), skip);
            fprintf(stderr, "DEBUG adb_sync_send: recv=%d cmd=0x%08x\n", n, msg.command);
            fflush(stderr);
            if (n == 1) {
                session_handle_message(conn, &msg, pl);
            }
        }
        retries--;
    }
    if (chan->state != CHAN_OPEN) {
        log_error("Sync channel did not open (state=%d)", chan->state);
        return false;
    }
    log_info("Sync channel opened, sending SEND command...");

    /* Send SEND command */
    uint8_t cmd_buf[4 + 4 + 512];
    memcpy(cmd_buf, "SEND", 4);
    uint32_t path_len = (uint32_t)strlen(remote_path);
    write32le(cmd_buf + 4, path_len);
    memcpy(cmd_buf + 8, remote_path, path_len);

    adb_send_msg_conn(conn, ADB_WRTE, chan->local_id, chan->remote_id,
                      cmd_buf, 8 + path_len, 1);
    session_poll(conn, 100);

    /* Stream file data */
    uint8_t chunk_buf[4 + 4 + SYNC_MAX_CHUNK];
    while (!feof(local_file)) {
        size_t nread = fread(chunk_buf + 8, 1, SYNC_MAX_CHUNK, local_file);
        if (nread == 0) break;

        memcpy(chunk_buf, "DATA", 4);
        write32le(chunk_buf + 4, (uint32_t)nread);

        adb_send_msg_conn(conn, ADB_WRTE, chan->local_id, chan->remote_id,
                          chunk_buf, 8 + (uint32_t)nread, 1);
        session_poll(conn, 50);
    }

    /* Send DONE */
    memcpy(chunk_buf, "DONE", 4);
    write32le(chunk_buf + 4, 0);
    adb_send_msg_conn(conn, ADB_WRTE, chan->local_id, chan->remote_id,
                      chunk_buf, 8, 1);

    /* Wait for OKAY */
    retries = 200;
    while (retries > 0) {
        int r = session_poll(conn, 100);
        if (r > 0) break;
        retries--;
    }

    return true;
}

bool adb_push(adb_connection_t *conn, const char *local, const char *remote) {
    fprintf(stderr, "DEBUG adb_push: ENTERED conn=%p local=%s remote=%s\n", (void*)conn, local, remote);
    fflush(stderr);
    if (!conn || conn->state != ADB_STATE_CONNECTED) {
        log_error("Not connected");
        return false;
    }

    fprintf(stderr, "DEBUG adb_push: opening file %s\n", local);
    fflush(stderr);
    FILE *fp = fopen(local, "rb");
    if (!fp) {
        log_error("Failed to open local file: %s", local);
        return false;
    }
    fprintf(stderr, "DEBUG adb_push: file opened, opening sync channel\n");
    fflush(stderr);

    adb_channel_t *chan = session_open_channel(conn, "sync:");
    if (!chan) {
        log_error("Failed to open sync channel");
        fclose(fp);
        return false;
    }
    fprintf(stderr, "DEBUG adb_push: sync channel opened, calling adb_sync_send\n");
    fflush(stderr);

    bool ret = adb_sync_send(conn, chan, remote, fp);

    session_close_channel(conn, chan);
    fclose(fp);

    if (ret) {
        log_info("Pushed %s -> %s", local, remote);
    } else {
        log_error("Failed to push %s -> %s", local, remote);
    }

    return ret;
}

bool adb_forward(adb_connection_t *conn, uint16_t local_port, const char *remote_spec) {
    if (!conn || conn->state != ADB_STATE_CONNECTED) {
        log_error("Not connected");
        return false;
    }

    char service[SERVICE_NAME_MAX];
    snprintf(service, sizeof(service), "tcp:%u", local_port);

    adb_channel_t *chan = session_open_channel(conn, service);
    if (!chan) {
        log_error("Failed to open forward channel: %s", service);
        return false;
    }

    log_info("Forward channel opened: %s -> %s", service, remote_spec);
    return true;
}
