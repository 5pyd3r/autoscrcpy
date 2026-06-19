#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
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

void test_tcp_connect(void) {
    printf("  Connecting to %s:%u... ", host, port);
    fflush(stdout);

    adb_connection_t *conn = adb_connect(host, port);
    assert(conn != NULL);
    assert(conn->fd != INVALID_SOCKFD);
    printf("OK (fd=%d)\n", (int)conn->fd);

    adb_disconnect(conn);
    printf("test_tcp_connect passed\n");
}

void test_adb_handshake(void) {
    printf("  Connecting to %s:%u... ", host, port);
    fflush(stdout);

    adb_connection_t *conn = adb_connect(host, port);
    assert(conn != NULL);
    assert(conn->state == ADB_STATE_CONNECTED);

    /* Banner should contain device info */
    assert(strlen(conn->banner) > 0);
    printf("OK\n");
    printf("  Banner: %.80s...\n", conn->banner);
    assert(conn->max_payload > 0);
    printf("  Max payload: %zu\n", conn->max_payload);

    adb_disconnect(conn);
    printf("test_adb_handshake passed\n");
}

void test_adb_disconnect(void) {
    adb_connection_t *conn = adb_connect(host, port);
    assert(conn != NULL);

    adb_disconnect(conn);
    /* After disconnect, calling again should be safe */
    printf("test_adb_disconnect passed\n");
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

    test_tcp_connect();
    test_adb_handshake();
    test_adb_disconnect();

    adb_destroy();
    printf("\nAll adb_connect tests passed!\n");
    return 0;
}
