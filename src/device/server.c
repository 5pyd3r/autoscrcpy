#include "server.h"
#include "../adb/adb.h"
#include "../platform/log.h"
#include <string.h>
#include <stdio.h>

bool server_init(server_t *srv, const struct server_config *config) {
    srv->config = *config;
    srv->listen_fd = INVALID_SOCKFD;
    srv->adb_conn = NULL;
    srv->running = false;
    return true;
}

static SOCKET_T create_listen_socket(uint16_t port) {
    SOCKET_T fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKFD) {
        log_error("Failed to create listen socket");
        return INVALID_SOCKFD;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("Failed to bind to port %u", port);
        CLOSESOCKET(fd);
        return INVALID_SOCKFD;
    }

    if (listen(fd, 3) < 0) {
        log_error("Failed to listen on port %u", port);
        CLOSESOCKET(fd);
        return INVALID_SOCKFD;
    }

    return fd;
}

bool server_start(server_t *srv, video_socket_t *video_sock,
                  audio_socket_t *audio_sock, control_socket_t *control_sock) {
    adb_connection_t *conn = adb_connect("127.0.0.1", 5037);
    if (!conn) {
        log_error("Failed to connect to ADB daemon");
        return false;
    }
    srv->adb_conn = conn;

    if (srv->config.server_path) {
        if (!adb_push(conn, srv->config.server_path,
                      "/data/local/tmp/scrcpy-server.jar")) {
            log_error("Failed to push scrcpy-server");
            return false;
        }
    }

    srv->listen_fd = create_listen_socket(srv->config.local_port);
    if (srv->listen_fd == INVALID_SOCKFD) return false;

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "CLASSPATH=/data/local/tmp/scrcpy-server.jar "
             "app_process / com.genymobile.scrcpy.Server "
             "scid=%08x log_level=info max_size=%u "
             "video_bit_rate=%u audio_bit_rate=%u "
             "video=%s audio=%s control=%s "
             "tunnel_forward=true send_dummy_byte=true",
             0, srv->config.max_size, srv->config.video_bit_rate,
             srv->config.audio_bit_rate,
             srv->config.video ? "true" : "false",
             srv->config.audio ? "true" : "false",
             srv->config.control ? "true" : "false");

    if (!adb_shell(conn, cmd)) {
        log_error("Failed to start scrcpy-server");
        return false;
    }

    log_info("Waiting for device connections on port %u...", srv->config.local_port);

    if (srv->config.video) {
        if (!video_socket_accept(video_sock, srv->listen_fd)) {
            log_error("Failed to accept video socket");
            return false;
        }
        log_info("Video socket connected");
    }

    if (srv->config.audio) {
        if (!audio_socket_accept(audio_sock, srv->listen_fd)) {
            log_error("Failed to accept audio socket");
            return false;
        }
        log_info("Audio socket connected");
    }

    if (srv->config.control) {
        if (!control_socket_accept(control_sock, srv->listen_fd)) {
            log_error("Failed to accept control socket");
            return false;
        }
        log_info("Control socket connected");
    }

    srv->running = true;
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
    if (srv->listen_fd != INVALID_SOCKFD) {
        CLOSESOCKET(srv->listen_fd);
        srv->listen_fd = INVALID_SOCKFD;
    }
    if (srv->adb_conn) {
        adb_disconnect((adb_connection_t *)srv->adb_conn);
        srv->adb_conn = NULL;
    }
}
