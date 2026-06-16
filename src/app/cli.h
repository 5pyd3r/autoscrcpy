#ifndef APP_CLI_H
#define APP_CLI_H

#include <stdbool.h>

struct scrcpy_options {
    /* options will be populated in future tasks */
};

bool cli_parse(int argc, char *argv[], struct scrcpy_options *options);

#endif /* APP_CLI_H */
