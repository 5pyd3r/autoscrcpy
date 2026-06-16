#include "tls.h"
#include "../platform/log.h"
#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <string.h>
#include <stdlib.h>

static mbedtls_ssl_config conf;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context ctr_drbg;
static int tls_initialized = 0;

int tls_init(void) {
    if (tls_initialized) return 0;

    mbedtls_ssl_config_init(&conf);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                     (const unsigned char *)"autoscrcpy", 10);
    if (ret != 0) {
        log_error("Failed to seed TLS RNG: -0x%04x", -ret);
        return -1;
    }

    ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                       MBEDTLS_SSL_TRANSPORT_STREAM,
                                       MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        log_error("Failed to set TLS defaults: -0x%04x", -ret);
        return -1;
    }

    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    /* ADB uses anonymous authentication */
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);

    tls_initialized = 1;
    return 0;
}

void *tls_handshake(SOCKET_T fd) {
    if (!tls_initialized) {
        log_error("TLS not initialized");
        return NULL;
    }

    mbedtls_ssl_context *ssl = malloc(sizeof(mbedtls_ssl_context));
    if (!ssl) {
        log_error("Failed to allocate TLS context");
        return NULL;
    }

    mbedtls_ssl_init(ssl);

    int ret = mbedtls_ssl_setup(ssl, &conf);
    if (ret != 0) {
        log_error("Failed to setup TLS: -0x%04x", -ret);
        free(ssl);
        return NULL;
    }

    /* Set up BIO callbacks */
    mbedtls_ssl_set_bio(ssl, &fd, mbedtls_net_send, mbedtls_net_recv, NULL);

    /* Perform handshake */
    while ((ret = mbedtls_ssl_handshake(ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            log_error("TLS handshake failed: -0x%04x", -ret);
            mbedtls_ssl_free(ssl);
            free(ssl);
            return NULL;
        }
    }

    return ssl;
}

int tls_send(void *ssl_ctx, const void *buf, int len) {
    return mbedtls_ssl_write(ssl_ctx, buf, len);
}

int tls_recv(void *ssl_ctx, void *buf, int len) {
    return mbedtls_ssl_read(ssl_ctx, buf, len);
}

void tls_free(void *ssl_ctx) {
    if (ssl_ctx) {
        mbedtls_ssl_free(ssl_ctx);
        free(ssl_ctx);
    }
}

void tls_cleanup(void) {
    if (tls_initialized) {
        mbedtls_ssl_config_free(&conf);
        mbedtls_entropy_free(&entropy);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        tls_initialized = 0;
    }
}
