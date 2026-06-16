#include <stdio.h>
#include <stdlib.h>
#include "app/application.h"
#include "app/cli.h"
#include "platform/log.h"

int main(int argc, char *argv[]) {
    // Initialize logging
    log_init(LOG_LEVEL_INFO);

    // Parse command line options
    struct scrcpy_options options;
    if (!cli_parse(argc, argv, &options)) {
        log_error("Failed to parse command line options");
        return EXIT_FAILURE;
    }

    // Run application
    int ret = application_run(&options);

    // Cleanup
    log_destroy();

    return ret;
}
