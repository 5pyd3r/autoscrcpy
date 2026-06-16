#include "shader.h"
#include "../platform/log.h"

bool shader_init(shader_t *shader, ID3D11Device *device,
                 const void *vs_data, size_t vs_size,
                 const void *ps_data, size_t ps_size) {
    HRESULT hr;

    // Create vertex shader
    hr = device->lpVtbl->CreateVertexShader(device, vs_data, vs_size, NULL, &shader->vs);
    if (FAILED(hr)) {
        log_error("Failed to create vertex shader: 0x%08x", hr);
        return false;
    }

    // Create pixel shader
    hr = device->lpVtbl->CreatePixelShader(device, ps_data, ps_size, NULL, &shader->ps);
    if (FAILED(hr)) {
        log_error("Failed to create pixel shader: 0x%08x", hr);
        return false;
    }

    // Create input layout
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    hr = device->lpVtbl->CreateInputLayout(device, layout, 2, vs_data, vs_size, &shader->layout);
    if (FAILED(hr)) {
        log_error("Failed to create input layout: 0x%08x", hr);
        return false;
    }

    return true;
}

void shader_bind(shader_t *shader, ID3D11DeviceContext *ctx) {
    ctx->lpVtbl->VSSetShader(ctx, shader->vs, NULL, 0);
    ctx->lpVtbl->PSSetShader(ctx, shader->ps, NULL, 0);
    ctx->lpVtbl->IASetInputLayout(ctx, shader->layout);
}

void shader_destroy(shader_t *shader) {
    if (shader->vs) shader->vs->lpVtbl->Release(shader->vs);
    if (shader->ps) shader->ps->lpVtbl->Release(shader->ps);
    if (shader->layout) shader->layout->lpVtbl->Release(shader->layout);
}
