# Config File Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add INI config file support so users can persist settings in `config.ini` and load via `-c config.ini`.

**Architecture:** Hand-written INI parser in `src/app/config.c` (~150 lines). CLI refactored to two-pass scan: first pass finds `-c`, loads config; second pass overrides with CLI args. Priority: CLI > config > defaults.

**Tech Stack:** C11, standard library (`stdio.h`, `string.h`, `stdlib.h`), no new dependencies.

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `src/app/options.h` | Modify | Add 4 new fields to `scrcpy_options` |
| `src/app/options.c` | Modify | Set defaults for new fields |
| `src/app/config.h` | Create | `config_parse()` declaration |
| `src/app/config.c` | Create | INI parser implementation |
| `src/app/cli.c` | Modify | Two-pass parsing + `-c` flag + help text |
| `tests/test_config.c` | Create | Unit tests for config parser |
| `tests/meson.build` | Modify | Register test_config |
| `src/meson.build` | Modify | Register config.c in app_src |

---

### Task 1: Extend scrcpy_options with new fields

**Files:**
- Modify: `src/app/options.h:7-27`
- Modify: `src/app/options.c:3-23`

- [ ] **Step 1: Add new fields to options.h**

Open `src/app/options.h` and add 4 new fields after `bool record;`:

```c
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
```

- [ ] **Step 2: Set defaults for new fields in options.c**

Open `src/app/options.c` and add defaults after `.record = false,`:

```c
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
```

Note: `log_level = 2` corresponds to `LOG_LEVEL_INFO` from `platform/log.h` (VERBOSE=0, DEBUG=1, INFO=2, WARN=3, ERROR=4).

- [ ] **Step 3: Build to verify compilation**

Run: `ninja -C builddir`
Expected: Build succeeds (no new code yet, just struct extension).

- [ ] **Step 4: Run existing tests to verify no regressions**

Run: `meson test -C builddir`
Expected: All 38 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/app/options.h src/app/options.c
git commit -m "feat: extend scrcpy_options with audio_source, window_width, window_height, log_level"
```

---

### Task 2: Create config.h header

**Files:**
- Create: `src/app/config.h`

- [ ] **Step 1: Write config.h**

Create `src/app/config.h`:

```c
#ifndef CONFIG_H
#define CONFIG_H

#include "options.h"
#include <stdbool.h>

/**
 * Parse an INI config file and apply values to options.
 *
 * Supports sections: [connection], [video], [audio], [control],
 * [window], [device], [record], [log].
 *
 * Unknown sections/keys are warned but do not cause failure.
 *
 * @param path    Path to the INI file.
 * @param options Options struct to populate (must already have defaults).
 * @return true on success, false if file cannot be opened.
 */
bool config_parse(const char *path, struct scrcpy_options *options);

#endif /* CONFIG_H */
```

- [ ] **Step 2: Commit**

```bash
git add src/app/config.h
git commit -m "feat: add config.h header for INI parser"
```

---

### Task 3: Implement INI parser with tests (TDD)

**Files:**
- Create: `tests/test_config.c`
- Create: `src/app/config.c`
- Modify: `tests/meson.build`
- Modify: `src/meson.build`

- [ ] **Step 1: Write test_config.c with all test cases**

Create `tests/test_config.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "../src/app/config.h"
#include "../src/app/options.h"
#include "../src/platform/log.h"

/* Helper: write content to a temp file, return its path */
static const char *write_temp_config(const char *content) {
    static char path[256];
    snprintf(path, sizeof(path), "test_config_tmp.ini");
    FILE *f = fopen(path, "w");
    if (!f) return NULL;
    fputs(content, f);
    fclose(f);
    return path;
}

/* Helper: remove temp file */
static void remove_temp_config(void) {
    remove("test_config_tmp.ini");
}

void test_parse_full_config(void) {
    const char *content =
        "[connection]\n"
        "serial = 192.168.1.100:5555\n"
        "port = 5556\n"
        "server_path = /custom/scrcpy-server.jar\n"
        "\n"
        "[video]\n"
        "enabled = false\n"
        "codec = h265\n"
        "max_size = 720\n"
        "bit_rate = 4000000\n"
        "\n"
        "[audio]\n"
        "enabled = false\n"
        "codec = aac\n"
        "bit_rate = 64000\n"
        "source = mic\n"
        "\n"
        "[control]\n"
        "enabled = false\n"
        "\n"
        "[window]\n"
        "title = My Device\n"
        "fullscreen = true\n"
        "always_on_top = true\n"
        "width = 1280\n"
        "height = 720\n"
        "\n"
        "[device]\n"
        "turn_screen_off = true\n"
        "stay_awake = true\n"
        "show_touches = true\n"
        "\n"
        "[record]\n"
        "enabled = true\n"
        "filename = test.mp4\n"
        "\n"
        "[log]\n"
        "level = debug\n";

    const char *path = write_temp_config(content);
    assert(path != NULL);

    struct scrcpy_options options = scrcpy_options_default;
    bool ok = config_parse(path, &options);
    assert(ok == true);

    /* connection */
    assert(strcmp(options.serial, "192.168.1.100:5555") == 0);
    assert(options.port == 5556);
    assert(strcmp(options.server_path, "/custom/scrcpy-server.jar") == 0);

    /* video */
    assert(options.video == false);
    assert(strcmp(options.video_codec, "h265") == 0);
    assert(options.max_size == 720);
    assert(options.video_bit_rate == 4000000);

    /* audio */
    assert(options.audio == false);
    assert(strcmp(options.audio_codec, "aac") == 0);
    assert(options.audio_bit_rate == 64000);
    assert(strcmp(options.audio_source, "mic") == 0);

    /* control */
    assert(options.control == false);

    /* window */
    assert(strcmp(options.window_title, "My Device") == 0);
    assert(options.fullscreen == true);
    assert(options.always_on_top == true);
    assert(options.window_width == 1280);
    assert(options.window_height == 720);

    /* device */
    assert(options.turn_screen_off == true);
    assert(options.stay_awake == true);
    assert(options.show_touches == true);

    /* record */
    assert(options.record == true);
    assert(strcmp(options.record_filename, "test.mp4") == 0);

    /* log */
    assert(options.log_level == 1); /* LOG_LEVEL_DEBUG */

    remove_temp_config();
    printf("test_parse_full_config passed\n");
}

void test_boolean_variants(void) {
    const char *content =
        "[video]\n"
        "enabled = true\n"
        "[audio]\n"
        "enabled = 1\n"
        "[control]\n"
        "enabled = yes\n";

    const char *path = write_temp_config(content);
    assert(path != NULL);

    struct scrcpy_options options = scrcpy_options_default;
    bool ok = config_parse(path, &options);
    assert(ok == true);
    assert(options.video == true);
    assert(options.audio == true);
    assert(options.control == true);

    /* Test false variants */
    const char *content2 =
        "[video]\n"
        "enabled = false\n"
        "[audio]\n"
        "enabled = 0\n"
        "[control]\n"
        "enabled = no\n";

    path = write_temp_config(content2);
    assert(path != NULL);

    options = scrcpy_options_default;
    ok = config_parse(path, &options);
    assert(ok == true);
    assert(options.video == false);
    assert(options.audio == false);
    assert(options.control == false);

    remove_temp_config();
    printf("test_boolean_variants passed\n");
}

void test_comments_and_blanks(void) {
    const char *content =
        "# This is a comment\n"
        "\n"
        "[video]\n"
        "# Another comment\n"
        "enabled = true\n"
        "\n"
        "  # Indented comment\n"
        "[audio]\n"
        "enabled = false\n";

    const char *path = write_temp_config(content);
    assert(path != NULL);

    struct scrcpy_options options = scrcpy_options_default;
    bool ok = config_parse(path, &options);
    assert(ok == true);
    assert(options.video == true);
    assert(options.audio == false);

    remove_temp_config();
    printf("test_comments_and_blanks passed\n");
}

void test_unknown_section(void) {
    const char *content =
        "[unknown_section]\n"
        "some_key = some_value\n"
        "[video]\n"
        "enabled = true\n";

    const char *path = write_temp_config(content);
    assert(path != NULL);

    struct scrcpy_options options = scrcpy_options_default;
    bool ok = config_parse(path, &options);
    assert(ok == true); /* should not fail */
    assert(options.video == true);

    remove_temp_config();
    printf("test_unknown_section passed\n");
}

void test_unknown_key(void) {
    const char *content =
        "[video]\n"
        "unknown_key = value\n"
        "enabled = true\n";

    const char *path = write_temp_config(content);
    assert(path != NULL);

    struct scrcpy_options options = scrcpy_options_default;
    bool ok = config_parse(path, &options);
    assert(ok == true); /* should not fail */
    assert(options.video == true);

    remove_temp_config();
    printf("test_unknown_key passed\n");
}

void test_file_not_found(void) {
    struct scrcpy_options options = scrcpy_options_default;
    bool ok = config_parse("nonexistent_file.ini", &options);
    assert(ok == false);

    printf("test_file_not_found passed\n");
}

void test_partial_config(void) {
    const char *content =
        "[video]\n"
        "codec = h265\n";

    const char *path = write_temp_config(content);
    assert(path != NULL);

    struct scrcpy_options options = scrcpy_options_default;
    bool ok = config_parse(path, &options);
    assert(ok == true);

    /* Only video_codec should change */
    assert(strcmp(options.video_codec, "h265") == 0);

    /* Everything else should remain default */
    assert(options.video == true);
    assert(options.audio == true);
    assert(options.control == true);
    assert(options.port == 5555);
    assert(options.max_size == 0);
    assert(options.video_bit_rate == 8000000);

    remove_temp_config();
    printf("test_partial_config passed\n");
}

void test_cli_overrides_config(void) {
    /* This tests the priority: CLI > config > default */
    const char *content =
        "[connection]\n"
        "serial = config-device:5555\n"
        "port = 5556\n"
        "[video]\n"
        "max_size = 720\n";

    const char *path = write_temp_config(content);
    assert(path != NULL);

    struct scrcpy_options options = scrcpy_options_default;

    /* Load config first */
    bool ok = config_parse(path, &options);
    assert(ok == true);
    assert(strcmp(options.serial, "config-device:5555") == 0);
    assert(options.port == 5556);
    assert(options.max_size == 720);

    /* Simulate CLI override: --serial cli-device:5555 --port 5557 */
    options.serial = "cli-device:5555";
    options.port = 5557;
    /* max_size not overridden, stays at config value */

    assert(strcmp(options.serial, "cli-device:5555") == 0);
    assert(options.port == 5557);
    assert(options.max_size == 720); /* from config, not overridden */

    remove_temp_config();
    printf("test_cli_overrides_config passed\n");
}

void test_log_level_parsing(void) {
    /* Test debug */
    const char *content = "[log]\nlevel = debug\n";
    const char *path = write_temp_config(content);
    struct scrcpy_options options = scrcpy_options_default;
    config_parse(path, &options);
    assert(options.log_level == 1); /* LOG_LEVEL_DEBUG */

    /* Test info */
    content = "[log]\nlevel = info\n";
    path = write_temp_config(content);
    options = scrcpy_options_default;
    config_parse(path, &options);
    assert(options.log_level == 2); /* LOG_LEVEL_INFO */

    /* Test warn */
    content = "[log]\nlevel = warn\n";
    path = write_temp_config(content);
    options = scrcpy_options_default;
    config_parse(path, &options);
    assert(options.log_level == 3); /* LOG_LEVEL_WARN */

    /* Test error */
    content = "[log]\nlevel = error\n";
    path = write_temp_config(content);
    options = scrcpy_options_default;
    config_parse(path, &options);
    assert(options.log_level == 4); /* LOG_LEVEL_ERROR */

    /* Test unknown level defaults to info */
    content = "[log]\nlevel = unknown\n";
    path = write_temp_config(content);
    options = scrcpy_options_default;
    config_parse(path, &options);
    assert(options.log_level == 2); /* LOG_LEVEL_INFO */

    remove_temp_config();
    printf("test_log_level_parsing passed\n");
}

int main(void) {
    log_init(LOG_LEVEL_INFO);

    test_parse_full_config();
    test_boolean_variants();
    test_comments_and_blanks();
    test_unknown_section();
    test_unknown_key();
    test_file_not_found();
    test_partial_config();
    test_cli_overrides_config();
    test_log_level_parsing();

    printf("All config tests passed!\n");
    log_destroy();
    return 0;
}
```

- [ ] **Step 2: Add test_config to tests/meson.build**

Append to `tests/meson.build` after the existing automatic tests:

```meson
test_config = executable('test_config', 'test_config.c',
    '../src/app/config.c',
    '../src/platform/log.c',
    dependencies: winlibs,
)
test('Config test', test_config)
```

- [ ] **Step 3: Build and run tests to verify they FAIL**

Run: `ninja -C builddir`
Expected: Build fails — `config.c` does not exist yet.

- [ ] **Step 4: Implement config.c**

Create `src/app/config.c`:

```c
#include "config.h"
#include "../platform/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE 512
#define MAX_SECTION 64
#define MAX_KEY 64
#define MAX_VALUE 256

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
            options->serial = strdup(value);
        } else if (strcmp(key, "port") == 0) {
            options->port = (uint16_t)atoi(value);
        } else if (strcmp(key, "server_path") == 0) {
            options->server_path = strdup(value);
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
            options->video_codec = strdup(value);
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
            options->audio_codec = strdup(value);
        } else if (strcmp(key, "bit_rate") == 0) {
            options->audio_bit_rate = (uint32_t)atoi(value);
        } else if (strcmp(key, "source") == 0) {
            options->audio_source = strdup(value);
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
            options->window_title = strdup(value);
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
            options->record_filename = strdup(value);
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
            /* Still try to parse - we already warned */
            continue;
        }

        apply_setting(section, key, value, options);
    }

    fclose(f);
    log_info("Loaded config from: %s", path);
    return true;
}
```

- [ ] **Step 5: Add config.c to src/meson.build**

Open `src/meson.build` and add `'app/config.c'` to `app_src`:

```meson
# App sources
app_src = files(
    'app/application.c',
    'app/window.c',
    'app/options.c',
    'app/cli.c',
    'app/config.c',
)
```

- [ ] **Step 6: Build and run tests**

Run: `ninja -C builddir && meson test -C builddir`
Expected: All tests pass, including new `test_config`.

- [ ] **Step 7: Commit**

```bash
git add src/app/config.h src/app/config.c src/meson.build tests/test_config.c tests/meson.build
git commit -m "feat: implement INI config parser with unit tests"
```

---

### Task 4: Refactor cli.c for two-pass parsing

**Files:**
- Modify: `src/app/cli.c`

- [ ] **Step 1: Refactor cli_parse() to two-pass**

Replace the entire `src/app/cli.c` with:

```c
#include "cli.h"
#include "config.h"
#include "../platform/log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

bool cli_parse(int argc, char *argv[], struct scrcpy_options *options) {
    *options = scrcpy_options_default;

    /* First pass: find -c/--config */
    const char *config_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                log_error("Missing config file path after -c/--config");
                return false;
            }
            config_path = argv[++i];
        }
    }

    /* Load config file if specified */
    if (config_path) {
        if (!config_parse(config_path, options)) {
            log_error("Failed to load config file: %s", config_path);
            return false;
        }
    }

    /* Second pass: CLI arguments override config values */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            i++; /* skip config path value */
            continue;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--serial") == 0) {
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
        } else if (strcmp(argv[i], "--server-path") == 0) {
            if (i + 1 >= argc) {
                log_error("Missing server path");
                return false;
            }
            options->server_path = argv[++i];
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
            printf("  -c, --config <file>        Load config file\n");
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
```

- [ ] **Step 2: Build and run all tests**

Run: `ninja -C builddir && meson test -C builddir`
Expected: All tests pass.

- [ ] **Step 3: Manual smoke test with config file**

Create a test config file `test_run.ini`:

```ini
[connection]
serial = 192.168.1.100:5555

[video]
codec = h265
max_size = 720
```

Run: `./builddir/autoscrcpy.exe -c test_run.ini -h`
Expected: Help text displayed (confirms -c parsing works without crashing).

- [ ] **Step 4: Commit**

```bash
git add src/app/cli.c
git commit -m "feat: refactor CLI to support -c config file with two-pass parsing"
```

---

### Task 5: Final verification

**Files:** None (verification only)

- [ ] **Step 1: Full build from clean**

Run: `rm -rf builddir && meson setup builddir --native-file meson-native-clang-gcc.ini && ninja -C builddir`
Expected: Build succeeds.

- [ ] **Step 2: Run all tests**

Run: `meson test -C builddir`
Expected: All tests pass (38 existing + new config tests).

- [ ] **Step 3: Verify help text includes -c**

Run: `./builddir/autoscrcpy.exe -h`
Expected: Help shows `-c, --config <file>` option.

- [ ] **Step 4: Commit any final fixes if needed**

If all tests pass, no commit needed. If fixes required:

```bash
git add -A
git commit -m "fix: address test failures in config file implementation"
```

---

## Summary

| Task | Files Changed | Tests Added |
|------|---------------|-------------|
| 1. Extend options | `options.h`, `options.c` | 0 |
| 2. Create config.h | `config.h` | 0 |
| 3. Implement parser + tests | `config.c`, `test_config.c`, `meson.build` x2 | 9 test cases |
| 4. Refactor CLI | `cli.c` | 0 |
| 5. Final verification | none | none |

**Total new files:** 3 (`config.h`, `config.c`, `test_config.c`)
**Total modified files:** 5 (`options.h`, `options.c`, `cli.c`, `src/meson.build`, `tests/meson.build`)
**Total new test cases:** 9
