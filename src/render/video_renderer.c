#include "video_renderer.h"
#include "shader_bytecode.h"
#include "../platform/log.h"
#include <string.h>

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

/* Identity transform matrix (row-major) */
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
    renderer->initialized = false;
    renderer->cb = NULL;
    renderer->sampler = NULL;

    /* Create vertex buffer */
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

    /* Create index buffer */
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

    /* Create sampler state */
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

static bool ensure_textures(video_renderer_t *renderer, uint32_t width, uint32_t height) {
    if (renderer->video_width == width && renderer->video_height == height) {
        return true;
    }

    /* Destroy old textures */
    texture_destroy(&renderer->y_tex);
    texture_destroy(&renderer->uv_tex);

    /* Create Y texture (R8_UNORM, full resolution) */
    D3D11_TEXTURE2D_DESC y_desc = {0};
    y_desc.Width = width;
    y_desc.Height = height;
    y_desc.MipLevels = 1;
    y_desc.ArraySize = 1;
    y_desc.Format = DXGI_FORMAT_R8_UNORM;
    y_desc.SampleDesc.Count = 1;
    y_desc.Usage = D3D11_USAGE_DYNAMIC;
    y_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    y_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = renderer->d3d_ctx->device->lpVtbl->CreateTexture2D(
        renderer->d3d_ctx->device, &y_desc, NULL, &renderer->y_tex.texture);
    if (FAILED(hr)) {
        log_error("Failed to create Y texture: 0x%08x", hr);
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC y_srv_desc = {0};
    y_srv_desc.Format = y_desc.Format;
    y_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    y_srv_desc.Texture2D.MipLevels = 1;

    hr = renderer->d3d_ctx->device->lpVtbl->CreateShaderResourceView(
        renderer->d3d_ctx->device, (ID3D11Resource *)renderer->y_tex.texture,
        &y_srv_desc, &renderer->y_tex.srv);
    if (FAILED(hr)) {
        log_error("Failed to create Y SRV: 0x%08x", hr);
        return false;
    }
    renderer->y_tex.width = width;
    renderer->y_tex.height = height;

    /* Create UV texture (R8G8_UNORM, half resolution) */
    D3D11_TEXTURE2D_DESC uv_desc = {0};
    uv_desc.Width = width / 2;
    uv_desc.Height = height / 2;
    uv_desc.MipLevels = 1;
    uv_desc.ArraySize = 1;
    uv_desc.Format = DXGI_FORMAT_R8G8_UNORM;
    uv_desc.SampleDesc.Count = 1;
    uv_desc.Usage = D3D11_USAGE_DYNAMIC;
    uv_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    uv_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = renderer->d3d_ctx->device->lpVtbl->CreateTexture2D(
        renderer->d3d_ctx->device, &uv_desc, NULL, &renderer->uv_tex.texture);
    if (FAILED(hr)) {
        log_error("Failed to create UV texture: 0x%08x", hr);
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC uv_srv_desc = {0};
    uv_srv_desc.Format = uv_desc.Format;
    uv_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    uv_srv_desc.Texture2D.MipLevels = 1;

    hr = renderer->d3d_ctx->device->lpVtbl->CreateShaderResourceView(
        renderer->d3d_ctx->device, (ID3D11Resource *)renderer->uv_tex.texture,
        &uv_srv_desc, &renderer->uv_tex.srv);
    if (FAILED(hr)) {
        log_error("Failed to create UV SRV: 0x%08x", hr);
        return false;
    }
    renderer->uv_tex.width = width / 2;
    renderer->uv_tex.height = height / 2;

    renderer->video_width = width;
    renderer->video_height = height;

    log_info("Created NV12 textures: %ux%u", width, height);
    return true;
}

static bool update_nv12_textures(video_renderer_t *renderer,
                                  const uint8_t *nv12_data,
                                  uint32_t width, uint32_t height) {
    /* Update Y plane */
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = renderer->d3d_ctx->device_ctx->lpVtbl->Map(
        renderer->d3d_ctx->device_ctx, (ID3D11Resource *)renderer->y_tex.texture,
        0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return false;

    const uint8_t *y_src = nv12_data;
    for (uint32_t row = 0; row < height; row++) {
        memcpy((uint8_t *)mapped.pData + row * mapped.RowPitch,
               y_src + row * width, width);
    }
    renderer->d3d_ctx->device_ctx->lpVtbl->Unmap(
        renderer->d3d_ctx->device_ctx, (ID3D11Resource *)renderer->y_tex.texture, 0);

    /* Update UV plane */
    hr = renderer->d3d_ctx->device_ctx->lpVtbl->Map(
        renderer->d3d_ctx->device_ctx, (ID3D11Resource *)renderer->uv_tex.texture,
        0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return false;

    const uint8_t *uv_src = nv12_data + width * height;
    uint32_t uv_width = width; /* NV12 UV row is same width (interleaved U+V) */
    uint32_t uv_height = height / 2;
    for (uint32_t row = 0; row < uv_height; row++) {
        memcpy((uint8_t *)mapped.pData + row * mapped.RowPitch,
               uv_src + row * uv_width, uv_width);
    }
    renderer->d3d_ctx->device_ctx->lpVtbl->Unmap(
        renderer->d3d_ctx->device_ctx, (ID3D11Resource *)renderer->uv_tex.texture, 0);

    return true;
}

bool video_renderer_render(video_renderer_t *renderer, const video_frame_t *frame) {
    if (!renderer->initialized || !frame || !frame->data) return false;

    /* Ensure textures are created for this frame size */
    if (!ensure_textures(renderer, frame->width, frame->height)) {
        return false;
    }

    /* Update NV12 textures */
    if (!update_nv12_textures(renderer, frame->data, frame->width, frame->height)) {
        return false;
    }

    /* Bind shader */
    shader_bind(&renderer->shader, renderer->d3d_ctx->device_ctx);

    /* Bind textures */
    renderer->d3d_ctx->device_ctx->lpVtbl->PSSetShaderResources(
        renderer->d3d_ctx->device_ctx, 0, 1, &renderer->y_tex.srv);
    renderer->d3d_ctx->device_ctx->lpVtbl->PSSetShaderResources(
        renderer->d3d_ctx->device_ctx, 1, 1, &renderer->uv_tex.srv);

    /* Bind sampler */
    renderer->d3d_ctx->device_ctx->lpVtbl->PSSetSamplers(
        renderer->d3d_ctx->device_ctx, 0, 1, &renderer->sampler);

    /* Bind constant buffer */
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

void video_renderer_destroy(video_renderer_t *renderer) {
    shader_destroy(&renderer->shader);
    texture_destroy(&renderer->y_tex);
    texture_destroy(&renderer->uv_tex);
    if (renderer->vb) renderer->vb->lpVtbl->Release(renderer->vb);
    if (renderer->ib) renderer->ib->lpVtbl->Release(renderer->ib);
    if (renderer->cb) renderer->cb->lpVtbl->Release(renderer->cb);
    if (renderer->sampler) renderer->sampler->lpVtbl->Release(renderer->sampler);
}
