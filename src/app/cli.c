#include "cli.h"
#include "../platform/log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

bool cli_parse(int argc, char *argv[], struct scrcpy_options *options) {
    *options = scrcpy_options_default;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--serial") == 0) {
            if (i + 1 >= argc) {
                log_error("Missing serial number");
                return false;
            }
            options->serial = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (i + 1 >= argc) {
                log_error("Missing port number");
                return false;
            }
            options->port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--max-size") == 0) {
            if (i + 1 >= argc) {
                log_error("Missing max size");
                return false;
            }
            options->max_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--video-bit-rate") == 0) {
            if (i + 1 >= argc) {
                log_error("Missing video bit rate");
                return false;
            }
            options->video_bit_rate = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--video-codec") == 0) {
            if (i + 1 >= argc) {
                log_error("Missing video codec");
                return false;
            }
            options->video_codec = argv[++i];
        } else if (strcmp(argv[i], "--audio-codec") == 0) {
            if (i + 1 >= argc) {
                log_error("Missing audio codec");
                return false;
            }
            options->audio_codec = argv[++i];
        } else if (strcmp(argv[i], "--no-control") == 0) {
            options->control = false;
        } else if (strcmp(argv[i], "--no-video") == 0) {
            options->video = false;
        } else if (strcmp(argv[i], "--no-audio") == 0) {
            options->audio = false;
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--fullscreen") == 0) {
            options->fullscreen = true;
        } else if (strcmp(argv[i], "--always-on-top") == 0) {
            options->always_on_top = true;
        } else if (strcmp(argv[i], "--turn-screen-off") == 0) {
            options->turn_screen_off = true;
        } else if (strcmp(argv[i], "--stay-awake") == 0) {
            options->stay_awake = true;
        } else if (strcmp(argv[i], "--show-touches") == 0) {
            options->show_touches = true;
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--record") == 0) {
            if (i + 1 >= argc) {
                log_error("Missing record filename");
                return false;
            }
            options->record = true;
            options->record_filename = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: autoscrcpy [options]\n");
            printf("Options:\n");
            printf("  -s, --serial <serial>      Device serial number\n");
            printf("  -p, --port <port>          ADB port (default: 5555)\n");
            printf("  -m, --max-size <size>      Max video size\n");
            printf("  -b, --video-bit-rate <bps> Video bit rate\n");
            printf("  --video-codec <codec>      Video codec (h264, h265, av1)\n");
            printf("  --audio-codec <codec>      Audio codec (opus, aac, flac)\n");
            printf("  --no-control               Disable control\n");
            printf("  --no-video                 Disable video\n");
            printf("  --no-audio                 Disable audio\n");
            printf("  -f, --fullscreen           Start in fullscreen\n");
            printf("  --always-on-top            Keep window on top\n");
            printf("  --turn-screen-off          Turn screen off\n");
            printf("  --stay-awake               Keep device awake\n");
            printf("  --show-touches             Show touches\n");
            printf("  -r, --record <file>        Record to file\n");
            printf("  -h, --help                 Show this help\n");
            return false;
        } else {
            log_error("Unknown option: %s", argv[i]);
            return false;
        }
    }

    return true;
}
