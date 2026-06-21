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
