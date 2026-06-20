#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/pipeline/pipeline.h"
#include "../src/platform/log.h"

/* Test basic init */
static void test_init(void) {
    shared_frame_t sf;
    shared_frame_init(&sf);
    assert(sf.current == NULL);
    assert(sf.ready == 0);
    printf("test_init: PASS\n");
}

/* Test submit and acquire */
static void test_submit_acquire(void) {
    shared_frame_t sf;
    shared_frame_init(&sf);

    /* Create frame */
    frame_data_t *frame = calloc(1, sizeof(frame_data_t));
    frame->data = malloc(100);
    frame->width = 320;
    frame->height = 240;
    memset(frame->data, 0xAB, 100);

    /* Submit */
    shared_frame_submit(&sf, frame);
    assert(sf.ready == 1);

    /* Acquire */
    frame_data_t *acquired = shared_frame_acquire(&sf);
    assert(acquired != NULL);
    assert(acquired->width == 320);
    assert(acquired->height == 240);
    assert(acquired->data != NULL);

    frame_data_free(acquired);
    assert(sf.ready == 0);
    assert(sf.current == NULL);
    printf("test_submit_acquire: PASS\n");
}

/* Test overwrite (latest-frame-wins) */
static void test_overwrite(void) {
    shared_frame_t sf;
    shared_frame_init(&sf);

    /* Submit first frame */
    frame_data_t *frame1 = calloc(1, sizeof(frame_data_t));
    frame1->data = malloc(100);
    frame1->width = 320;
    frame1->height = 240;
    shared_frame_submit(&sf, frame1);

    /* Submit second frame (overwrites first) */
    frame_data_t *frame2 = calloc(1, sizeof(frame_data_t));
    frame2->data = malloc(200);
    frame2->width = 640;
    frame2->height = 480;
    shared_frame_submit(&sf, frame2);

    /* Acquire should return second frame */
    frame_data_t *acquired = shared_frame_acquire(&sf);
    assert(acquired != NULL);
    assert(acquired->width == 640);
    assert(acquired->height == 480);

    frame_data_free(acquired);
    printf("test_overwrite: PASS\n");
}

/* Test acquire when no frame available */
static void test_acquire_empty(void) {
    shared_frame_t sf;
    shared_frame_init(&sf);

    frame_data_t *acquired = shared_frame_acquire(&sf);
    assert(acquired == NULL);
    printf("test_acquire_empty: PASS\n");
}

int main(void) {
    log_init(LOG_LEVEL_ERROR);

    test_init();
    test_submit_acquire();
    test_overwrite();
    test_acquire_empty();

    printf("\nAll shared_frame tests passed!\n");
    return 0;
}
