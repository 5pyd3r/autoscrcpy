#include "input_transform.h"

void input_transform_coords(int32_t win_x, int32_t win_y,
                            int32_t *dev_x, int32_t *dev_y,
                            int32_t win_w, int32_t win_h,
                            uint32_t dev_w, uint32_t dev_h) {
    if (win_w <= 0 || win_h <= 0 || dev_w == 0 || dev_h == 0) {
        *dev_x = 0;
        *dev_y = 0;
        return;
    }

    float video_aspect = (float)dev_w / dev_h;
    float window_aspect = (float)win_w / win_h;

    int32_t render_x = 0, render_y = 0;
    int32_t render_w, render_h;

    if (window_aspect > video_aspect) {
        render_h = win_h;
        render_w = (int32_t)(win_h * video_aspect);
        render_x = (win_w - render_w) / 2;
    } else {
        render_w = win_w;
        render_h = (int32_t)(win_w / video_aspect);
        render_y = (win_h - render_h) / 2;
    }

    int32_t rel_x = win_x - render_x;
    int32_t rel_y = win_y - render_y;

    if (rel_x < 0) rel_x = 0;
    if (rel_y < 0) rel_y = 0;
    if (rel_x > render_w) rel_x = render_w;
    if (rel_y > render_h) rel_y = render_h;

    *dev_x = (int32_t)((float)rel_x / render_w * dev_w);
    *dev_y = (int32_t)((float)rel_y / render_h * dev_h);
}
