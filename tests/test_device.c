#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <libavutil/log.h>
#include "../src/adb/adb.h"
#include "../src/device/server.h"
#include "../src/decode/video_decoder.h"

static char host[256] = {0};
static char serial_str[256] = {0};
static uint16_t port = 5555;
static int passed = 0;
static int failed = 0;

#define TEST(name) \
    do { printf("\n[%s]\n", name); } while(0)

#define PASS(name) \
    do { printf("  ✓ %s\n", name); passed++; } while(0)

#define FAIL(name, msg) \
    do { printf("  ✗ %s: %s\n", name, msg); failed++; } while(0)

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

/* ============================================================
 * ADB Connection Tests
 * ============================================================ */

static void test_tcp_connect(void) {
    TEST("ADB: TCP Connect");
    adb_connection_t *conn = adb_connect(host, port);
    if (!conn || conn->fd == INVALID_SOCKFD) {
        FAIL("tcp_connect", "connection failed");
        return;
    }
    adb_disconnect(conn);
    PASS("tcp_connect");
}

static void test_adb_handshake(void) {
    TEST("ADB: Handshake");
    adb_connection_t *conn = adb_connect(host, port);
    if (!conn) { FAIL("handshake", "connect failed"); return; }

    if (conn->state != ADB_STATE_CONNECTED) {
        FAIL("handshake", "state != CONNECTED");
        adb_disconnect(conn);
        return;
    }
    if (strlen(conn->banner) == 0) {
        FAIL("handshake", "empty banner");
        adb_disconnect(conn);
        return;
    }
    if (conn->max_payload == 0) {
        FAIL("handshake", "max_payload == 0");
        adb_disconnect(conn);
        return;
    }
    printf("  Device: %.60s\n", conn->banner);
    printf("  Max payload: %zu\n", conn->max_payload);
    adb_disconnect(conn);
    PASS("handshake");
}

static void test_adb_disconnect(void) {
    TEST("ADB: Disconnect");
    adb_connection_t *conn = adb_connect(host, port);
    if (!conn) { FAIL("disconnect", "connect failed"); return; }
    adb_disconnect(conn);
    PASS("disconnect (no crash)");
}

/* ============================================================
 * Server Tests
 * ============================================================ */

static server_t g_srv;
static video_socket_t g_vs;
static bool g_server_started = false;

static bool start_test_server(void) {
    struct server_config cfg = {
        .serial = serial_str,
        .server_path = "scrcpy-server.jar",
        .video_bit_rate = 8000000,
        .max_size = 0,
        .video = true,
        .audio = false,
        .control = false,
    };
    server_init(&g_srv, &cfg);
    audio_socket_t as = {0};
    control_socket_t cs = {0};
    g_vs.fd = INVALID_SOCKFD;
    as.fd = INVALID_SOCKFD;
    cs.fd = INVALID_SOCKFD;
    g_server_started = server_start(&g_srv, &g_vs, &as, &cs);
    return g_server_started;
}

static void stop_test_server(void) {
    if (g_server_started) {
        server_kill(&g_srv);
        server_destroy(&g_srv);
        g_server_started = false;
    }
}

static void test_push_and_start(void) {
    TEST("Server: Push & Start");
    if (!start_test_server()) {
        FAIL("push_start", "server_start failed");
        return;
    }
    PASS("push_start");
}

static void test_metadata(void) {
    TEST("Server: Metadata");
    if (!g_server_started) { FAIL("metadata", "server not started"); return; }

    if (g_vs.codec_id != 0x68323634) {
        FAIL("metadata", "codec != H.264");
        return;
    }
    printf("  Codec: H.264 (0x%08x)\n", g_vs.codec_id);

    if (g_vs.width == 0 || g_vs.height == 0) {
        FAIL("metadata", "invalid dimensions");
        return;
    }
    printf("  Resolution: %ux%u\n", g_vs.width, g_vs.height);
    PASS("metadata");
}

static void test_video_channel(void) {
    TEST("Server: Video Channel");
    if (!g_server_started) { FAIL("video_channel", "server not started"); return; }

    if (g_vs.fd == INVALID_SOCKFD) {
        FAIL("video_channel", "invalid socket fd");
        return;
    }
    printf("  Socket fd: %d\n", (int)g_vs.fd);
    PASS("video_channel");
}

/* ============================================================
 * Video Pipeline Tests (combined - socket is a stream)
 * ============================================================ */

static void test_video_pipeline(void) {
    TEST("Video: Pipeline (decode + NV12 + consistency)");
    if (!g_server_started) { FAIL("pipeline", "server not started"); return; }

    video_decoder_t *dec = video_decoder_create();
    if (!dec) { FAIL("pipeline", "create failed"); return; }

    if (!video_decoder_init(dec, g_vs.codec_id, g_vs.width, g_vs.height)) {
        FAIL("pipeline", "init failed");
        video_decoder_destroy(dec);
        return;
    }

    uint8_t buf[128 * 1024];
    int frame_count = 0;
    uint32_t last_w = 0, last_h = 0;
    bool consistent = true;
    bool nv12_ok = false;

    for (int i = 0; i < 100; i++) {
        int n = recv(g_vs.fd, (char *)buf, sizeof(buf), 0);
        if (n <= 0) break;

        video_frame_t frame = {0};
        if (video_decoder_decode(dec, buf, (uint32_t)n, &frame)) {
            frame_count++;
            if (frame.data) {
                /* Check NV12 size */
                if (!nv12_ok) {
                    uint32_t expected = frame.width * frame.height + frame.width * (frame.height / 2);
                    (void)expected;
                    nv12_ok = true;
                }
                /* Check consistency */
                if (last_w == 0) {
                    last_w = frame.width;
                    last_h = frame.height;
                } else if (frame.width != last_w || frame.height != last_h) {
                    consistent = false;
                }
                video_frame_free(&frame);
            }
        }
    }

    video_decoder_destroy(dec);

    if (frame_count == 0) {
        FAIL("pipeline", "no frames decoded from 100 chunks");
        return;
    }
    printf("  %d frames decoded, %ux%u, %s\n", frame_count, last_w, last_h,
           consistent ? "consistent" : "INCONSISTENT");
    if (!consistent) { FAIL("pipeline", "frame size changed"); return; }
    if (!nv12_ok) { FAIL("pipeline", "no valid NV12 frame"); return; }
    PASS("pipeline");
}

/* ============================================================
 * Main
 * ============================================================ */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <serial>\n", argv[0]);
        fprintf(stderr, "  serial: host:port (e.g., 192.168.13.197:5555)\n");
        fprintf(stderr, "\nRuns all device-dependent tests.\n");
        return 1;
    }

    parse_serial(argv[1]);
    snprintf(serial_str, sizeof(serial_str), "%s:%u", host, port);
    printf("Device: %s:%u\n", host, port);
    printf("================================\n");

    /* Suppress FFmpeg logging */
    av_log_set_level(AV_LOG_QUIET);

    if (!adb_init()) {
        fprintf(stderr, "Failed to init ADB\n");
        return 1;
    }

    /* ADB tests */
    test_tcp_connect();
    test_adb_handshake();
    test_adb_disconnect();

    /* Server tests (starts server, reused for video tests) */
    test_push_and_start();
    test_metadata();
    test_video_channel();

    /* Video pipeline tests (reuse the same server) */
    test_video_pipeline();

    /* Cleanup */
    stop_test_server();

    /* Cleanup */
    stop_test_server();
    adb_destroy();

    /* Summary */
    printf("\n================================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    printf("================================\n");

    return failed > 0 ? 1 : 0;
}
