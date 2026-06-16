#include "options.h"

const struct scrcpy_options scrcpy_options_default = {
    .serial = NULL,
    .server_path = "C:/Users/Spyder/Downloads/scrcpy-win64-v3.3.2/scrcpy-server",
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
};
