#include "config.h"
#include "../platform/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE 512
#define MAX_SECTION 64

/* Trim leading and trailing whitespace from a string in-place */
static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

/* Parse boolean value: true/1/yes -> true, false/0/no -> false */
static bool parse_bool(const char *value) {
    if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
        strcmp(value, "yes") == 0) {
        return true;
    }
    return false;
}

/* Parse log level string to enum value */
static int parse_log_level(const char *str) {
    if (strcmp(str, "debug") == 0) return 1; /* LOG_LEVEL_DEBUG */
    if (strcmp(str, "info") == 0)  return 2; /* LOG_LEVEL_INFO */
    if (strcmp(str, "warn") == 0)  return 3; /* LOG_LEVEL_WARN */
    if (strcmp(str, "error") == 0) return 4; /* LOG_LEVEL_ERROR */
    log_warn("Unknown log level: %s, using info", str);
    return 2; /* LOG_LEVEL_INFO */
}

/* Apply a key=value pair from the given section to options */
static void apply_setting(const char *section, const char *key,
                          const char *value, struct scrcpy_options *options) {
    /* [connection] */
    if (strcmp(section, "connection") == 0) {
        if (strcmp(key, "serial") == 0) {
            options->serial = _strdup(value);
        } else if (strcmp(key, "port") == 0) {
            options->port = (uint16_t)atoi(value);
        } else if (strcmp(key, "server_path") == 0) {
            options->server_path = _strdup(value);
        } else {
            log_warn("Unknown key '%s' in [connection]", key);
        }
        return;
    }

    /* [video] */
    if (strcmp(section, "video") == 0) {
        if (strcmp(key, "enabled") == 0) {
            options->video = parse_bool(value);
        } else if (strcmp(key, "codec") == 0) {
            options->video_codec = _strdup(value);
        } else if (strcmp(key, "max_size") == 0) {
            options->max_size = (uint32_t)atoi(value);
        } else if (strcmp(key, "bit_rate") == 0) {
            options->video_bit_rate = (uint32_t)atoi(value);
        } else {
            log_warn("Unknown key '%s' in [video]", key);
        }
        return;
    }

    /* [audio] */
    if (strcmp(section, "audio") == 0) {
        if (strcmp(key, "enabled") == 0) {
            options->audio = parse_bool(value);
        } else if (strcmp(key, "codec") == 0) {
            options->audio_codec = _strdup(value);
        } else if (strcmp(key, "bit_rate") == 0) {
            options->audio_bit_rate = (uint32_t)atoi(value);
        } else if (strcmp(key, "source") == 0) {
            options->audio_source = _strdup(value);
        } else {
            log_warn("Unknown key '%s' in [audio]", key);
        }
        return;
    }

    /* [control] */
    if (strcmp(section, "control") == 0) {
        if (strcmp(key, "enabled") == 0) {
            options->control = parse_bool(value);
        } else {
            log_warn("Unknown key '%s' in [control]", key);
        }
        return;
    }

    /* [window] */
    if (strcmp(section, "window") == 0) {
        if (strcmp(key, "title") == 0) {
            options->window_title = _strdup(value);
        } else if (strcmp(key, "fullscreen") == 0) {
            options->fullscreen = parse_bool(value);
        } else if (strcmp(key, "always_on_top") == 0) {
            options->always_on_top = parse_bool(value);
        } else if (strcmp(key, "width") == 0) {
            options->window_width = (uint32_t)atoi(value);
        } else if (strcmp(key, "height") == 0) {
            options->window_height = (uint32_t)atoi(value);
        } else {
            log_warn("Unknown key '%s' in [window]", key);
        }
        return;
    }

    /* [device] */
    if (strcmp(section, "device") == 0) {
        if (strcmp(key, "turn_screen_off") == 0) {
            options->turn_screen_off = parse_bool(value);
        } else if (strcmp(key, "stay_awake") == 0) {
            options->stay_awake = parse_bool(value);
        } else if (strcmp(key, "show_touches") == 0) {
            options->show_touches = parse_bool(value);
        } else {
            log_warn("Unknown key '%s' in [device]", key);
        }
        return;
    }

    /* [record] */
    if (strcmp(section, "record") == 0) {
        if (strcmp(key, "enabled") == 0) {
            options->record = parse_bool(value);
        } else if (strcmp(key, "filename") == 0) {
            options->record_filename = _strdup(value);
        } else {
            log_warn("Unknown key '%s' in [record]", key);
        }
        return;
    }

    /* [log] */
    if (strcmp(section, "log") == 0) {
        if (strcmp(key, "level") == 0) {
            options->log_level = parse_log_level(value);
        } else {
            log_warn("Unknown key '%s' in [log]", key);
        }
        return;
    }

    /* Unknown section - already handled in caller */
}

bool config_parse(const char *path, struct scrcpy_options *options) {
    FILE *f = fopen(path, "r");
    if (!f) {
        log_error("Cannot open config file: %s", path);
        return false;
    }

    char line[MAX_LINE];
    char section[MAX_SECTION] = "";
    int line_num = 0;

    while (fgets(line, sizeof(line), f)) {
        line_num++;
        char *trimmed = trim(line);

        /* Skip empty lines and comments */
        if (trimmed[0] == '\0' || trimmed[0] == '#') {
            continue;
        }

        /* Section header: [section] */
        if (trimmed[0] == '[') {
            char *end = strchr(trimmed, ']');
            if (!end) {
                log_warn("Config line %d: missing ']'", line_num);
                continue;
            }
            *end = '\0';
            strncpy(section, trimmed + 1, sizeof(section) - 1);
            section[sizeof(section) - 1] = '\0';
            continue;
        }

        /* Key = value */
        char *eq = strchr(trimmed, '=');
        if (!eq) {
            log_warn("Config line %d: missing '='", line_num);
            continue;
        }

        *eq = '\0';
        char *key = trim(trimmed);
        char *value = trim(eq + 1);

        if (section[0] == '\0') {
            log_warn("Config line %d: key '%s' before any section", line_num, key);
            continue;
        }

        /* Check for known sections */
        if (strcmp(section, "connection") != 0 &&
            strcmp(section, "video") != 0 &&
            strcmp(section, "audio") != 0 &&
            strcmp(section, "control") != 0 &&
            strcmp(section, "window") != 0 &&
            strcmp(section, "device") != 0 &&
            strcmp(section, "record") != 0 &&
            strcmp(section, "log") != 0) {
            log_warn("Config line %d: unknown section '[%s]'", line_num, section);
            continue;
        }

        apply_setting(section, key, value, options);
    }

    fclose(f);
    log_info("Loaded config from: %s", path);
    return true;
}
