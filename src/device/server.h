#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>
#include <stdbool.h>

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

bool server_push(struct server_config *config);
bool server_start(struct server_config *config);
void server_kill(void);

#endif /* SERVER_H */
