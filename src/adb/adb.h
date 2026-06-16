#ifndef ADB_H
#define ADB_H

#include <stdint.h>
#include <stdbool.h>
#include "../platform/platform.h"

/* ADB protocol version */
#define ADB_VERSION_MIN         0x01000000
#define ADB_VERSION_SKIP_CHECKSUM 0x01000001
#define ADB_VERSION             0x01000001
#define ADB_MAX_PAYLOAD (1024 * 1024)

/* ADB command identifiers */
#define ADB_CNXN 0x4e584e43
#define ADB_AUTH 0x48545541
#define ADB_OPEN 0x4e45504f
#define ADB_OKAY 0x59414b4f
#define ADB_WRTE 0x45545257
#define ADB_CLSE 0x45534c43
#define ADB_STLS 0x534c5453

/* AUTH sub-types */
#define ADB_AUTH_TYPE_TOKEN  1
#define ADB_AUTH_TYPE_RSAKEY 2
#define ADB_AUTH_TYPE_RSAPUB 3

/* ADB message header (24 bytes, wire format) */
#pragma pack(push, 1)
typedef struct {
    uint32_t command;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t data_length;
    uint32_t data_check;
    uint32_t magic;
} adb_message_t;
#pragma pack(pop)

#define ADB_MSG_HEADER_SIZE 24

/* Connection state */
typedef enum {
    ADB_STATE_DISCONNECTED,
    ADB_STATE_CONNECTING,
    ADB_STATE_AUTH_SENT,
    ADB_STATE_AUTH_RSAPUB_SENT,
    ADB_STATE_CONNECTED,
    ADB_STATE_TLS_NEGOTIATING,
} adb_conn_state_t;

/* Channel state */
typedef enum {
    CHAN_OPENING,
    CHAN_OPEN,
    CHAN_CLOSING,
    CHAN_CLOSED,
} adb_chan_state_t;

#define MAX_CHANNELS 64
#define SERVICE_NAME_MAX 256
#define BANNER_MAX 512

/* Channel (logical stream within a connection) */
typedef struct {
    uint32_t        local_id;
    uint32_t        remote_id;
    char            service[SERVICE_NAME_MAX];
    adb_chan_state_t state;
    SOCKET_T        local_fd;
} adb_channel_t;

/* Device connection */
typedef struct adb_connection {
    SOCKET_T         fd;
    adb_conn_state_t state;
    adb_channel_t    channels[MAX_CHANNELS];
    int              channel_count;
    uint32_t         next_local_id;
    char             banner[BANNER_MAX];
    int              protocol_version;
    size_t           max_payload;
    int              use_tls;
    void            *tls_ctx;
    int              cnxn_sent;
    int              stls_sent;
    void (*on_connected)(struct adb_connection *conn);
    void (*on_shell_output)(const uint8_t *data, uint32_t len, void *arg);
    void *on_shell_output_arg;
    struct adb_connection *next;
} adb_connection_t;

/* Initialize ADB subsystem */
bool adb_init(void);

/* Cleanup ADB subsystem */
void adb_destroy(void);

/* Connect to device */
adb_connection_t *adb_connect(const char *host, uint16_t port);

/* Disconnect */
void adb_disconnect(adb_connection_t *conn);

/* Execute shell command */
bool adb_shell(adb_connection_t *conn, const char *command);

/* Push file to device */
bool adb_push(adb_connection_t *conn, const char *local, const char *remote);

/* Forward port */
bool adb_forward(adb_connection_t *conn, uint16_t local_port, const char *remote_spec);

#endif /* ADB_H */
