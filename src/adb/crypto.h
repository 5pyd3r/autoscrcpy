#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>

int crypto_load_key(const char *path);
int crypto_sign_token(const uint8_t *token, int token_len, uint8_t *sig, int *sig_len);
int crypto_get_public_key(uint8_t *buf, int *len);
void crypto_free(void);

/* Get the internal pk context for TLS client certificate generation */
void *crypto_get_pk_context(void);
void *crypto_get_ctr_drbg(void);

#endif /* CRYPTO_H */
