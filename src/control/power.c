#include "power.h"
#include "../platform/log.h"

bool power_init(void) {
    return true;
}

bool power_set_screen_power(bool on) {
    // TODO: Implement screen power control
    log_info("Screen power: %s", on ? "on" : "off");
    return true;
}

void power_destroy(void) {
}
