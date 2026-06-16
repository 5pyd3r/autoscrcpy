#include "server.h"
#include "../adb/adb.h"
#include "../platform/log.h"
#include <string.h>
#include <stdio.h>

bool server_push(struct server_config *config) {
    if (!config->server_path) {
        log_error("Server path not specified");
        return false;
    }

    log_info("Pushing scrcpy-server to device...");

    // Use ADB push to send server.jar
    adb_connection_t *conn = adb_connect("localhost", config->local_port);
    if (!conn) {
        log_error("Failed to connect to ADB");
        return false;
    }

    bool ret = adb_push(conn, config->server_path, "/data/local/tmp/scrcpy-server.jar");
    adb_disconnect(conn);

    return ret;
}

bool server_start(struct server_config *config) {
    log_info("Starting scrcpy-server...");

    // Build command line
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "CLASSPATH=/data/local/tmp/scrcpy-server.jar "
             "app_process / com.genymobile.scrcpy.Server "
             "scid=%08x "
             "log_level=info "
             "max_size=%u "
             "video_bit_rate=%u "
             "audio_bit_rate=%u "
             "video=%s "
             "audio=%s "
             "control=%s "
             "tunnel_forward=true "
             "send_dummy_byte=true",
             0, // scid
             config->max_size,
             config->video_bit_rate,
             config->audio_bit_rate,
             config->video ? "true" : "false",
             config->audio ? "true" : "false",
             config->control ? "true" : "false");

    // Execute via ADB shell
    adb_connection_t *conn = adb_connect("localhost", config->local_port);
    if (!conn) {
        log_error("Failed to connect to ADB");
        return false;
    }

    bool ret = adb_shell(conn, cmd);
    adb_disconnect(conn);

    return ret;
}

void server_kill(void) {
    log_info("Killing scrcpy-server...");
    // TODO: Implement server kill
}
