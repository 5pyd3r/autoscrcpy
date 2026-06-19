#include "video_renderer.h"
#include "shader_bytecode.h"
#include "../platform/log.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

typedef struct {
    float x, y, z;
    float u, v;
} vertex_t;

/* Full-screen quad vertices (will be scaled by transform matrix for aspect ratio) */
static const vertex_t quad_vertices[] = {
    {-1,  1, 0, 0, 0},
    { 1,  1, 0, 1, 0},
    { 1, -1, 0, 1, 1},
    {-1, -1, 0, 0, 1},
};

static const uint16_t quad_indices[] = {0, 1, 2, 0, 2, 3};

/* Identity transform matrix (row-major, 4x4) */
static const float identity_matrix[16] = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1,
};

bool video_renderer_init(video_renderer_t *renderer, d3d_context_t *ctx) {
    renderer->d3d_ctx = ctx;
    renderer->video_width = 0;
    renderer->video_height = 0;
    renderer->window_width = (uint32_t)ctx->width;
    renderer->window_height = (uint32_t)ctx->height;
    renderer->initialized = false;
    renderer->cb = NULL;
    renderer->sampler = NULL;
    renderer->nv12_tex = NULL;
    renderer->nv12_staging = NULL;
    renderer->y_srv = NULL;
    renderer->uv_srv = NULL;

    /* Create vertex buffer */
    D3D11_BUFFER_DESC vb_desc = {0};
    vb_desc.ByteWidth = sizeof(quad_vertices);
    vb_desc.Usage = D3D11_USAGE_IMMUTABLE;
    vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vb_data = {0};
    vb_data.pSysMem = quad_vertices;

    HRESULT hr = ctx->device->lpVtbl->CreateBuffer(ctx->device, &vb_desc, &vb_data, &renderer->vb);
    if (FAILED(hr)) {
        log_error("Failed to create vertex buffer: 0x%08x", hr);
        return false;
    }

    /* Create index buffer */
    D3D11_BUFFER_DESC ib_desc = {0};
    ib_desc.ByteWidth = sizeof(quad_indices);
    ib_desc.Usage = D3D11_USAGE_IMMUTABLE;
    ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA ib_data = {0};
    ib_data.pSysMem = quad_indices;

    hr = ctx->device->lpVtbl->CreateBuffer(ctx->device, &ib_desc, &ib_data, &renderer->ib);
    if (FAILED(hr)) {
        log_error("Failed to create index buffer: 0x%08x", hr);
        return false;
    }

    /* Create constant buffer for transform matrix */
    D3D11_BUFFER_DESC cb_desc = {0};
    cb_desc.ByteWidth = 64; /* 4x4 float matrix */
    cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    D3D11_SUBRESOURCE_DATA cb_data = {0};
    cb_data.pSysMem = identity_matrix;

    hr = ctx->device->lpVtbl->CreateBuffer(ctx->device, &cb_desc, &cb_data, &renderer->cb);
    if (FAILED(hr)) {
        log_error("Failed to create constant buffer: 0x%08x", hr);
        return false;
    }

    /* Create sampler state (clamp to edge, bilinear) */
    D3D11_SAMPLER_DESC samp_desc = {0};
    samp_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samp_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samp_desc.MinLOD = 0;
    samp_desc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = ctx->device->lpVtbl->CreateSamplerState(ctx->device, &samp_desc, &renderer->sampler);
    if (FAILED(hr)) {
        log_error("Failed to create sampler state: 0x%08x", hr);
        return false;
    }

    /* Load shaders from bytecode */
    if (vs_bytecode_size > 4 && ps_nv12_bytecode_size > 4) {
        if (!shader_init_from_bytecode(&renderer->shader, ctx->device,
                                        vs_bytecode, vs_bytecode_size,
                                        ps_nv12_bytecode, ps_nv12_bytecode_size)) {
            log_error("Failed to load shaders from bytecode");
            return false;
        }
    } else {
        log_warn("Shader bytecode not available");
        return false;
    }

    renderer->initialized = true;
    return true;
}

/* Create/recreate the NV12 textures (shader resource + staging) and SRVs */
static bool ensure_nv12_texture(video_renderer_t *renderer, uint32_t width, uint32_t height) {
    if (renderer->video_width == width && renderer->video_height == height) {
        return true;
    }

    /* Release old resources */
    if (renderer->y_srv)  { renderer->y_srv->lpVtbl->Release(renderer->y_srv);  renderer->y_srv = NULL; }
    if (renderer->uv_srv) { renderer->uv_srv->lpVtbl->Release(renderer->uv_srv); renderer->uv_srv = NULL; }
    if (renderer->nv12_tex) { renderer->nv12_tex->lpVtbl->Release(renderer->nv12_tex); renderer->nv12_tex = NULL; }
    if (renderer->nv12_staging) { renderer->nv12_staging->lpVtbl->Release(renderer->nv12_staging); renderer->nv12_staging = NULL; }

    /* Create NV12 shader resource texture (GPU-readable, DEFAULT usage) */
    D3D11_TEXTURE2D_DESC desc = {0};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = renderer->d3d_ctx->device->lpVtbl->CreateTexture2D(
        renderer->d3d_ctx->device, &desc, NULL, &renderer->nv12_tex);
    if (FAILED(hr)) {
        log_error("Failed to create NV12 texture: 0x%08x", hr);
        return false;
    }

    /* Create NV12 staging texture (CPU-writable, for uploading frame data) */
    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = renderer->d3d_ctx->device->lpVtbl->CreateTexture2D(
        renderer->d3d_ctx->device, &staging_desc, NULL, &renderer->nv12_staging);
    if (FAILED(hr)) {
        log_error("Failed to create NV12 staging texture: 0x%08x", hr);
        return false;
    }

    /* Create luminance SRV (R8_UNORM reads the Y plane) */
    D3D11_SHADER_RESOURCE_VIEW_DESC y_srv_desc = {0};
    y_srv_desc.Format = DXGI_FORMAT_R8_UNORM;
    y_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    y_srv_desc.Texture2D.MostDetailedMip = 0;
    y_srv_desc.Texture2D.MipLevels = 1;

    hr = renderer->d3d_ctx->device->lpVtbl->CreateShaderResourceView(
        renderer->d3d_ctx->device, (ID3D11Resource *)renderer->nv12_tex,
        &y_srv_desc, &renderer->y_srv);
    if (FAILED(hr)) {
        log_error("Failed to create Y SRV: 0x%08x", hr);
        return false;
    }

    /* Create chrominance SRV (R8G8_UNORM reads the UV plane) */
    D3D11_SHADER_RESOURCE_VIEW_DESC uv_srv_desc = {0};
    uv_srv_desc.Format = DXGI_FORMAT_R8G8_UNORM;
    uv_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    uv_srv_desc.Texture2D.MostDetailedMip = 0;
    uv_srv_desc.Texture2D.MipLevels = 1;

    hr = renderer->d3d_ctx->device->lpVtbl->CreateShaderResourceView(
        renderer->d3d_ctx->device, (ID3D11Resource *)renderer->nv12_tex,
        &uv_srv_desc, &renderer->uv_srv);
    if (FAILED(hr)) {
        log_error("Failed to create UV SRV: 0x%08x", hr);
        return false;
    }

    renderer->video_width = width;
    renderer->video_height = height;

    log_info("Created NV12 texture: %ux%u", width, height);
    return true;
}

/* Update the NV12 texture with new frame data via staging texture.
 * NV12 layout: Y plane (width * height bytes) + UV plane (width * height/2 bytes).
 * The staging texture is mapped, data is copied in, then CopyResource uploads to GPU. */
static bool update_nv12_texture(video_renderer_t *renderer,
                                 const uint8_t *nv12_data,
                                 uint32_t width, uint32_t height) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = renderer->d3d_ctx->device_ctx->lpVtbl->Map(
        renderer->d3d_ctx->device_ctx, (ID3D11Resource *)renderer->nv12_staging,
        0, D3D11_MAP_WRITE, 0, &mapped);
    if (FAILED(hr)) return false;

    /* Copy Y plane */
    const uint8_t *y_src = nv12_data;
    for (uint32_t row = 0; row < height; row++) {
        memcpy((uint8_t *)mapped.pData + row * mapped.RowPitch,
               y_src + row * width, width);
    }

    /* Copy UV plane (starts after Y plane in NV12, height/2 rows, width bytes each) */
    const uint8_t *uv_src = nv12_data + width * height;
    uint8_t *uv_dst = (uint8_t *)mapped.pData + mapped.RowPitch * height;
    for (uint32_t row = 0; row < height / 2; row++) {
        memcpy(uv_dst + row * mapped.RowPitch,
               uv_src + row * width, width);
    }

    renderer->d3d_ctx->device_ctx->lpVtbl->Unmap(
        renderer->d3d_ctx->device_ctx, (ID3D11Resource *)renderer->nv12_staging, 0);

    /* Copy staging texture to shader resource texture */
    renderer->d3d_ctx->device_ctx->lpVtbl->CopyResource(
        renderer->d3d_ctx->device_ctx,
        (ID3D11Resource *)renderer->nv12_tex,
        (ID3D11Resource *)renderer->nv12_staging);

    return true;
}

/* Update the constant buffer with an aspect-ratio-preserving transform matrix.
 * Matches reference VideoQuad::UpdateByRatio logic. */
static void update_aspect_ratio_transform(video_renderer_t *renderer) {
    if (renderer->video_width == 0 || renderer->video_height == 0) return;
    if (renderer->window_width == 0 || renderer->window_height == 0) return;

    double src_ratio = (double)renderer->video_width / renderer->video_height;
    double dst_ratio = (double)renderer->window_width / renderer->window_height;

    float sx = 1.0f, sy = 1.0f;
    if (src_ratio > dst_ratio) {
        /* Video is wider than window → shrink Y (letterbox top/bottom) */
        sy = (float)(dst_ratio / src_ratio);
    } else if (src_ratio < dst_ratio) {
        /* Video is taller than window → shrink X (pillarbox left/right) */
        sx = (float)(src_ratio / dst_ratio);
    }

    /* Build scale matrix (row-major) */
    float matrix[16] = {
        sx,  0,  0,  0,
         0, sy,  0,  0,
         0,  0,  1,  0,
         0,  0,  0,  1,
    };

    /* Upload to constant buffer */
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = renderer->d3d_ctx->device_ctx->lpVtbl->Map(
        renderer->d3d_ctx->device_ctx, (ID3D11Resource *)renderer->cb,
        0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        memcpy(mapped.pData, matrix, 64);
        renderer->d3d_ctx->device_ctx->lpVtbl->Unmap(
            renderer->d3d_ctx->device_ctx, (ID3D11Resource *)renderer->cb, 0);
    }
}

bool video_renderer_render(video_renderer_t *renderer, const video_frame_t *frame) {
    if (!renderer->initialized || !frame || !frame->data) return false;

    /* Ensure NV12 texture is created for this frame size */
    if (!ensure_nv12_texture(renderer, frame->width, frame->height)) {
        return false;
    }

    /* Update NV12 texture with frame data */
    if (!update_nv12_texture(renderer, frame->data, frame->width, frame->height)) {
        return false;
    }

    /* Update aspect ratio transform */
    update_aspect_ratio_transform(renderer);

    /* Bind shader + input layout */
    shader_bind(&renderer->shader, renderer->d3d_ctx->device_ctx);

    /* Bind NV12 SRVs: slot 0 = luminance (Y), slot 1 = chrominance (UV) */
    renderer->d3d_ctx->device_ctx->lpVtbl->PSSetShaderResources(
        renderer->d3d_ctx->device_ctx, 0, 1, &renderer->y_srv);
    renderer->d3d_ctx->device_ctx->lpVtbl->PSSetShaderResources(
        renderer->d3d_ctx->device_ctx, 1, 1, &renderer->uv_srv);

    /* Bind sampler */
    renderer->d3d_ctx->device_ctx->lpVtbl->PSSetSamplers(
        renderer->d3d_ctx->device_ctx, 0, 1, &renderer->sampler);

    /* Bind constant buffer (transform matrix) */
    renderer->d3d_ctx->device_ctx->lpVtbl->VSSetConstantBuffers(
        renderer->d3d_ctx->device_ctx, 0, 1, &renderer->cb);

    /* Set vertex and index buffers */
    UINT stride = sizeof(vertex_t);
    UINT offset = 0;
    renderer->d3d_ctx->device_ctx->lpVtbl->IASetVertexBuffers(
        renderer->d3d_ctx->device_ctx, 0, 1, &renderer->vb, &stride, &offset);
    renderer->d3d_ctx->device_ctx->lpVtbl->IASetIndexBuffer(
        renderer->d3d_ctx->device_ctx, renderer->ib, DXGI_FORMAT_R16_UINT, 0);
    renderer->d3d_ctx->device_ctx->lpVtbl->IASetPrimitiveTopology(
        renderer->d3d_ctx->device_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    /* Draw */
    renderer->d3d_ctx->device_ctx->lpVtbl->DrawIndexed(
        renderer->d3d_ctx->device_ctx, 6, 0, 0);

    return true;
}

void video_renderer_set_window_size(video_renderer_t *renderer, uint32_t width, uint32_t height) {
    renderer->window_width = width;
    renderer->window_height = height;
}

void video_renderer_destroy(video_renderer_t *renderer) {
    shader_destroy(&renderer->shader);
    if (renderer->y_srv)  { renderer->y_srv->lpVtbl->Release(renderer->y_srv);  renderer->y_srv = NULL; }
    if (renderer->uv_srv) { renderer->uv_srv->lpVtbl->Release(renderer->uv_srv); renderer->uv_srv = NULL; }
    if (renderer->nv12_tex) { renderer->nv12_tex->lpVtbl->Release(renderer->nv12_tex); renderer->nv12_tex = NULL; }
    if (renderer->nv12_staging) { renderer->nv12_staging->lpVtbl->Release(renderer->nv12_staging); renderer->nv12_staging = NULL; }
    if (renderer->vb) renderer->vb->lpVtbl->Release(renderer->vb);
    if (renderer->ib) renderer->ib->lpVtbl->Release(renderer->ib);
    if (renderer->cb) renderer->cb->lpVtbl->Release(renderer->cb);
    if (renderer->sampler) renderer->sampler->lpVtbl->Release(renderer->sampler);
}
