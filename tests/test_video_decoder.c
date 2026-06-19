#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include "../src/decode/video_decoder.h"

void test_decoder_create_destroy(void) {
    video_decoder_t *dec = video_decoder_create();
    assert(dec != NULL);
    video_decoder_destroy(dec);

    /* Destroy NULL is safe */
    video_decoder_destroy(NULL);

    printf("test_decoder_create_destroy passed\n");
}

void test_decoder_init_h264(void) {
    video_decoder_t *dec = video_decoder_create();
    assert(dec != NULL);

    /* H.264 codec should init successfully */
    bool ok = video_decoder_init(dec, 0x68323634, 1920, 1080);
    assert(ok == true);

    video_decoder_destroy(dec);
    printf("test_decoder_init_h264 passed\n");
}

void test_decoder_init_unknown_codec(void) {
    video_decoder_t *dec = video_decoder_create();
    assert(dec != NULL);

    /* Unknown codec should fail */
    bool ok = video_decoder_init(dec, 0x00000000, 1920, 1080);
    assert(ok == false);

    video_decoder_destroy(dec);
    printf("test_decoder_init_unknown_codec passed\n");
}

void test_decoder_decode_no_init(void) {
    video_decoder_t *dec = video_decoder_create();
    assert(dec != NULL);

    /* Decode without init should return false */
    uint8_t dummy[] = {0x00, 0x00, 0x00, 0x01, 0x67};
    video_frame_t frame = {0};
    bool ok = video_decoder_decode(dec, dummy, sizeof(dummy), &frame);
    assert(ok == false);
    assert(frame.data == NULL);

    video_decoder_destroy(dec);
    printf("test_decoder_decode_no_init passed\n");
}

void test_decoder_decode_null(void) {
    video_frame_t frame = {0};
    bool ok = video_decoder_decode(NULL, NULL, 0, &frame);
    assert(ok == false);

    printf("test_decoder_decode_null passed\n");
}

void test_frame_free(void) {
    video_frame_t frame = {0};
    frame.data = malloc(100);
    video_frame_free(&frame);
    assert(frame.data == NULL);

    /* Free NULL is safe */
    video_frame_free(&frame);
    assert(frame.data == NULL);

    printf("test_frame_free passed\n");
}

int main(void) {
    test_decoder_create_destroy();
    test_decoder_init_h264();
    test_decoder_init_unknown_codec();
    test_decoder_decode_no_init();
    test_decoder_decode_null();
    test_frame_free();
    printf("All video_decoder tests passed!\n");
    return 0;
}
