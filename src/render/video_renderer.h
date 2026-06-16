#ifndef VIDEO_RENDERER_H
#define VIDEO_RENDERER_H

#include "d3d_context.h"
#include "shader.h"
#include "texture.h"
#include "../decode/video_decoder.h"
#include <stdbool.h>
#include <d3d11.h>

typedef struct {
    d3d_context_t *d3d_ctx;
    shader_t shader;
    texture_t y_tex;      /* Luminance (R8) */
    texture_t uv_tex;     /* Chrominance (R8G8) */
    ID3D11Buffer *vb;
    ID3D11Buffer *ib;
    ID3D11Buffer *cb;     /* Constant buffer for transform */
    ID3D11SamplerState *sampler;
    uint32_t video_width;
    uint32_t video_height;
    bool initialized;
} video_renderer_t;

bool video_renderer_init(video_renderer_t *renderer, d3d_context_t *ctx);
bool video_renderer_render(video_renderer_t *renderer, const video_frame_t *frame);
void video_renderer_destroy(video_renderer_t *renderer);

#endif /* VIDEO_RENDERER_H */
