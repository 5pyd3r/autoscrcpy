#ifndef TEXTURE_H
#define TEXTURE_H

#include <d3d11.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    ID3D11Texture2D *texture;
    ID3D11ShaderResourceView *srv;
    uint32_t width;
    uint32_t height;
} texture_t;

bool texture_init(texture_t *tex, ID3D11Device *device, uint32_t width, uint32_t height);
bool texture_update(texture_t *tex, ID3D11DeviceContext *ctx, const uint8_t *data, uint32_t pitch);
void texture_bind(texture_t *tex, ID3D11DeviceContext *ctx, int slot);
void texture_destroy(texture_t *tex);

#endif /* TEXTURE_H */
