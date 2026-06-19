#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include "../src/input/input_transform.h"

void test_identity_transform(void) {
    int32_t dx, dy;
    input_transform_coords(400, 300, &dx, &dy, 800, 600, 800, 600);
    assert(dx == 400);
    assert(dy == 300);
    printf("test_identity_transform passed\n");
}

void test_letterbox(void) {
    /* Window 1600x600, Device 800x600 (window wider than video) */
    /* Video aspect 4:3, window aspect 8:3 → pillarbox, not letterbox */
    /* Actually: device 800x600 = 4:3, window 1600x600 = 8:3 */
    /* window_aspect (2.67) > video_aspect (1.33) → pillarbox */
    int32_t dx, dy;

    /* Center of window should map to center of device */
    input_transform_coords(800, 300, &dx, &dy, 1600, 600, 800, 600);
    assert(dx == 400);
    assert(dy == 300);

    printf("test_letterbox passed\n");
}

void test_pillarbox(void) {
    /* Window 600x800, Device 800x600 (window taller than video) */
    /* device aspect 4:3 = 1.33, window aspect 3:4 = 0.75 */
    /* window_aspect (0.75) < video_aspect (1.33) → pillarbox */
    int32_t dx, dy;

    /* Center of window should map to center of device */
    input_transform_coords(300, 400, &dx, &dy, 600, 800, 800, 600);
    assert(dx == 400);
    assert(dy == 300);

    printf("test_pillarbox passed\n");
}

void test_clipping(void) {
    int32_t dx, dy;

    /* Click at origin */
    input_transform_coords(0, 0, &dx, &dy, 800, 600, 800, 600);
    assert(dx == 0);
    assert(dy == 0);

    /* Click at far corner */
    input_transform_coords(800, 600, &dx, &dy, 800, 600, 800, 600);
    assert(dx == 800);
    assert(dy == 600);

    printf("test_clipping passed\n");
}

void test_zero_window(void) {
    int32_t dx, dy;
    input_transform_coords(100, 100, &dx, &dy, 0, 0, 800, 600);
    assert(dx == 0);
    assert(dy == 0);
    printf("test_zero_window passed\n");
}

void test_zero_device(void) {
    int32_t dx, dy;
    input_transform_coords(100, 100, &dx, &dy, 800, 600, 0, 0);
    assert(dx == 0);
    assert(dy == 0);
    printf("test_zero_device passed\n");
}

int main(void) {
    test_identity_transform();
    test_letterbox();
    test_pillarbox();
    test_clipping();
    test_zero_window();
    test_zero_device();
    printf("All input_transform tests passed!\n");
    return 0;
}
