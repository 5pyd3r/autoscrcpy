#include <stdio.h>
#include <stdlib.h>
#include <libavutil/log.h>
#include "app/application.h"
#include "app/cli.h"
#include "platform/log.h"

int main(int argc, char *argv[]) {
    /* Initialize logging */
    log_init(LOG_LEVEL_INFO);

    /* Suppress FFmpeg verbose logging (only show errors) */
    av_log_set_level(AV_LOG_ERROR);

    /* Parse command line options */
    struct scrcpy_options options;
    if (!cli_parse(argc, argv, &options)) {
        log_error("Failed to parse command line options");
        return EXIT_FAILURE;
    }

    /* Initialize application */
    application_t app;
    if (!application_init(&app, &options)) {
        log_error("Failed to initialize application");
        return EXIT_FAILURE;
    }

    /* Run application */
    int ret = application_run(&app);

    /* Cleanup */
    application_destroy(&app);
    log_destroy();

    return ret;
}
