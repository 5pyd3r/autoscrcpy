#include "options.h"

const struct scrcpy_options scrcpy_options_default = {
    .serial = NULL,
    .server_path = "scrcpy-server.jar",
    .record_filename = NULL,
    .window_title = "AutoScrcpy",
    .port = 5555,
    .max_size = 0,
    .video_bit_rate = 8000000,
    .audio_bit_rate = 128000,
    .video_codec = "h264",
    .audio_codec = "opus",
    .control = true,
    .video = true,
    .audio = true,
    .fullscreen = false,
    .always_on_top = false,
    .turn_screen_off = false,
    .stay_awake = false,
    .show_touches = false,
    .record = false,

    /* New defaults */
    .audio_source = "output",
    .window_width = 0,
    .window_height = 0,
    .log_level = 2,  /* LOG_LEVEL_INFO */
};
