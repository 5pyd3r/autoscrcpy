#include "texture.h"
#include "../platform/log.h"

#include <string.h>

bool texture_init(texture_t *tex, ID3D11Device *device, uint32_t width, uint32_t height) {
    tex->width = width;
    tex->height = height;

    D3D11_TEXTURE2D_DESC desc = {0};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = device->lpVtbl->CreateTexture2D(device, &desc, NULL, &tex->texture);
    if (FAILED(hr)) {
        log_error("Failed to create texture: 0x%08x", hr);
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {0};
    srv_desc.Format = desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;

    hr = device->lpVtbl->CreateShaderResourceView(device, tex->texture, &srv_desc, &tex->srv);
    if (FAILED(hr)) {
        log_error("Failed to create shader resource view: 0x%08x", hr);
        return false;
    }

    return true;
}

bool texture_update(texture_t *tex, ID3D11DeviceContext *ctx, const uint8_t *data, uint32_t pitch) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = ctx->lpVtbl->Map(ctx, (ID3D11Resource *)tex->texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        log_error("Failed to map texture: 0x%08x", hr);
        return false;
    }

    for (uint32_t y = 0; y < tex->height; y++) {
        memcpy((uint8_t *)mapped.pData + y * mapped.RowPitch,
               data + y * pitch,
               tex->width * 4);
    }

    ctx->lpVtbl->Unmap(ctx, (ID3D11Resource *)tex->texture, 0);
    return true;
}

void texture_bind(texture_t *tex, ID3D11DeviceContext *ctx, int slot) {
    ctx->lpVtbl->PSSetShaderResources(ctx, slot, 1, &tex->srv);
}

void texture_destroy(texture_t *tex) {
    if (tex->srv) tex->srv->lpVtbl->Release(tex->srv);
    if (tex->texture) tex->texture->lpVtbl->Release(tex->texture);
}
