#include "d3d_context.h"
#include "../platform/log.h"
#include <stdio.h>

bool d3d_context_init(d3d_context_t *ctx, HWND hwnd, int width, int height) {
    ctx->width = width;
    ctx->height = height;

    DXGI_SWAP_CHAIN_DESC desc = {0};
    desc.BufferDesc.Width = width;
    desc.BufferDesc.Height = height;
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.OutputWindow = hwnd;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags,
        NULL, 0, D3D11_SDK_VERSION, &desc,
        &ctx->swap_chain, &ctx->device, &level, &ctx->device_ctx);

    if (FAILED(hr)) {
        log_error("Failed to create D3D11 device: 0x%08x", hr);
        return false;
    }

    /* Enable multi-threaded protection so the video thread can render
     * while the main thread processes Win32 messages */
    {
        ID3D10Multithread *mt = NULL;
        hr = ctx->device_ctx->lpVtbl->QueryInterface(
            ctx->device_ctx, &IID_ID3D10Multithread, (void **)&mt);
        if (SUCCEEDED(hr) && mt) {
            mt->lpVtbl->SetMultithreadProtected(mt, TRUE);
            mt->lpVtbl->Release(mt);
            log_info("D3D11 multi-threaded protection enabled");
        }
    }

    // Create render target view
    ID3D11Texture2D *back_buffer;
    hr = ctx->swap_chain->lpVtbl->GetBuffer(ctx->swap_chain, 0, &IID_ID3D11Texture2D, (void **)&back_buffer);
    if (FAILED(hr)) {
        log_error("Failed to get back buffer: 0x%08x", hr);
        d3d_context_destroy(ctx);
        return false;
    }

    hr = ctx->device->lpVtbl->CreateRenderTargetView(ctx->device, back_buffer, NULL, &ctx->rtv);
    back_buffer->lpVtbl->Release(back_buffer);

    if (FAILED(hr)) {
        log_error("Failed to create render target view: 0x%08x", hr);
        d3d_context_destroy(ctx);
        return false;
    }

    /* Bind render target and set viewport */
    ctx->device_ctx->lpVtbl->OMSetRenderTargets(ctx->device_ctx, 1, &ctx->rtv, NULL);

    D3D11_VIEWPORT vp = {0, 0, (FLOAT)width, (FLOAT)height, 0, 1};
    ctx->device_ctx->lpVtbl->RSSetViewports(ctx->device_ctx, 1, &vp);

    return true;
}

void d3d_context_resize(d3d_context_t *ctx, int width, int height) {
    if (width == 0 || height == 0) return;

    ctx->width = width;
    ctx->height = height;

    ctx->device_ctx->lpVtbl->OMSetRenderTargets(ctx->device_ctx, 0, NULL, NULL);
    if (ctx->rtv) { ctx->rtv->lpVtbl->Release(ctx->rtv); ctx->rtv = NULL; }

    HRESULT hr = ctx->swap_chain->lpVtbl->ResizeBuffers(ctx->swap_chain, 0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        log_error("Failed to resize swap chain: 0x%08x", hr);
        return;
    }

    ID3D11Texture2D *back_buffer;
    hr = ctx->swap_chain->lpVtbl->GetBuffer(ctx->swap_chain, 0, &IID_ID3D11Texture2D, (void **)&back_buffer);
    if (FAILED(hr)) {
        log_error("Failed to get back buffer: 0x%08x", hr);
        return;
    }

    hr = ctx->device->lpVtbl->CreateRenderTargetView(ctx->device, back_buffer, NULL, &ctx->rtv);
    back_buffer->lpVtbl->Release(back_buffer);

    if (FAILED(hr)) {
        log_error("Failed to create render target view: 0x%08x", hr);
        return;
    }

    ctx->device_ctx->lpVtbl->OMSetRenderTargets(ctx->device_ctx, 1, &ctx->rtv, NULL);

    D3D11_VIEWPORT vp = {0, 0, (FLOAT)width, (FLOAT)height, 0, 1};
    ctx->device_ctx->lpVtbl->RSSetViewports(ctx->device_ctx, 1, &vp);
}

void d3d_context_begin_frame(d3d_context_t *ctx) {
    if (!ctx->rtv) return; /* Resize in progress or failed */

    /* Rebind render target and viewport every frame (may have changed after resize) */
    ctx->device_ctx->lpVtbl->OMSetRenderTargets(ctx->device_ctx, 1, &ctx->rtv, NULL);

    D3D11_VIEWPORT vp = {0, 0, (FLOAT)ctx->width, (FLOAT)ctx->height, 0, 1};
    ctx->device_ctx->lpVtbl->RSSetViewports(ctx->device_ctx, 1, &vp);

    float clear_color[4] = {0, 0, 0, 1};
    ctx->device_ctx->lpVtbl->ClearRenderTargetView(ctx->device_ctx, ctx->rtv, clear_color);
}

void d3d_context_end_frame(d3d_context_t *ctx) {
    /* Present with SyncInterval=0 for no VSync (lowest latency) */
    HRESULT hr = ctx->swap_chain->lpVtbl->Present(ctx->swap_chain, 0, 0);
    if (FAILED(hr)) {
        static int present_errors = 0;
        present_errors++;
        if (present_errors <= 3)
            fprintf(stderr, "D3D: Present failed: 0x%08x\n", hr);
    }
}

void d3d_context_destroy(d3d_context_t *ctx) {
    if (ctx->rtv) ctx->rtv->lpVtbl->Release(ctx->rtv);
    if (ctx->swap_chain) ctx->swap_chain->lpVtbl->Release(ctx->swap_chain);
    if (ctx->device_ctx) ctx->device_ctx->lpVtbl->Release(ctx->device_ctx);
    if (ctx->device) ctx->device->lpVtbl->Release(ctx->device);
}
