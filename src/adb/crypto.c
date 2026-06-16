#include "crypto.h"
#include "../platform/log.h"
#include <mbedtls/pk.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <string.h>
#include <stdlib.h>

static mbedtls_pk_context pk;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context ctr_drbg;
static int crypto_initialized = 0;

int crypto_load_key(const char *path) {
    if (crypto_initialized) {
        crypto_free();
    }

    mbedtls_pk_init(&pk);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                     (const unsigned char *)"autoscrcpy", 10);
    if (ret != 0) {
        log_error("Failed to seed RNG: -0x%04x", -ret);
        return -1;
    }

    ret = mbedtls_pk_parse_keyfile(&pk, path, NULL, mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        log_error("Failed to load private key: -0x%04x", -ret);
        return -1;
    }

    crypto_initialized = 1;
    return 0;
}

int crypto_sign_token(const uint8_t *token, int token_len, uint8_t *sig, int *sig_len) {
    if (!crypto_initialized) {
        log_error("Crypto not initialized");
        return -1;
    }

    size_t olen;
    int ret = mbedtls_pk_sign(&pk, MBEDTLS_MD_SHA1, token, token_len,
                               sig, 256, &olen,
                               mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        log_error("Failed to sign token: -0x%04x", -ret);
        return -1;
    }

    *sig_len = (int)olen;
    return 0;
}

int crypto_get_public_key(uint8_t *buf, int *len) {
    if (!crypto_initialized) {
        log_error("Crypto not initialized");
        return -1;
    }

    /* Get public key in DER format */
    int ret = mbedtls_pk_write_pubkey_der(&pk, buf, 512);
    if (ret < 0) {
        log_error("Failed to get public key: -0x%04x", -ret);
        return -1;
    }

    *len = ret;
    return 0;
}

void crypto_free(void) {
    if (crypto_initialized) {
        mbedtls_pk_free(&pk);
        mbedtls_entropy_free(&entropy);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        crypto_initialized = 0;
    }
}
