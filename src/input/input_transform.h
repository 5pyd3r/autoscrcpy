#ifndef INPUT_TRANSFORM_H
#define INPUT_TRANSFORM_H

#include <stdint.h>

void input_transform_coords(int32_t win_x, int32_t win_y,
                            int32_t *dev_x, int32_t *dev_y,
                            int32_t win_w, int32_t win_h,
                            uint32_t dev_w, uint32_t dev_h);

#endif /* INPUT_TRANSFORM_H */
