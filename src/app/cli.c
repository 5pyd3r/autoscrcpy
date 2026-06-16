#include "cli.h"
#include <string.h>

bool cli_parse(int argc, char *argv[], struct scrcpy_options *options) {
    (void)argc;
    (void)argv;
    memset(options, 0, sizeof(*options));
    /* stub - will be implemented in future tasks */
    return true;
}
