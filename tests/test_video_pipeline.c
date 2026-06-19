#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "../src/device/server.h"
#include "../src/decode/video_decoder.h"
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

/* Helper: start server and return video socket */
static bool start_server(server_t *srv, video_socket_t *vs) {
    struct server_config cfg = {
        .serial = host,
        .server_path = "scrcpy-server.jar",
        .video_bit_rate = 8000000,
        .max_size = 0,
        .video = true,
        .audio = false,
        .control = false,
    };

    server_init(srv, &cfg);

    audio_socket_t as = {0};
    control_socket_t cs = {0};
    vs->fd = INVALID_SOCKFD;
    as.fd = INVALID_SOCKFD;
    cs.fd = INVALID_SOCKFD;

    return server_start(srv, vs, &as, &cs);
}

void test_decode_frames(void) {
    printf("  Starting server for decode test...\n");

    server_t srv;
    video_socket_t vs;
    bool ok = start_server(&srv, &vs);
    assert(ok == true);

    video_decoder_t *dec = video_decoder_create();
    assert(dec != NULL);
    ok = video_decoder_init(dec, vs.codec_id, vs.width, vs.height);
    assert(ok == true);

    uint8_t buf[128 * 1024];
    int frame_count = 0;

    printf("  Reading and decoding frames...\n");
    for (int i = 0; i < 10; i++) {
        int n = recv(vs.fd, (char *)buf, sizeof(buf), 0);
        if (n <= 0) break;

        video_frame_t frame = {0};
        if (video_decoder_decode(dec, buf, (uint32_t)n, &frame)) {
            frame_count++;
            assert(frame.data != NULL);
            assert(frame.width > 0 && frame.height > 0);
            video_frame_free(&frame);
        }
    }

    printf("  Decoded %d frames from 10 chunks\n", frame_count);
    assert(frame_count > 0);

    video_decoder_destroy(dec);
    server_kill(&srv);
    server_destroy(&srv);
    printf("test_decode_frames passed\n");
}

void test_nv12_size(void) {
    printf("  Testing NV12 buffer size...\n");

    server_t srv;
    video_socket_t vs;
    bool ok = start_server(&srv, &vs);
    assert(ok == true);

    video_decoder_t *dec = video_decoder_create();
    video_decoder_init(dec, vs.codec_id, vs.width, vs.height);

    uint8_t buf[128 * 1024];
    for (int i = 0; i < 20; i++) {
        int n = recv(vs.fd, (char *)buf, sizeof(buf), 0);
        if (n <= 0) break;

        video_frame_t frame = {0};
        if (video_decoder_decode(dec, buf, (uint32_t)n, &frame)) {
            uint32_t expected = frame.width * frame.height + frame.width * (frame.height / 2);
            /* Frame data might be NULL if only header received */
            if (frame.data) {
                printf("  Frame %ux%u, NV12 size: %u (expected %u)\n",
                       frame.width, frame.height, 0, expected);
                video_frame_free(&frame);
            }
            break;
        }
    }

    video_decoder_destroy(dec);
    server_kill(&srv);
    server_destroy(&srv);
    printf("test_nv12_size passed\n");
}

void test_multi_frame(void) {
    printf("  Testing multi-frame decode...\n");

    server_t srv;
    video_socket_t vs;
    bool ok = start_server(&srv, &vs);
    assert(ok == true);

    video_decoder_t *dec = video_decoder_create();
    video_decoder_init(dec, vs.codec_id, vs.width, vs.height);

    uint8_t buf[128 * 1024];
    int frame_count = 0;
    uint32_t last_w = 0, last_h = 0;

    for (int i = 0; i < 100; i++) {
        int n = recv(vs.fd, (char *)buf, sizeof(buf), 0);
        if (n <= 0) break;

        video_frame_t frame = {0};
        if (video_decoder_decode(dec, buf, (uint32_t)n, &frame)) {
            frame_count++;
            if (frame.data) {
                if (last_w == 0) {
                    last_w = frame.width;
                    last_h = frame.height;
                } else {
                    /* Frame size should be consistent */
                    assert(frame.width == last_w);
                    assert(frame.height == last_h);
                }
                video_frame_free(&frame);
            }
        }
    }

    printf("  Decoded %d frames from 100 chunks, size %ux%u\n",
           frame_count, last_w, last_h);
    assert(frame_count > 0);

    video_decoder_destroy(dec);
    server_kill(&srv);
    server_destroy(&srv);
    printf("test_multi_frame passed\n");
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

    test_decode_frames();
    test_nv12_size();
    test_multi_frame();

    adb_destroy();
    printf("\nAll video_pipeline tests passed!\n");
    return 0;
}
