#include "application.h"
#include "../platform/log.h"
#include "../adb/adb.h"
#include "../device/server.h"
#include "../input/keycode_map.h"
#include "../input/input_transform.h"
#include "../control/control_msg.h"
#include <string.h>

static void on_key_event(uint32_t vk, bool down, void *userdata);
static void on_mouse_event(int32_t x, int32_t y, uint32_t buttons,
                            uint32_t action, void *userdata);
static void on_wheel_event(int32_t x, int32_t y, int32_t delta, void *userdata);
static void on_resize(int32_t width, int32_t height, void *userdata);

static DWORD WINAPI video_receiver_thread(LPVOID arg) {
    application_t *app = (application_t *)arg;

    while (app->running) {
        uint8_t *data = NULL;
        uint32_t size = 0;

        if (!video_socket_read_packet(&app->video_sock, &data, &size)) {
            if (app->running) log_error("Video socket read failed");
            break;
        }

        if (app->video_sock.codec_id == 0 && size >= 12) {
            app->video_sock.codec_id = *(uint32_t *)data;
            app->video_sock.width = *(uint32_t *)(data + 4);
            app->video_sock.height = *(uint32_t *)(data + 8);
            app->device_width = app->video_sock.width;
            app->device_height = app->video_sock.height;

            video_decoder_init(app->video_decoder, app->video_sock.codec_id,
                               app->video_sock.width, app->video_sock.height);

            free(data);
            continue;
        }

        video_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        if (video_decoder_decode(app->video_decoder, data, size, &frame)) {
            d3d_context_begin_frame(&app->d3d_ctx);
            video_renderer_render(&app->renderer, &frame);
            d3d_context_end_frame(&app->d3d_ctx);
            video_frame_free(&frame);
        }

        free(data);
    }

    return 0;
}

static DWORD WINAPI audio_receiver_thread(LPVOID arg) {
    application_t *app = (application_t *)arg;

    while (app->running) {
        uint8_t *data = NULL;
        uint32_t size = 0;

        if (!audio_socket_read_packet(&app->audio_sock, &data, &size)) {
            if (app->running) log_error("Audio socket read failed");
            break;
        }

        audio_frame_t aframe;
        memset(&aframe, 0, sizeof(aframe));
        if (audio_decoder_decode(app->audio_decoder, data, size, &aframe)) {
            if (aframe.data) free(aframe.data);
        }

        free(data);
    }

    return 0;
}

bool application_init(application_t *app, const struct scrcpy_options *options) {
    app->options = *options;
    app->running = false;
    app->video_decoder = NULL;
    app->audio_decoder = NULL;
    app->video_thread = NULL;
    app->audio_thread = NULL;
    app->stop_event = NULL;
    app->device_width = 0;
    app->device_height = 0;

    memset(&app->video_sock, 0, sizeof(app->video_sock));
    memset(&app->audio_sock, 0, sizeof(app->audio_sock));
    memset(&app->control_sock, 0, sizeof(app->control_sock));

    if (!adb_init()) {
        log_error("Failed to initialize ADB");
        return false;
    }

    if (!window_init(&app->window, GetModuleHandle(NULL), options->window_title,
                     800, 600)) {
        log_error("Failed to initialize window");
        return false;
    }

    window_callbacks_t cbs = {
        .key_cb = on_key_event,
        .mouse_cb = on_mouse_event,
        .wheel_cb = on_wheel_event,
        .resize_cb = on_resize,
        .userdata = app,
    };
    window_set_callbacks(&app->window, &cbs);

    if (!d3d_context_init(&app->d3d_ctx, app->window.hwnd, 800, 600)) {
        log_error("Failed to initialize D3D context");
        return false;
    }

    if (!video_renderer_init(&app->renderer, &app->d3d_ctx)) {
        log_error("Failed to initialize video renderer");
        return false;
    }

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

    app->stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!app->stop_event) {
        log_error("Failed to create stop event");
        return false;
    }

    return true;
}

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
        log_error("Failed to start server");
        return 1;
    }

    window_show(&app->window);
    if (app->options.fullscreen) window_set_fullscreen(&app->window, true);
    if (app->options.always_on_top) window_set_always_on_top(&app->window, true);

    app->running = true;

    if (app->options.video)
        app->video_thread = CreateThread(NULL, 0, video_receiver_thread, app, 0, NULL);
    if (app->options.audio)
        app->audio_thread = CreateThread(NULL, 0, audio_receiver_thread, app, 0, NULL);

    HANDLE events[1] = { app->stop_event };
    while (app->running) {
        DWORD result = MsgWaitForMultipleObjects(
            1, events, FALSE, INFINITE, QS_ALLINPUT);

        if (result == WAIT_OBJECT_0 + 1) {
            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    app->running = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        } else if (result == WAIT_OBJECT_0) {
            app->running = false;
        }
    }

    server_kill(&app->server);

    if (app->video_thread) {
        WaitForSingleObject(app->video_thread, 2000);
        CloseHandle(app->video_thread);
    }
    if (app->audio_thread) {
        WaitForSingleObject(app->audio_thread, 2000);
        CloseHandle(app->audio_thread);
    }

    server_destroy(&app->server);
    return 0;
}

void application_destroy(application_t *app) {
    if (app->stop_event) CloseHandle(app->stop_event);
    if (app->video_decoder) video_decoder_destroy(app->video_decoder);
    if (app->audio_decoder) audio_decoder_destroy(app->audio_decoder);

    video_renderer_destroy(&app->renderer);
    d3d_context_destroy(&app->d3d_ctx);
    window_destroy(&app->window);

    video_socket_destroy(&app->video_sock);
    audio_socket_destroy(&app->audio_sock);
    control_socket_destroy(&app->control_sock);

    adb_destroy();
}

static void on_key_event(uint32_t vk, bool down, void *userdata) {
    application_t *app = (application_t *)userdata;
    if (!app->options.control) return;

    uint32_t android_keycode = vk_to_android_keycode(vk);
    if (android_keycode == 0) return;

    uint32_t metastate = get_android_metastate();
    uint32_t action = down ? 0 : 1;

    uint8_t buf[64];
    uint32_t args[4] = {action, android_keycode, 0, metastate};
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_INJECT_KEYCODE,
                                          args, buf, sizeof(buf));
    if (len > 0) control_socket_send_msg(&app->control_sock, buf, len);
}

static void on_mouse_event(int32_t x, int32_t y, uint32_t buttons,
                            uint32_t action, void *userdata) {
    application_t *app = (application_t *)userdata;
    if (!app->options.control) return;
    if (app->device_width == 0 || app->device_height == 0) return;

    int32_t dev_x, dev_y;
    input_transform_coords(x, y, &dev_x, &dev_y,
                           app->window.width, app->window.height,
                           app->device_width, app->device_height);

    uint32_t android_action;
    uint32_t android_button;
    if (action == 1) {
        android_action = 0;
        android_button = (buttons & 1) ? 1 : ((buttons & 2) ? 2 : 1);
    } else if (action == 0) {
        android_action = 1;
        android_button = (buttons & 1) ? 1 : ((buttons & 2) ? 2 : 1);
    } else {
        android_action = 2;
        android_button = 0;
    }

    uint8_t buf[64];
    uint32_t args[10] = {
        android_action, 0xFFFFFFFF, 0xFFFFFFFF,
        (uint32_t)dev_x, (uint32_t)dev_y,
        app->device_width, app->device_height,
        (action == 1) ? 0xFFFF : 0, android_button, android_button
    };
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT,
                                          args, buf, sizeof(buf));
    if (len > 0) control_socket_send_msg(&app->control_sock, buf, len);
}

static void on_wheel_event(int32_t x, int32_t y, int32_t delta, void *userdata) {
    application_t *app = (application_t *)userdata;
    if (!app->options.control) return;
    if (app->device_width == 0 || app->device_height == 0) return;

    int32_t dev_x, dev_y;
    input_transform_coords(x, y, &dev_x, &dev_y,
                           app->window.width, app->window.height,
                           app->device_width, app->device_height);

    int32_t vscroll = delta / WHEEL_DELTA;

    uint8_t buf[64];
    int32_t args[7] = {dev_x, dev_y, (int32_t)app->device_width,
                        (int32_t)app->device_height, 0, vscroll, 0};
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT,
                                          args, buf, sizeof(buf));
    if (len > 0) control_socket_send_msg(&app->control_sock, buf, len);
}

static void on_resize(int32_t width, int32_t height, void *userdata) {
    application_t *app = (application_t *)userdata;
    if (width > 0 && height > 0)
        d3d_context_resize(&app->d3d_ctx, width, height);
}
