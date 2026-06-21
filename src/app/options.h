#ifndef OPTIONS_H
#define OPTIONS_H

#include <stdint.h>
#include <stdbool.h>

struct scrcpy_options {
    const char *serial;
    const char *server_path;
    const char *record_filename;
    const char *window_title;
    uint16_t port;
    uint32_t max_size;
    uint32_t video_bit_rate;
    uint32_t audio_bit_rate;
    const char *video_codec;
    const char *audio_codec;
    bool control;
    bool video;
    bool audio;
    bool fullscreen;
    bool always_on_top;
    bool turn_screen_off;
    bool stay_awake;
    bool show_touches;
    bool record;

    /* New fields for config file support */
    const char *audio_source;    /* "output" or "mic" */
    uint32_t window_width;       /* initial window width (0 = auto) */
    uint32_t window_height;      /* initial window height (0 = auto) */
    int log_level;               /* LOG_LEVEL_DEBUG/INFO/WARN/ERROR */
};

extern const struct scrcpy_options scrcpy_options_default;

#endif /* OPTIONS_H */
