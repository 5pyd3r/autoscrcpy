#ifndef SESSION_H
#define SESSION_H

#include "adb.h"

/* Connect to adbd at host:port */
SOCKET_T session_connect(const char *host, int port);

/* Process incoming ADB message */
void session_handle_message(adb_connection_t *conn, const adb_message_t *msg,
                            const uint8_t *payload);

/* Start AUTH handshake */
int session_start_auth(adb_connection_t *conn);

/* Send CNXN message */
void session_send_cnxn(adb_connection_t *conn);

/* Open a channel */
adb_channel_t *session_open_channel(adb_connection_t *conn, const char *service);

/* Close a channel */
void session_close_channel(adb_connection_t *conn, adb_channel_t *chan);

/* Graceful connection teardown */
void session_disconnect(adb_connection_t *conn);

#endif /* SESSION_H */
