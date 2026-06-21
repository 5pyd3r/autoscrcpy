#ifndef SCRIPT_MESSAGE_QUEUE_H
#define SCRIPT_MESSAGE_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <windows.h>

#define SCRIPT_MSG_QUEUE_CAPACITY 256
#define SCRIPT_MSG_MAX_DATA_SIZE  256

typedef enum {
    /* Scheme -> Main: control commands */
    MSG_INJECT_KEYCODE,
    MSG_INJECT_TEXT,
    MSG_INJECT_TOUCH,
    MSG_INJECT_SCROLL,
    MSG_SET_CLIPBOARD,
    MSG_EXPAND_NOTIFICATION,
    MSG_COLLAPSE_PANELS,
    MSG_SET_DISPLAY_POWER,
    MSG_ROTATE_DEVICE,
    MSG_START_APP,
    /* Scheme -> Main: synchronous queries */
    MSG_QUERY_DEVICE_INFO,      /* request: device width/height/name/connected */
    MSG_QUERY_FRAME_CAPTURE,    /* request: current frame NV12 data */
    MSG_QUERY_CLIPBOARD,        /* request: device clipboard text */
    MSG_QUERY_VIDEO_SIZE,       /* request: video dimensions */
    MSG_QUERY_WINDOW_SIZE,      /* request: window dimensions */
    /* Main -> Scheme: events */
    MSG_EVENT_KEY,
    MSG_EVENT_MOUSE,
    MSG_EVENT_FRAME,
    MSG_EVENT_CONNECTED,
    MSG_EVENT_DISCONNECTED,
    MSG_EVENT_ERROR,
    /* Main -> Scheme: query responses */
    MSG_RESPONSE_DEVICE_INFO,   /* response: width(4)+height(4)+name(64)+connected(4) */
    MSG_RESPONSE_FRAME_CAPTURE, /* response: width(4)+height(4)+data_size(4)+data[..] */
    MSG_RESPONSE_CLIPBOARD,     /* response: null-terminated string */
    MSG_RESPONSE_VIDEO_SIZE,    /* response: width(4)+height(4) */
    MSG_RESPONSE_WINDOW_SIZE,   /* response: width(4)+height(4) */
    /* Control */
    MSG_SHUTDOWN,
} script_msg_type_t;

typedef struct {
    script_msg_type_t type;
    uint32_t data_size;
    uint8_t data[SCRIPT_MSG_MAX_DATA_SIZE];
} script_msg_t;

/* Query message sent from Scheme thread to main thread.
 * Includes a response semaphore for synchronous wait. */
typedef struct {
    script_msg_type_t type;
    HANDLE response_sem;        /* signaled when response is ready */
    script_msg_t *response;     /* pointer to caller-owned response buffer */
} script_query_t;

typedef struct {
    script_msg_t items[SCRIPT_MSG_QUEUE_CAPACITY];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    CRITICAL_SECTION cs;
    HANDLE semaphore;
    bool initialized;
} script_msg_queue_t;

bool script_msg_queue_init(script_msg_queue_t *q);
void script_msg_queue_destroy(script_msg_queue_t *q);
bool script_msg_queue_send(script_msg_queue_t *q, const script_msg_t *msg);
bool script_msg_queue_recv(script_msg_queue_t *q, script_msg_t *msg, uint32_t timeout_ms);
bool script_msg_queue_try_recv(script_msg_queue_t *q, script_msg_t *msg);
void script_msg_queue_drain(script_msg_queue_t *q);

#endif /* SCRIPT_MESSAGE_QUEUE_H */
