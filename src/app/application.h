#ifndef APPLICATION_H
#define APPLICATION_H

#include "options.h"
#include "window.h"
#include "../render/d3d_context.h"
#include "../render/video_renderer.h"
#include "../decode/video_decoder.h"
#include "../decode/audio_decoder.h"
#include "../device/video_socket.h"
#include "../device/audio_socket.h"
#include "../device/control_socket.h"
#include <stdbool.h>

typedef struct {
    struct scrcpy_options options;
    window_t window;
    d3d_context_t d3d_ctx;
    video_renderer_t renderer;
    video_decoder_t *video_decoder;
    audio_decoder_t *audio_decoder;
    video_socket_t video_sock;
    audio_socket_t audio_sock;
    control_socket_t control_sock;
    bool running;
} application_t;

bool application_init(application_t *app, const struct scrcpy_options *options);
int application_run(application_t *app);
void application_destroy(application_t *app);

#endif /* APPLICATION_H */
