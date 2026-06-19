#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "../src/device/server.h"
#include "../src/adb/adb.h"

static char host[256] = {0};
static uint16_t port = 5555;

static void parse_serial(const char *serial) {
    const char *colon = strrchr(serial, ':');
    if (colon) {
        int len = (int)(colon - serial);
        if (len >= (int)sizeof(host)) len = sizeof(host) - 1;
        memcpy(host, serial, len);
        host[len] = '\0';
        port = (uint16_t)atoi(colon + 1);
    } else {
        snprintf(host, sizeof(host), "%s", serial);
        port = 5555;
    }
}

void test_push_jar(void) {
    printf("  Testing push scrcpy-server.jar...\n");

    struct server_config cfg = {
        .serial = host,
        .server_path = "scrcpy-server.jar",
        .video_bit_rate = 8000000,
        .max_size = 0,
        .video = true,
        .audio = false,
        .control = false,
    };

    server_t srv;
    bool ok = server_init(&srv, &cfg);
    assert(ok == true);

    video_socket_t vs = {0};
    audio_socket_t as = {0};
    control_socket_t cs = {0};
    vs.fd = INVALID_SOCKFD;
    as.fd = INVALID_SOCKFD;
    cs.fd = INVALID_SOCKFD;

    ok = server_start(&srv, &vs, &as, &cs);
    assert(ok == true);
    printf("  Server started successfully\n");

    server_kill(&srv);
    server_destroy(&srv);
    printf("test_push_jar passed\n");
}

void test_metadata(void) {
    printf("  Testing video metadata...\n");

    struct server_config cfg = {
        .serial = host,
        .server_path = "scrcpy-server.jar",
        .video_bit_rate = 8000000,
        .max_size = 0,
        .video = true,
        .audio = false,
        .control = false,
    };

    server_t srv;
    server_init(&srv, &cfg);

    video_socket_t vs = {0};
    audio_socket_t as = {0};
    control_socket_t cs = {0};
    vs.fd = INVALID_SOCKFD;
    as.fd = INVALID_SOCKFD;
    cs.fd = INVALID_SOCKFD;

    bool ok = server_start(&srv, &vs, &as, &cs);
    assert(ok == true);

    /* Verify video metadata */
    assert(vs.codec_id == 0x68323634); /* H.264 */
    printf("  Codec: H.264 (0x%08x)\n", vs.codec_id);

    assert(vs.width > 0 && vs.height > 0);
    printf("  Resolution: %ux%u\n", vs.width, vs.height);

    server_kill(&srv);
    server_destroy(&srv);
    printf("test_metadata passed\n");
}

void test_video_channel(void) {
    printf("  Testing video channel...\n");

    struct server_config cfg = {
        .serial = host,
        .server_path = "scrcpy-server.jar",
        .video_bit_rate = 8000000,
        .max_size = 0,
        .video = true,
        .audio = false,
        .control = false,
    };

    server_t srv;
    server_init(&srv, &cfg);

    video_socket_t vs = {0};
    audio_socket_t as = {0};
    control_socket_t cs = {0};
    vs.fd = INVALID_SOCKFD;
    as.fd = INVALID_SOCKFD;
    cs.fd = INVALID_SOCKFD;

    bool ok = server_start(&srv, &vs, &as, &cs);
    assert(ok == true);

    /* Video socket should be valid */
    assert(vs.fd != INVALID_SOCKFD);
    printf("  Video socket fd: %d\n", (int)vs.fd);

    server_kill(&srv);
    server_destroy(&srv);
    printf("test_video_channel passed\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <serial>\n", argv[0]);
        fprintf(stderr, "  serial: host:port (e.g., 192.168.13.197:5555)\n");
        return 1;
    }

    parse_serial(argv[1]);
    printf("Device: %s:%u\n", host, port);

    if (!adb_init()) {
        fprintf(stderr, "Failed to init ADB\n");
        return 1;
    }

    test_push_jar();
    test_metadata();
    test_video_channel();

    adb_destroy();
    printf("\nAll server tests passed!\n");
    return 0;
}
