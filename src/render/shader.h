#ifndef SHADER_H
#define SHADER_H

#include <d3d11.h>
#include <stdbool.h>

typedef struct {
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11InputLayout *layout;
} shader_t;

bool shader_init(shader_t *shader, ID3D11Device *device,
                 const void *vs_data, size_t vs_size,
                 const void *ps_data, size_t ps_size);
void shader_bind(shader_t *shader, ID3D11DeviceContext *ctx);
void shader_destroy(shader_t *shader);

#endif /* SHADER_H */
