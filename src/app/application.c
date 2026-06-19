#include "application.h"
#include "../platform/log.h"
#include "../adb/adb.h"
#include "../device/server.h"
#include "../input/keycode_map.h"
#include "../input/input_transform.h"
#include "../control/control_msg.h"
#include <string.h>
#include <stdio.h>

static void on_key_event(uint32_t vk, bool down, void *userdata);
static void on_mouse_event(int32_t x, int32_t y, uint32_t buttons,
                            uint32_t action, void *userdata);
static void on_wheel_event(int32_t x, int32_t y, int32_t delta, void *userdata);
static void on_resize(int32_t width, int32_t height, void *userdata);

/* Video thread: reads H.264 from socketpair, decodes, puts frame in shared buffer.
 * NO D3D here — rendering is on the main thread (DXGI requirement). */
static DWORD WINAPI video_thread_func(LPVOID arg) {
    application_t *app = (application_t *)arg;
    uint8_t buf[128 * 1024];

    while (app->running) {
        int n = recv(app->video_sock.fd, (char *)buf, sizeof(buf), 0);
        if (n <= 0) break;

        video_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        if (video_decoder_decode(app->video_decoder, buf, (uint32_t)n, &frame)) {
            /* Latest-frame-wins: overwrite shared buffer */
            shared_frame_t *sf = &app->shared_frame;
            uint8_t *old = InterlockedExchangePointer(
                (volatile PVOID *)&sf->data, frame.data);
            sf->width = frame.width;
            sf->height = frame.height;
            InterlockedExchange(&sf->ready, 1);
            if (old) free(old);
            frame.data = NULL;
        }
    }
    return 0;
}

static DWORD WINAPI audio_receiver_thread(LPVOID arg) {
    application_t *app = (application_t *)arg;
    while (app->running) {
        uint8_t *data = NULL;
        uint32_t size = 0;
        if (!audio_socket_read_packet(&app->audio_sock, &data, &size)) break;
        audio_frame_t aframe;
        memset(&aframe, 0, sizeof(aframe));
        if (audio_decoder_decode(app->audio_decoder, data, size, &aframe)) {
            if (app->audio_player && aframe.data)
                audio_player_write(app->audio_player, aframe.data, aframe.size);
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
    app->audio_player = NULL;
    app->video_thread = NULL;
    app->audio_thread = NULL;
    app->stop_event = NULL;
    app->device_width = 0;
    app->device_height = 0;
    memset(&app->video_sock, 0, sizeof(app->video_sock));
    memset(&app->audio_sock, 0, sizeof(app->audio_sock));
    memset(&app->control_sock, 0, sizeof(app->control_sock));
    memset(&app->shared_frame, 0, sizeof(app->shared_frame));
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
    if (!audio_player_init(app->audio_player, 48000, 2)) {
        log_warn("Audio player init failed (WASAPI unavailable)");
        /* Non-fatal: audio just won't work */
    }

    app->stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    app->video_sock.fd = INVALID_SOCKFD;
    app->audio_sock.fd = INVALID_SOCKFD;
    app->control_sock.fd = INVALID_SOCKFD;
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
        log_error("Server start failed");
        return 1;
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

    window_show(&app->window);
    app->running = true;

    if (app->options.video)
        app->video_thread = CreateThread(NULL, 0, video_thread_func, app, 0, NULL);
    if (app->options.audio && app->audio_sock.fd != INVALID_SOCKFD)
        app->audio_thread = CreateThread(NULL, 0, audio_receiver_thread, app, 0, NULL);

    /* Main message loop: PeekMessage (non-blocking) + idle render.
     * Pattern from reference/d3d_video MessageLoop. */
    MSG msg = {};
    int render_count = 0;
    while (app->running) {
        BOOL hasMsg = PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
        if (hasMsg) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            /* Idle: render latest decoded frame */
            if (app->shared_frame.ready) {
                shared_frame_t *sf = &app->shared_frame;
                /* Atomically take ownership of the frame data */
                video_frame_t frame = {0};
                frame.data = InterlockedExchangePointer(
                    (volatile PVOID *)&sf->data, NULL);
                frame.width = sf->width;
                frame.height = sf->height;
                InterlockedExchange(&sf->ready, 0);

                render_count++;
                if (render_count <= 3 || render_count % 300 == 0)
                    log_info("Render #%d: %ux%u", render_count, frame.width, frame.height);

                d3d_context_begin_frame(&app->d3d_ctx);
                video_renderer_render(&app->renderer, &frame);
                d3d_context_end_frame(&app->d3d_ctx);
                video_frame_free(&frame);
            } else {
                /* No frame ready, sleep briefly to avoid busy-spin */
                Sleep(1);
            }
        }
    }
    server_kill(&app->server);

    if (app->video_thread) {
        WaitForSingleObject(app->video_thread, 3000);
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
    if (app->stop_event) { CloseHandle(app->stop_event); app->stop_event = NULL; }
    if (app->video_decoder) { video_decoder_destroy(app->video_decoder); app->video_decoder = NULL; }
    if (app->audio_decoder) { audio_decoder_destroy(app->audio_decoder); app->audio_decoder = NULL; }
    if (app->audio_player) { audio_player_destroy(app->audio_player); app->audio_player = NULL; }
    video_renderer_destroy(&app->renderer);
    d3d_context_destroy(&app->d3d_ctx);
    if (app->shared_frame.data) { free(app->shared_frame.data); app->shared_frame.data = NULL; }
    if (app->video_sock.fd != INVALID_SOCKFD) { video_socket_destroy(&app->video_sock); app->video_sock.fd = INVALID_SOCKFD; }
    if (app->audio_sock.fd != INVALID_SOCKFD) { audio_socket_destroy(&app->audio_sock); app->audio_sock.fd = INVALID_SOCKFD; }
    if (app->control_sock.fd != INVALID_SOCKFD) { control_socket_destroy(&app->control_sock); app->control_sock.fd = INVALID_SOCKFD; }
    adb_destroy();
}

static void on_key_event(uint32_t vk, bool down, void *userdata) {
    application_t *app = (application_t *)userdata;
    if (!app->options.control) return;
    uint32_t kc = vk_to_android_keycode(vk);
    if (kc == 0) return;
    uint8_t buf[64];
    uint32_t args[4] = {down ? 0 : 1, kc, 0, get_android_metastate()};
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_INJECT_KEYCODE, args, buf, sizeof(buf));
    if (len > 0) control_socket_send_msg(&app->control_sock, buf, len);
}

static void on_mouse_event(int32_t x, int32_t y, uint32_t buttons, uint32_t action, void *userdata) {
    application_t *app = (application_t *)userdata;
    if (!app->options.control || !app->device_width || !app->device_height) return;
    int32_t dx, dy;
    input_transform_coords(x, y, &dx, &dy, app->window.width, app->window.height,
                           app->device_width, app->device_height);
    uint32_t aa = (action == 1) ? 0 : (action == 0) ? 1 : 2;
    uint32_t ab = (action == 2) ? 0 : ((buttons & 1) ? 1 : ((buttons & 2) ? 2 : 1));
    uint8_t buf[64];
    uint32_t args[10] = {aa, 0xFFFFFFFF, 0xFFFFFFFF, (uint32_t)dx, (uint32_t)dy,
                         app->device_width, app->device_height, (action == 1) ? 0xFFFF : 0, ab, ab};
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT, args, buf, sizeof(buf));
    if (len > 0) control_socket_send_msg(&app->control_sock, buf, len);
}

static void on_wheel_event(int32_t x, int32_t y, int32_t delta, void *userdata) {
    application_t *app = (application_t *)userdata;
    if (!app->options.control || !app->device_width || !app->device_height) return;
    int32_t dx, dy;
    input_transform_coords(x, y, &dx, &dy, app->window.width, app->window.height,
                           app->device_width, app->device_height);
    uint8_t buf[64];
    int32_t args[7] = {dx, dy, (int32_t)app->device_width, (int32_t)app->device_height, 0, delta / WHEEL_DELTA, 0};
    uint32_t len = control_msg_serialize(CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT, args, buf, sizeof(buf));
    if (len > 0) control_socket_send_msg(&app->control_sock, buf, len);
}

static void on_resize(int32_t width, int32_t height, void *userdata) {
    application_t *app = (application_t *)userdata;
    if (width > 0 && height > 0) {
        d3d_context_resize(&app->d3d_ctx, width, height);
        video_renderer_set_window_size(&app->renderer, (uint32_t)width, (uint32_t)height);
    }
}
