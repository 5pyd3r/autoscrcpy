#include "crypto.h"
#include "../platform/log.h"
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/bignum.h>
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

    /* ADB format: 256-byte big-endian RSA modulus + 4-byte LE exponent = 260 bytes */
    mbedtls_rsa_context *rsa = mbedtls_pk_rsa(pk);
    if (!rsa) {
        log_error("Key is not RSA");
        return -1;
    }

    mbedtls_mpi N, E;
    mbedtls_mpi_init(&N);
    mbedtls_mpi_init(&E);

    int ret = mbedtls_rsa_export(rsa, &N, NULL, NULL, NULL, &E);
    if (ret != 0) {
        log_error("Failed to export RSA key: -0x%04x", -ret);
        mbedtls_mpi_free(&N);
        mbedtls_mpi_free(&E);
        return -1;
    }

    /* Write modulus (256 bytes, big-endian, zero-padded) */
    int mod_len = (int)mbedtls_mpi_size(&N);
    if (mod_len > 256) mod_len = 256;
    memset(buf, 0, 256);
    mbedtls_mpi_write_binary(&N, buf + (256 - mod_len), mod_len);

    /* Write exponent (little-endian uint32) */
    unsigned char exp_buf[8];
    mbedtls_mpi_write_binary_le(&E, exp_buf, sizeof(exp_buf));
    memcpy(buf + 256, exp_buf, 4);

    *len = 260;

    mbedtls_mpi_free(&N);
    mbedtls_mpi_free(&E);
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

void *crypto_get_pk_context(void) {
    if (!crypto_initialized) return NULL;
    return &pk;
}

void *crypto_get_ctr_drbg(void) {
    if (!crypto_initialized) return NULL;
    return &ctr_drbg;
}
