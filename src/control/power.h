#ifndef POWER_H
#define POWER_H

#include <stdint.h>
#include <stdbool.h>

bool power_init(void);
bool power_set_screen_power(void *adb_conn, bool on);
void power_destroy(void);

#endif /* POWER_H */
