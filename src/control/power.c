#include "power.h"
#include "../adb/adb.h"
#include "../platform/log.h"

bool power_init(void) {
    return true;
}

bool power_set_screen_power(void *adb_conn, bool on) {
    if (!adb_conn) {
        log_error("No ADB connection for power control");
        return false;
    }

    const char *cmd = on ? "input keyevent KEYCODE_WAKEUP"
                         : "input keyevent KEYCODE_SLEEP";

    if (!adb_shell((adb_connection_t *)adb_conn, cmd)) {
        log_error("Failed to send power command");
        return false;
    }

    log_info("Screen power: %s", on ? "on" : "off");
    return true;
}

void power_destroy(void) {
}
