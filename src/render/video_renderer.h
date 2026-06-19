#ifndef VIDEO_RENDERER_H
#define VIDEO_RENDERER_H

#include "d3d_context.h"
#include "shader.h"
#include "../decode/video_decoder.h"
#include <stdbool.h>
#include <d3d11.h>

typedef struct {
    d3d_context_t *d3d_ctx;
    shader_t shader;
    ID3D11Texture2D *nv12_tex;         /* NV12 shader resource (DEFAULT usage) */
    ID3D11Texture2D *nv12_staging;     /* NV12 staging texture (CPU-writable) */
    ID3D11ShaderResourceView *y_srv;   /* Luminance SRV (R8_UNORM) */
    ID3D11ShaderResourceView *uv_srv;  /* Chrominance SRV (R8G8_UNORM) */
    ID3D11Buffer *vb;
    ID3D11Buffer *ib;
    ID3D11Buffer *cb;     /* Constant buffer for transform */
    ID3D11SamplerState *sampler;
    uint32_t video_width;
    uint32_t video_height;
    uint32_t window_width;   /* For aspect ratio computation */
    uint32_t window_height;
    bool initialized;
} video_renderer_t;

bool video_renderer_init(video_renderer_t *renderer, d3d_context_t *ctx);
bool video_renderer_render(video_renderer_t *renderer, const video_frame_t *frame);
void video_renderer_set_window_size(video_renderer_t *renderer, uint32_t width, uint32_t height);
void video_renderer_destroy(video_renderer_t *renderer);

#endif /* VIDEO_RENDERER_H */
