#include "application.h"
#include "../platform/log.h"
#include "../adb/adb.h"
#include "../device/server.h"
#include "../script/event_dispatch.h"
#include "../script/bindings.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static void on_key_event(uint32_t vk, bool down, void *userdata);
static void on_mouse_event(int32_t x, int32_t y, uint32_t buttons,
                            uint32_t action, void *userdata);
static void on_wheel_event(int32_t x, int32_t y, int32_t delta, void *userdata);
static void on_resize(int32_t width, int32_t height, void *userdata);

/* Script query callbacks */
static void on_script_query_device_info(script_engine_t *engine, void *ctx);
static void on_script_query_video_size(script_engine_t *engine, void *ctx);
static void on_script_query_window_size(script_engine_t *engine, void *ctx);
static void on_script_query_clipboard(script_engine_t *engine, void *ctx);
static void on_script_query_frame_capture(script_engine_t *engine, void *ctx);

bool application_init(application_t *app, const struct scrcpy_options *options) {
    app->options = *options;
    app->running = false;
    app->video_decoder = NULL;
    app->audio_decoder = NULL;
    app->audio_player = NULL;
    app->device_width = 0;
    app->device_height = 0;
    memset(&app->video_sock, 0, sizeof(app->video_sock));
    memset(&app->audio_sock, 0, sizeof(app->audio_sock));
    memset(&app->control_sock, 0, sizeof(app->control_sock));
    shared_frame_init(&app->shared_frame);
    memset(&app->d3d_ctx, 0, sizeof(app->d3d_ctx));
    memset(&app->renderer, 0, sizeof(app->renderer));

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        log_error("COM init failed: 0x%08x", hr);
        return false;
    }

    if (!adb_init()) { log_error("ADB init failed"); return false; }

    if (!window_init(&app->window, GetModuleHandle(NULL),
                     options->window_title, 800, 600)) {
        log_error("Window init failed");
        return false;
    }

    window_callbacks_t cbs = {
        .key_cb = on_key_event, .mouse_cb = on_mouse_event,
        .wheel_cb = on_wheel_event, .resize_cb = on_resize,
        .userdata = app,
    };
    window_set_callbacks(&app->window, &cbs);

    /* D3D11 on main thread (same thread as window — DXGI requirement) */
    if (!d3d_context_init(&app->d3d_ctx, app->window.hwnd, 800, 600)) {
        log_error("D3D init failed"); return false;
    }
    if (!video_renderer_init(&app->renderer, &app->d3d_ctx)) {
        log_error("Renderer init failed");
        d3d_context_destroy(&app->d3d_ctx);
        return false;
    }

    app->video_decoder = video_decoder_create();
    app->audio_decoder = audio_decoder_create();
    app->audio_player = audio_player_create();
    if (!app->video_decoder || !app->audio_decoder || !app->audio_player) {
        log_error("Decoder/player alloc failed");
        application_destroy(app);
        return false;
    }

    /* Initialize controller */
    controller_init(&app->controller, &app->control_sock);

    /* Initialize pipelines (thread not started yet) */
    video_pipeline_init(&app->video_pipeline, app->video_decoder,
                        &app->video_sock, &app->shared_frame);
    audio_pipeline_init(&app->audio_pipeline, app->audio_decoder,
                        app->audio_player, &app->audio_sock);

    app->video_sock.fd = INVALID_SOCKFD;
    app->audio_sock.fd = INVALID_SOCKFD;
    app->control_sock.fd = INVALID_SOCKFD;

    /* Initialize script engine */
    app->script_enabled = (options->script_path || options->script_eval || options->repl);
    if (app->script_enabled) {
        if (!script_engine_init(&app->script_engine, options->script_path,
                                options->script_eval, NULL, NULL)) {
            log_warn("Script engine init failed, scripting disabled");
            app->script_enabled = false;
        } else {
            repl_window_init(&app->repl_window, GetModuleHandle(NULL), NULL, NULL);
            if (options->repl) {
                repl_window_show(&app->repl_window);
            }
        }
    }

    return true;
}

static void application_resize_window_for_video(application_t *app,
                                                 uint32_t video_w, uint32_t video_h);

int application_run(application_t *app) {
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

    server_init(&app->server, &server_cfg);
    if (!server_start(&app->server, &app->video_sock, &app->audio_sock,
                      &app->control_sock)) {
        log_error("Server start failed");
        return 1;
    }

    /* Initialize audio pipeline if audio channel is available */
    if (app->options.audio && app->audio_sock.fd != INVALID_SOCKFD) {
        if (audio_socket_read_metadata(&app->audio_sock)) {
            if (audio_decoder_init(app->audio_decoder, app->audio_sock.codec_id,
                                   app->audio_sock.sample_rate, app->audio_sock.channels)) {
                if (audio_player_init(app->audio_player, app->audio_sock.sample_rate,
                                      app->audio_sock.channels)) {
                    log_info("Audio pipeline ready");
                } else {
                    log_warn("Audio player init failed (WASAPI unavailable)");
                }
            } else {
                log_error("Audio decoder init failed");
            }
        } else {
            log_warn("Audio metadata read failed, audio disabled");
        }
    }

    if (app->options.video && app->video_sock.codec_id != 0) {
        app->device_width = app->video_sock.width;
        app->device_height = app->video_sock.height;
        if (!video_decoder_init(app->video_decoder, app->video_sock.codec_id,
                                app->video_sock.width, app->video_sock.height)) {
            log_error("Video decoder init failed");
            server_kill(&app->server);
            server_destroy(&app->server);
            return 1;
        }
        log_info("Video: %ux%u", app->video_sock.width, app->video_sock.height);
    }

    /* Configure controller with device dimensions */
    controller_set_device_size(&app->controller, app->device_width, app->device_height);
    controller_set_enabled(&app->controller, app->options.control);

    /* Resize window to match initial video aspect ratio (avoid black bars) */
    if (app->device_width > 0 && app->device_height > 0) {
        application_resize_window_for_video(app, app->device_width, app->device_height);
    }

    window_show(&app->window);
    app->running = true;

    /* Start script engine */
    if (app->script_enabled) {
        script_engine_start(&app->script_engine);
    }

    /* Start pipelines */
    if (app->options.video)
        video_pipeline_start(&app->video_pipeline);
    if (app->options.audio && app->audio_sock.fd != INVALID_SOCKFD)
        audio_pipeline_start(&app->audio_pipeline);

    /* Main message loop: PeekMessage (non-blocking) + idle render.
     * Pattern from reference/d3d_video MessageLoop. */
    MSG msg = {};
    int render_count = 0;
    int fps_count = 0;
    DWORD fps_last_update = GetTickCount();
    char title_buf[256];

    while (app->running) {
        BOOL hasMsg = PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
        if (hasMsg) {
            /* Let REPL window handle its messages first */
            if (app->script_enabled && repl_window_process_message(&app->repl_window, &msg)) {
                continue;
            }
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            /* Idle: render latest decoded frame */
            frame_data_t *fd = shared_frame_acquire(&app->shared_frame);
            if (fd) {
                /* Detect video dimension change (e.g. device rotation) */
                if (fd->width != app->device_width || fd->height != app->device_height) {
                    log_info("Video dimensions changed: %ux%u -> %ux%u",
                             app->device_width, app->device_height, fd->width, fd->height);
                    app->device_width = fd->width;
                    app->device_height = fd->height;
                    controller_set_device_size(&app->controller, fd->width, fd->height);
                    application_resize_window_for_video(app, fd->width, fd->height);
                }

                video_frame_t frame = {0};
                frame.data = fd->data;
                frame.width = fd->width;
                frame.height = fd->height;
                fd->data = NULL; /* Transfer ownership */

                render_count++;
                fps_count++;
                (void)render_count;

                d3d_context_begin_frame(&app->d3d_ctx);
                video_renderer_render(&app->renderer, &frame);
                d3d_context_end_frame(&app->d3d_ctx);

                /* Update window title with video info every second */
                DWORD now = GetTickCount();
                if (now - fps_last_update >= 1000) {
                    float fps = (float)fps_count * 1000.0f / (now - fps_last_update);
                    fps_count = 0;
                    fps_last_update = now;

                    snprintf(title_buf, sizeof(title_buf),
                             "AutoScrcpy - %ux%u @ %.1f FPS | %u kbps",
                             frame.width, frame.height, fps,
                             app->options.video_bit_rate / 1000);
                    SetWindowTextA(app->window.hwnd, title_buf);
                }

                video_frame_free(&frame);
                frame_data_free(fd);
            } else {
                Sleep(1);
            }

            /* Process script engine messages */
            if (app->script_enabled) {
                script_process_messages(&app->script_engine,
                    /* Command callbacks */
                    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                    /* Query callbacks */
                    on_script_query_device_info,
                    on_script_query_video_size,
                    on_script_query_window_size,
                    on_script_query_clipboard,
                    on_script_query_frame_capture,
                    app);
            }
        }
    }
    server_kill(&app->server);

    /* Stop script engine */
    if (app->script_enabled) {
        script_engine_stop(&app->script_engine);
    }

    /* Stop pipelines (graceful shutdown with socket close) */
    video_pipeline_stop(&app->video_pipeline);
    audio_pipeline_stop(&app->audio_pipeline);

    server_destroy(&app->server);
    return 0;
}

void application_destroy(application_t *app) {
    if (app->script_enabled) {
        script_engine_destroy(&app->script_engine);
        repl_window_destroy(&app->repl_window);
    }
    video_pipeline_destroy(&app->video_pipeline);
    audio_pipeline_destroy(&app->audio_pipeline);
    if (app->video_decoder) { video_decoder_destroy(app->video_decoder); app->video_decoder = NULL; }
    if (app->audio_decoder) { audio_decoder_destroy(app->audio_decoder); app->audio_decoder = NULL; }
    if (app->audio_player) { audio_player_destroy(app->audio_player); app->audio_player = NULL; }
    video_renderer_destroy(&app->renderer);
    d3d_context_destroy(&app->d3d_ctx);
    if (app->shared_frame.current) {
        frame_data_free(app->shared_frame.current);
        app->shared_frame.current = NULL;
    }
    if (app->video_sock.fd != INVALID_SOCKFD) { video_socket_destroy(&app->video_sock); app->video_sock.fd = INVALID_SOCKFD; }
    if (app->audio_sock.fd != INVALID_SOCKFD) { audio_socket_destroy(&app->audio_sock); app->audio_sock.fd = INVALID_SOCKFD; }
    if (app->control_sock.fd != INVALID_SOCKFD) { control_socket_destroy(&app->control_sock); app->control_sock.fd = INVALID_SOCKFD; }
    adb_destroy();
}

static void application_resize_window_for_video(application_t *app,
                                                 uint32_t video_w, uint32_t video_h) {
    if (video_w == 0 || video_h == 0) return;

    /* Get current client area to preserve display area across rotations */
    RECT client;
    GetClientRect(app->window.hwnd, &client);
    int cur_w = client.right - client.left;
    int cur_h = client.bottom - client.top;
    if (cur_w <= 0 || cur_h <= 0) return;

    /* Preserve display area: new dimensions give same client-area pixels */
    double area = (double)cur_w * cur_h;
    double aspect = (double)video_w / video_h;
    int new_w = (int)(sqrt(area * aspect) + 0.5);
    int new_h = (int)(sqrt(area / aspect) + 0.5);

    /* Cap at screen bounds */
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (new_w > screen_w) {
        new_w = screen_w;
        new_h = (int)(new_w / aspect + 0.5);
    }
    if (new_h > screen_h) {
        new_h = screen_h;
        new_w = (int)(new_h * aspect + 0.5);
    }

    RECT rect = {0, 0, new_w, new_h};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    SetWindowPos(app->window.hwnd, NULL, 0, 0,
                 rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER);
}

/* Window callbacks — delegate to controller */

static void on_key_event(uint32_t vk, bool down, void *userdata) {
    application_t *app = (application_t *)userdata;
    controller_on_key_event(&app->controller, vk, down);
    /* Dispatch to script engine */
    if (app->script_enabled) {
        script_dispatch_key_event(&app->script_engine, vk, down);
    }
}

static void on_mouse_event(int32_t x, int32_t y, uint32_t buttons,
                            uint32_t action, void *userdata) {
    application_t *app = (application_t *)userdata;
    controller_on_mouse_event(&app->controller, x, y, buttons, action);
}

static void on_wheel_event(int32_t x, int32_t y, int32_t delta, void *userdata) {
    application_t *app = (application_t *)userdata;
    controller_on_wheel_event(&app->controller, x, y, delta);
}

static void on_resize(int32_t width, int32_t height, void *userdata) {
    application_t *app = (application_t *)userdata;
    if (width > 0 && height > 0) {
        d3d_context_resize(&app->d3d_ctx, width, height);
        video_renderer_set_window_size(&app->renderer, (uint32_t)width, (uint32_t)height);
        controller_set_window_size(&app->controller, (uint32_t)width, (uint32_t)height);
    }
}

/* Script query callbacks — called from main thread, send response to script engine */

static void on_script_query_device_info(script_engine_t *engine, void *ctx) {
    application_t *app = (application_t *)ctx;
    script_msg_t resp;
    script_fill_device_info(&resp, app->device_width, app->device_height,
                            "Android Device", /* TODO: get real device name */
                            app->video_sock.fd != INVALID_SOCKFD);
    script_msg_queue_send(&engine->response_q, &resp);
}

static void on_script_query_video_size(script_engine_t *engine, void *ctx) {
    application_t *app = (application_t *)ctx;
    script_msg_t resp;
    script_fill_video_size(&resp, app->device_width, app->device_height);
    script_msg_queue_send(&engine->response_q, &resp);
}

static void on_script_query_window_size(script_engine_t *engine, void *ctx) {
    application_t *app = (application_t *)ctx;
    RECT client;
    GetClientRect(app->window.hwnd, &client);
    script_msg_t resp;
    script_fill_window_size(&resp, client.right - client.left, client.bottom - client.top);
    script_msg_queue_send(&engine->response_q, &resp);
}

static void on_script_query_clipboard(script_engine_t *engine, void *ctx) {
    (void)ctx;
    script_msg_t resp;
    /* TODO: read device clipboard via control socket */
    script_fill_clipboard(&resp, "");
    script_msg_queue_send(&engine->response_q, &resp);
}

static void on_script_query_frame_capture(script_engine_t *engine, void *ctx) {
    application_t *app = (application_t *)ctx;
    script_msg_t resp;

    frame_data_t *fd = shared_frame_acquire(&app->shared_frame);
    if (fd && fd->data && fd->width > 0 && fd->height > 0) {
        /* NV12 size = width * height * 3/2 */
        uint32_t nv12_size = fd->width * fd->height * 3 / 2;
        script_fill_frame_capture(&resp, fd->data, fd->width, fd->height, nv12_size);
        frame_data_free(fd);
    } else {
        script_fill_frame_capture(&resp, NULL, 0, 0, 0);
        if (fd) frame_data_free(fd);
    }

    script_msg_queue_send(&engine->response_q, &resp);
}
