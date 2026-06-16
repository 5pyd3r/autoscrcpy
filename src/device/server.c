#include "server.h"
#include "../platform/log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <process.h>

#define ADB_PATH "C:\\Users\\Spyder\\AppData\\Local\\Android\\Sdk\\platform-tools\\adb.exe"

bool server_init(server_t *srv, const struct server_config *config) {
    srv->config = *config;
    srv->listen_fd = INVALID_SOCKFD;
    srv->adb_conn = NULL;
    srv->running = false;
    return true;
}

static SOCKET_T connect_to_port(uint16_t port) {
    SOCKET_T fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKFD) return INVALID_SOCKFD;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    for (int i = 0; i < 20; i++) {
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            return fd;
        }
        Sleep(500);
    }

    CLOSESOCKET(fd);
    return INVALID_SOCKFD;
}

/* Launch a process in the background (detached) */
static bool launch_background(const char *cmd) {
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    char cmd_buf[2048];
    snprintf(cmd_buf, sizeof(cmd_buf), "cmd.exe /C %s", cmd);

    BOOL ret = CreateProcessA(
        NULL, cmd_buf, NULL, NULL, FALSE,
        CREATE_NO_WINDOW | DETACHED_PROCESS,
        NULL, NULL, &si, &pi);

    if (ret) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    log_error("CreateProcess failed: %lu", GetLastError());
    return false;
}

bool server_start(server_t *srv, video_socket_t *video_sock,
                  audio_socket_t *audio_sock, control_socket_t *control_sock) {
    const char *serial = srv->config.serial;
    uint16_t forward_port = srv->config.local_port;

    /* Step 1: Push scrcpy-server.jar */
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "%s -s %s push C:/Users/Spyder/Downloads/scrcpy-win64-v3.3.2/scrcpy-server /data/local/tmp/scrcpy-server.jar",
                 ADB_PATH, serial ? serial : "");
        log_info("CMD: %s", cmd);
        if (system(cmd) != 0) {
            log_error("Failed to push scrcpy-server");
            return false;
        }
        log_info("Pushed scrcpy-server.jar");
    }

    /* Step 2: Remove old port forwarding and set up new one */
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "%s -s %s forward --remove-all",
                 ADB_PATH, serial ? serial : "");
        system(cmd);

        snprintf(cmd, sizeof(cmd), "%s -s %s forward tcp:%u localabstract:scrcpy",
                 ADB_PATH, serial ? serial : "", forward_port);
        log_info("CMD: %s", cmd);
        if (system(cmd) != 0) {
            log_error("Failed to set up port forwarding");
            return false;
        }
        log_info("Port forwarding: localhost:%u -> localabstract:scrcpy", forward_port);
    }

    /* Step 3: Kill any existing scrcpy-server instances */
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "%s -s %s shell pkill -f com.genymobile.scrcpy.Server",
                 ADB_PATH, serial ? serial : "");
        system(cmd);
        Sleep(1000);
    }

    /* Step 4: Push jar file again (it may have been deleted by cleanup) */
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "%s -s %s push C:/Users/Spyder/Downloads/scrcpy-win64-v3.3.2/scrcpy-server /data/local/tmp/scrcpy-server.jar",
                 ADB_PATH, serial ? serial : "");
        system(cmd);
    }

    /* Step 5: Start scrcpy-server on device in background */
    {
        char shell_cmd[1024];
        snprintf(shell_cmd, sizeof(shell_cmd),
                 "CLASSPATH=/data/local/tmp/scrcpy-server.jar "
                 "app_process / com.genymobile.scrcpy.Server "
                 "3.3.2 "
                 "tunnel_forward=true "
                 "send_device_meta=true "
                 "send_frame_meta=true "
                 "video=%s audio=%s control=%s "
                 "max_size=%u video_bit_rate=%u audio_bit_rate=%u",
                 srv->config.video ? "true" : "false",
                 srv->config.audio ? "true" : "false",
                 srv->config.control ? "true" : "false",
                 srv->config.max_size,
                 srv->config.video_bit_rate,
                 srv->config.audio_bit_rate);

        char cmd[1280];
        snprintf(cmd, sizeof(cmd), "%s -s %s shell %s",
                 ADB_PATH, serial ? serial : "", shell_cmd);

        log_info("Starting scrcpy-server...");
        if (!launch_background(cmd)) {
            log_error("Failed to start scrcpy-server");
            return false;
        }
    }

    /* Wait for server to start and create socket */
    log_info("Waiting for server to start...");
    Sleep(5000);

    /* Step 5: Connect video socket */
    if (srv->config.video) {
        video_sock->fd = connect_to_port(forward_port);
        if (video_sock->fd == INVALID_SOCKFD) {
            log_error("Failed to connect video socket");
            return false;
        }

        /* Read dummy byte */
        uint8_t dummy;
        int n = recv(video_sock->fd, (char *)&dummy, 1, 0);
        if (n != 1) {
            log_error("Failed to read video dummy byte");
            return false;
        }
        log_info("Video dummy byte: 0x%02x", dummy);

        /* Read device name (64 bytes) */
        char devname[65] = {0};
        int received = 0;
        while (received < 64) {
            n = recv(video_sock->fd, devname + received, 64 - received, 0);
            if (n <= 0) {
                log_error("Failed to read device name");
                return false;
            }
            received += n;
        }
        log_info("Device: %s", devname);

        /* Read stream header (12 bytes: codec_id + width + height) */
        uint8_t stream_hdr[12];
        received = 0;
        while (received < 12) {
            n = recv(video_sock->fd, stream_hdr + received, 12 - received, 0);
            if (n <= 0) {
                log_error("Failed to read stream header");
                return false;
            }
            received += n;
        }

        uint32_t codec_id = ((uint32_t)stream_hdr[0] << 24) | ((uint32_t)stream_hdr[1] << 16) |
                            ((uint32_t)stream_hdr[2] << 8) | (uint32_t)stream_hdr[3];
        video_sock->codec_id = codec_id;
        video_sock->width = ((uint32_t)stream_hdr[4] << 24) | ((uint32_t)stream_hdr[5] << 16) |
                            ((uint32_t)stream_hdr[6] << 8) | (uint32_t)stream_hdr[7];
        video_sock->height = ((uint32_t)stream_hdr[8] << 24) | ((uint32_t)stream_hdr[9] << 16) |
                             ((uint32_t)stream_hdr[10] << 8) | (uint32_t)stream_hdr[11];

        const char *codec_name = "unknown";
        if (codec_id == 0x68323634) codec_name = "H.264";
        else if (codec_id == 0x68323635) codec_name = "H.265";
        else if (codec_id == 0x00617631) codec_name = "AV1";

        log_info("Video stream: %s (%08x), %ux%u", codec_name, codec_id,
                 video_sock->width, video_sock->height);
    }

    /* Step 6: Connect control socket */
    if (srv->config.control) {
        Sleep(1000);
        control_sock->fd = connect_to_port(forward_port);
        if (control_sock->fd == INVALID_SOCKFD) {
            log_error("Failed to connect control socket");
            return false;
        }
        log_info("Control socket connected");
    }

    srv->running = true;
    log_info("Server started successfully");
    return true;
}

void server_kill(server_t *srv) {
    if (srv->config.serial) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "\"%s\" -s %s shell pkill -f com.genymobile.scrcpy.Server",
                 ADB_PATH, srv->config.serial);
        system(cmd);
    }
    srv->running = false;
}

void server_destroy(server_t *srv) {
    if (srv->listen_fd != INVALID_SOCKFD) {
        CLOSESOCKET(srv->listen_fd);
        srv->listen_fd = INVALID_SOCKFD;
    }
}
