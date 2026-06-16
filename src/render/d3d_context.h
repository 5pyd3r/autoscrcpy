#ifndef D3D_CONTEXT_H
#define D3D_CONTEXT_H

#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdbool.h>

typedef struct {
    ID3D11Device *device;
    ID3D11DeviceContext *device_ctx;
    IDXGISwapChain *swap_chain;
    ID3D11RenderTargetView *rtv;
    int width;
    int height;
} d3d_context_t;

bool d3d_context_init(d3d_context_t *ctx, HWND hwnd, int width, int height);
void d3d_context_resize(d3d_context_t *ctx, int width, int height);
void d3d_context_begin_frame(d3d_context_t *ctx);
void d3d_context_end_frame(d3d_context_t *ctx);
void d3d_context_destroy(d3d_context_t *ctx);

#endif /* D3D_CONTEXT_H */
