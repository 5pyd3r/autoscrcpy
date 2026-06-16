#ifndef TLS_H
#define TLS_H

#include "adb.h"

int tls_init(void);
void *tls_handshake(SOCKET_T fd);
int tls_send(void *ssl_ctx, const void *buf, int len);
int tls_recv(void *ssl_ctx, void *buf, int len);
void tls_free(void *ssl_ctx);
void tls_cleanup(void);

#endif /* TLS_H */
