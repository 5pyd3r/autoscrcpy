#include "adb.h"
#include "protocol.h"
#include "session.h"
#include "crypto.h"
#include "tls.h"
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

bool adb_push(adb_connection_t *conn, const char *local, const char *remote) {
    /* TODO: Implement file push using sync protocol */
    (void)conn;
    (void)local;
    (void)remote;
    log_error("adb_push not yet implemented");
    return false;
}

bool adb_forward(adb_connection_t *conn, uint16_t local_port, const char *remote_spec) {
    /* TODO: Implement port forwarding */
    (void)conn;
    (void)local_port;
    (void)remote_spec;
    log_error("adb_forward not yet implemented");
    return false;
}
