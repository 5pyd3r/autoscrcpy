#include "video_renderer.h"
#include "../platform/log.h"

typedef struct {
    float x, y, z;
    float u, v;
} vertex_t;

static const vertex_t vertices[] = {
    {-1,  1, 0, 0, 0},
    { 1,  1, 0, 1, 0},
    { 1, -1, 0, 1, 1},
    {-1, -1, 0, 0, 1},
};

static const uint16_t indices[] = {0, 1, 2, 0, 2, 3};

bool video_renderer_init(video_renderer_t *renderer, d3d_context_t *ctx) {
    renderer->d3d_ctx = ctx;

    // Create vertex buffer
    D3D11_BUFFER_DESC vb_desc = {0};
    vb_desc.ByteWidth = sizeof(vertices);
    vb_desc.Usage = D3D11_USAGE_IMMUTABLE;
    vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vb_data = {0};
    vb_data.pSysMem = vertices;

    HRESULT hr = ctx->device->lpVtbl->CreateBuffer(ctx->device, &vb_desc, &vb_data, &renderer->vb);
    if (FAILED(hr)) {
        log_error("Failed to create vertex buffer: 0x%08x", hr);
        return false;
    }

    // Create index buffer
    D3D11_BUFFER_DESC ib_desc = {0};
    ib_desc.ByteWidth = sizeof(indices);
    ib_desc.Usage = D3D11_USAGE_IMMUTABLE;
    ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA ib_data = {0};
    ib_data.pSysMem = indices;

    hr = ctx->device->lpVtbl->CreateBuffer(ctx->device, &ib_desc, &ib_data, &renderer->ib);
    if (FAILED(hr)) {
        log_error("Failed to create index buffer: 0x%08x", hr);
        return false;
    }

    // TODO: Load compiled shaders
    // For now, return true and skip shader initialization
    return true;
}

bool video_renderer_render(video_renderer_t *renderer, const video_frame_t *frame) {
    // Update texture with frame data
    if (!texture_update(&renderer->texture, renderer->d3d_ctx->device_ctx,
                        frame->data, frame->width * 4)) {
        return false;
    }

    // Bind shader and texture
    shader_bind(&renderer->shader, renderer->d3d_ctx->device_ctx);
    texture_bind(&renderer->texture, renderer->d3d_ctx->device_ctx, 0);

    // Set vertex and index buffers
    UINT stride = sizeof(vertex_t);
    UINT offset = 0;
    renderer->d3d_ctx->device_ctx->lpVtbl->IASetVertexBuffers(
        renderer->d3d_ctx->device_ctx, 0, 1, &renderer->vb, &stride, &offset);
    renderer->d3d_ctx->device_ctx->lpVtbl->IASetIndexBuffer(
        renderer->d3d_ctx->device_ctx, renderer->ib, DXGI_FORMAT_R16_UINT, 0);
    renderer->d3d_ctx->device_ctx->lpVtbl->IASetPrimitiveTopology(
        renderer->d3d_ctx->device_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Draw
    renderer->d3d_ctx->device_ctx->lpVtbl->DrawIndexed(
        renderer->d3d_ctx->device_ctx, 6, 0, 0);

    return true;
}

void video_renderer_destroy(video_renderer_t *renderer) {
    shader_destroy(&renderer->shader);
    texture_destroy(&renderer->texture);
    if (renderer->vb) renderer->vb->lpVtbl->Release(renderer->vb);
    if (renderer->ib) renderer->ib->lpVtbl->Release(renderer->ib);
}
