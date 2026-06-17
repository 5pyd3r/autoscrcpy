#include "tls.h"
#include "crypto.h"
#include "../platform/log.h"
#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/md.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static mbedtls_ssl_config conf;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context ctr_drbg;
static mbedtls_x509_crt client_cert;
static mbedtls_pk_context client_key;
static int tls_initialized = 0;

/* Generate a self-signed X509 certificate from the RSA key */
static int generate_self_signed_cert(void) {
    mbedtls_pk_context *pk = crypto_get_pk_context();
    mbedtls_ctr_drbg_context *drbg = crypto_get_ctr_drbg();
    if (!pk || !drbg) {
        log_error("Crypto not initialized");
        return -1;
    }

    mbedtls_x509write_cert crt;
    mbedtls_x509write_crt_init(&crt);

    /* Set version to v3 */
    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);

    /* Set serial number */
    unsigned char serial[] = {0x01};
    mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial));

    /* Set validity period (2020-2030) */
    mbedtls_x509write_crt_set_validity(&crt, "20200101000000", "20300101000000");

    /* Set subject and issuer (self-signed) */
    const char *subject = "CN=autoscrcpy,O=Android Debug Bridge";
    mbedtls_x509write_crt_set_subject_name(&crt, subject);
    mbedtls_x509write_crt_set_issuer_name(&crt, subject);

    /* Set the subject key */
    mbedtls_x509write_crt_set_subject_key(&crt, pk);

    /* Set the issuer key (same as subject for self-signed) */
    mbedtls_x509write_crt_set_issuer_key(&crt, pk);

    /* Set MD algorithm */
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);

    /* Write certificate to DER format */
    unsigned char cert_buf[4096];
    int ret = mbedtls_x509write_crt_der(&crt, cert_buf, sizeof(cert_buf),
                                         mbedtls_ctr_drbg_random, drbg);
    if (ret < 0) {
        log_error("Failed to write certificate: -0x%04x", -ret);
        mbedtls_x509write_crt_free(&crt);
        return -1;
    }

    /* Parse the DER certificate */
    mbedtls_x509_crt_init(&client_cert);
    ret = mbedtls_x509_crt_parse_der(&client_cert, cert_buf + sizeof(cert_buf) - ret, ret);
    if (ret != 0) {
        log_error("Failed to parse certificate: -0x%04x", -ret);
        mbedtls_x509write_crt_free(&crt);
        return -1;
    }

    /* Copy the private key */
    mbedtls_pk_init(&client_key);
    ret = mbedtls_pk_setup(&client_key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    if (ret != 0) {
        log_error("Failed to setup key: -0x%04x", -ret);
        mbedtls_x509_crt_free(&client_cert);
        mbedtls_x509write_crt_free(&crt);
        return -1;
    }

    /* Copy RSA key from crypto context */
    mbedtls_rsa_context *rsa = mbedtls_pk_rsa(*pk);
    mbedtls_rsa_context *client_rsa = mbedtls_pk_rsa(client_key);
    ret = mbedtls_rsa_copy(client_rsa, rsa);
    if (ret != 0) {
        log_error("Failed to copy RSA key: -0x%04x", -ret);
        mbedtls_pk_free(&client_key);
        mbedtls_x509_crt_free(&client_cert);
        mbedtls_x509write_crt_free(&crt);
        return -1;
    }

    mbedtls_x509write_crt_free(&crt);
    log_info("Generated self-signed client certificate");
    return 0;
}

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

    /* ADB uses anonymous authentication for server */
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);

    tls_initialized = 1;
    return 0;
}

int tls_configure_client_cert(void) {
    if (!tls_initialized) {
        log_error("TLS not initialized");
        return -1;
    }

    /* Generate client certificate for device authentication */
    if (generate_self_signed_cert() == 0) {
        /* Configure client certificate */
        mbedtls_ssl_conf_own_cert(&conf, &client_cert, &client_key);
        log_info("Client certificate configured for TLS");
        return 0;
    } else {
        log_warn("Failed to generate client certificate");
        return -1;
    }
}

/* TLS context: wraps mbedtls ssl_context + persistent fd storage */
typedef struct {
    mbedtls_ssl_context ssl;
    SOCKET_T fd;       /* must persist — BIO callback uses &fd */
} tls_ctx_t;

void *tls_handshake(SOCKET_T fd) {
    if (!tls_initialized) {
        log_error("TLS not initialized");
        return NULL;
    }

    tls_ctx_t *ctx = calloc(1, sizeof(tls_ctx_t));
    if (!ctx) return NULL;

    ctx->fd = fd;
    mbedtls_ssl_init(&ctx->ssl);

    int ret = mbedtls_ssl_setup(&ctx->ssl, &conf);
    if (ret != 0) {
        log_error("Failed to setup TLS: -0x%04x", -ret);
        mbedtls_ssl_free(&ctx->ssl);
        free(ctx);
        return NULL;
    }

    /* BIO uses &ctx->fd which persists for the lifetime of the connection */
    mbedtls_ssl_set_bio(&ctx->ssl, &ctx->fd, mbedtls_net_send, mbedtls_net_recv, NULL);

    while ((ret = mbedtls_ssl_handshake(&ctx->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            log_error("TLS handshake failed: -0x%04x", -ret);
            mbedtls_ssl_free(&ctx->ssl);
            free(ctx);
            return NULL;
        }
    }

    log_info("TLS handshake complete, version=%s, cipher=%s",
             mbedtls_ssl_get_version(&ctx->ssl),
             mbedtls_ssl_get_ciphersuite(&ctx->ssl));

    return ctx;
}

int tls_send(void *ssl_ctx, const void *buf, int len) {
    tls_ctx_t *ctx = ssl_ctx;
    int ret = mbedtls_ssl_write(&ctx->ssl, buf, len);
    if (ret == MBEDTLS_ERR_SSL_WANT_WRITE ||
        ret == MBEDTLS_ERR_SSL_WANT_READ)
        return 0;
    return ret;
}

int tls_recv(void *ssl_ctx, void *buf, int len) {
    tls_ctx_t *ctx = ssl_ctx;
    int ret = mbedtls_ssl_read(&ctx->ssl, buf, len);
    /* Non-fatal conditions — return 0 so caller can retry */
    if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
        ret == MBEDTLS_ERR_SSL_WANT_WRITE ||
        ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ||
        ret == MBEDTLS_ERR_NET_RECV_FAILED ||
        ret == MBEDTLS_ERR_SSL_UNEXPECTED_MESSAGE ||
        ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
        return 0;
    return ret;
}

void tls_free(void *ssl_ctx) {
    if (ssl_ctx) {
        tls_ctx_t *ctx = ssl_ctx;
        mbedtls_ssl_free(&ctx->ssl);
        free(ctx);
    }
}

void tls_cleanup(void) {
    if (tls_initialized) {
        mbedtls_ssl_config_free(&conf);
        mbedtls_entropy_free(&entropy);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_x509_crt_free(&client_cert);
        mbedtls_pk_free(&client_key);
        tls_initialized = 0;
    }
}
