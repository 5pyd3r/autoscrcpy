#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>
#include "../platform/platform.h"
#include "../platform/thread.h"
#include "control_msg.h"

#define CONTROLLER_QUEUE_SIZE 64

typedef struct {
    uint8_t data[CONTROL_MSG_MAX_SIZE];
    uint32_t size;
} controller_msg_t;

typedef struct {
    SOCKET_T control_socket;
    thread_t thread;
    mutex_t mutex;
    cond_t cond;
    bool stopped;

    controller_msg_t queue[CONTROLLER_QUEUE_SIZE];
    int queue_head;
    int queue_tail;
    int queue_count;
} controller_t;

bool controller_init(controller_t *ctrl, SOCKET_T control_socket);
bool controller_start(controller_t *ctrl);
void controller_stop(controller_t *ctrl);
void controller_join(controller_t *ctrl);
void controller_destroy(controller_t *ctrl);

/* Push a serialized control message (thread-safe, non-blocking) */
bool controller_push_msg(controller_t *ctrl, const uint8_t *data, uint32_t size);

#endif /* CONTROLLER_H */
