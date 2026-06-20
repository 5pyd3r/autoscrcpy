#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include "../platform/platform.h"
#include "video_socket.h"
#include "audio_socket.h"
#include "control_socket.h"

struct server_config {
    const char *serial;
    const char *server_path;
    uint16_t local_port;
    uint32_t max_size;
    uint32_t video_bit_rate;
    uint32_t audio_bit_rate;
    const char *video_encoder;
    const char *audio_encoder;
    bool control;
    bool video;
    bool audio;
};

typedef struct server {
    struct server_config config;
    SOCKET_T listen_fd;
    void *adb_conn;          /* adb_connection_t* */
    void *video_chan;        /* adb_channel_t* for video stream */
    SOCKET_T video_read_fd;  /* read end of video socketpair */
    SOCKET_T video_write_fd; /* write end of video socketpair (owned by reader thread) */
    HANDLE reader_thread;    /* ADB reader thread handle */
    volatile int *reader_running; /* pointer to reader's running flag */
    volatile bool running;
    void *reader;              /* adb_reader_t* allocated on heap */
} server_t;

bool server_init(server_t *srv, const struct server_config *config);
bool server_start(server_t *srv, video_socket_t *video_sock,
                  audio_socket_t *audio_sock, control_socket_t *control_sock);
void server_kill(server_t *srv);
void server_destroy(server_t *srv);

#endif /* SERVER_H */
