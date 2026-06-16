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
        free(conn);
        return NULL;
    }

    conn->state = ADB_STATE_CONNECTING;
    conn->next_local_id = 1;
    conn->max_payload = ADB_MAX_PAYLOAD;

    /* Start authentication */
    session_start_auth(conn);

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
    /* Wait for channel to open */
    int retries = 200;
    while (chan->state == CHAN_OPENING && retries > 0) {
        session_poll(conn, 50);
        retries--;
    }
    if (chan->state != CHAN_OPEN) {
        log_error("Sync channel did not open");
        return false;
    }

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
    if (!conn || conn->state != ADB_STATE_CONNECTED) {
        log_error("Not connected");
        return false;
    }

    FILE *fp = fopen(local, "rb");
    if (!fp) {
        log_error("Failed to open local file: %s", local);
        return false;
    }

    adb_channel_t *chan = session_open_channel(conn, "sync:");
    if (!chan) {
        log_error("Failed to open sync channel");
        fclose(fp);
        return false;
    }

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
