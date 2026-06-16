#ifndef CLI_H
#define CLI_H

#include "options.h"
#include <stdbool.h>

bool cli_parse(int argc, char *argv[], struct scrcpy_options *options);

#endif /* CLI_H */
