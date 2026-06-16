#include "application.h"
#include "../platform/log.h"
#include "../adb/adb.h"
#include "../device/server.h"
#include <string.h>

bool application_init(application_t *app, const struct scrcpy_options *options) {
    app->options = *options;
    app->running = false;
    app->video_decoder = NULL;
    app->audio_decoder = NULL;

    /* Zero-init sockets */
    memset(&app->video_sock, 0, sizeof(app->video_sock));
    memset(&app->audio_sock, 0, sizeof(app->audio_sock));
    memset(&app->control_sock, 0, sizeof(app->control_sock));

    /* Initialize ADB */
    if (!adb_init()) {
        log_error("Failed to initialize ADB");
        return false;
    }

    /* Initialize window */
    if (!window_init(&app->window, GetModuleHandle(NULL), options->window_title,
                     800, 600)) {
        log_error("Failed to initialize window");
        return false;
    }

    /* Initialize D3D context */
    if (!d3d_context_init(&app->d3d_ctx, app->window.hwnd, 800, 600)) {
        log_error("Failed to initialize D3D context");
        return false;
    }

    /* Initialize video renderer */
    if (!video_renderer_init(&app->renderer, &app->d3d_ctx)) {
        log_error("Failed to initialize video renderer");
        return false;
    }

    /* Initialize decoders */
    app->video_decoder = video_decoder_create();
    if (!app->video_decoder) {
        log_error("Failed to create video decoder");
        return false;
    }

    app->audio_decoder = audio_decoder_create();
    if (!app->audio_decoder) {
        log_error("Failed to create audio decoder");
        return false;
    }

    return true;
}

int application_run(application_t *app) {
    /* Push server to device */
    struct server_config server_cfg = {
        .serial = app->options.serial,
        .server_path = app->options.server_path,
        .local_port = app->options.port,
        .max_size = app->options.max_size,
        .video_bit_rate = app->options.video_bit_rate,
        .audio_bit_rate = app->options.audio_bit_rate,
        .video = app->options.video,
        .audio = app->options.audio,
        .control = app->options.control,
    };

    if (!server_push(&server_cfg)) {
        log_error("Failed to push server");
        return 1;
    }

    /* Start server */
    if (!server_start(&server_cfg)) {
        log_error("Failed to start server");
        return 1;
    }

    /* Show window */
    window_show(&app->window);
    if (app->options.fullscreen) {
        window_set_fullscreen(&app->window, true);
    }
    if (app->options.always_on_top) {
        window_set_always_on_top(&app->window, true);
    }

    /* Main loop */
    app->running = true;
    MSG msg;
    while (app->running && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        /* Render frame */
        d3d_context_begin_frame(&app->d3d_ctx);

        /*
         * TODO: Read video frame from video_socket and render.
         * Requires wiring up sockets after server handshake and
         * feeding decoded frames to the renderer.
         */

        d3d_context_end_frame(&app->d3d_ctx);
    }

    return 0;
}

void application_destroy(application_t *app) {
    if (app->video_decoder) {
        video_decoder_destroy(app->video_decoder);
        app->video_decoder = NULL;
    }
    if (app->audio_decoder) {
        audio_decoder_destroy(app->audio_decoder);
        app->audio_decoder = NULL;
    }

    video_renderer_destroy(&app->renderer);
    d3d_context_destroy(&app->d3d_ctx);
    window_destroy(&app->window);

    video_socket_destroy(&app->video_sock);
    audio_socket_destroy(&app->audio_sock);
    control_socket_destroy(&app->control_sock);

    adb_destroy();
}
