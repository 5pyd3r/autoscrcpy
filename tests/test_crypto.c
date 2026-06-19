#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../src/adb/crypto.h"

void test_load_key(void) {
    int ret = crypto_load_key("C:/Users/Spyder/.android/adbkey");
    assert(ret == 0);
    crypto_free();
    printf("test_load_key passed\n");
}

void test_load_bad_path(void) {
    int ret = crypto_load_key("/nonexistent/key");
    assert(ret == -1);
    printf("test_load_bad_path passed\n");
}

void test_sign_token(void) {
    crypto_load_key("C:/Users/Spyder/.android/adbkey");

    uint8_t token[20];
    memset(token, 0xAB, sizeof(token));
    uint8_t sig[512];
    int sig_len = 0;

    int ret = crypto_sign_token(token, sizeof(token), sig, &sig_len);
    assert(ret == 0);
    assert(sig_len > 0);

    crypto_free();
    printf("test_sign_token passed\n");
}

void test_sign_no_init(void) {
    crypto_free(); /* ensure not initialized */
    uint8_t token[20] = {0};
    uint8_t sig[512];
    int sig_len = 0;

    int ret = crypto_sign_token(token, sizeof(token), sig, &sig_len);
    assert(ret == -1);
    printf("test_sign_no_init passed\n");
}

void test_public_key(void) {
    crypto_load_key("C:/Users/Spyder/.android/adbkey");

    uint8_t buf[260];
    int len = 0;
    int ret = crypto_get_public_key(buf, &len);
    assert(ret == 0);
    assert(len == 260);
    /* Modulus should not be all zeros */
    assert(buf[0] != 0 || buf[1] != 0 || buf[2] != 0);

    crypto_free();
    printf("test_public_key passed\n");
}

void test_public_no_init(void) {
    crypto_free();
    uint8_t buf[260];
    int len = 0;
    int ret = crypto_get_public_key(buf, &len);
    assert(ret == -1);
    printf("test_public_no_init passed\n");
}

void test_free_safe(void) {
    crypto_free();
    crypto_free(); /* double free should not crash */
    printf("test_free_safe passed\n");
}

int main(void) {
    test_load_key();
    test_load_bad_path();
    test_sign_token();
    test_sign_no_init();
    test_public_key();
    test_public_no_init();
    test_free_safe();
    printf("All crypto tests passed!\n");
    return 0;
}
