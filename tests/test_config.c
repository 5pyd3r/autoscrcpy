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
