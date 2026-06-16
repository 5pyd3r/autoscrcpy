#include "controller.h"
#include "../platform/log.h"
#include <string.h>

static void *controller_thread_func(void *arg) {
    controller_t *ctrl = (controller_t *)arg;

    while (!ctrl->stopped) {
        mutex_lock(&ctrl->mutex);

        while (ctrl->queue_count == 0 && !ctrl->stopped) {
            cond_wait(&ctrl->cond, &ctrl->mutex);
        }

        if (ctrl->stopped) {
            mutex_unlock(&ctrl->mutex);
            break;
        }

        /* Dequeue one message */
        controller_msg_t msg = ctrl->queue[ctrl->queue_head];
        ctrl->queue_head = (ctrl->queue_head + 1) % CONTROLLER_QUEUE_SIZE;
        ctrl->queue_count--;

        mutex_unlock(&ctrl->mutex);

        /* Send the message */
        if (ctrl->control_socket != INVALID_SOCKFD) {
            /* Send size header (4 bytes big-endian) */
            uint8_t size_buf[4];
            size_buf[0] = (msg.size >> 24) & 0xFF;
            size_buf[1] = (msg.size >> 16) & 0xFF;
            size_buf[2] = (msg.size >> 8) & 0xFF;
            size_buf[3] = msg.size & 0xFF;

            size_t sent = 0;
            while (sent < 4) {
                int n = send(ctrl->control_socket, (char *)(size_buf + sent), 4 - (int)sent, MSG_NOSIGNAL);
                if (n <= 0) {
                    if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
                    log_error("Failed to send control message size");
                    break;
                }
                sent += n;
            }

            /* Send data */
            sent = 0;
            while (sent < msg.size) {
                int n = send(ctrl->control_socket, (char *)(msg.data + sent), (int)(msg.size - sent), MSG_NOSIGNAL);
                if (n <= 0) {
                    if (n < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
                    log_error("Failed to send control message data");
                    break;
                }
                sent += n;
            }
        }
    }

    return NULL;
}

bool controller_init(controller_t *ctrl, SOCKET_T control_socket) {
    ctrl->control_socket = control_socket;
    ctrl->stopped = false;
    ctrl->queue_head = 0;
    ctrl->queue_tail = 0;
    ctrl->queue_count = 0;

    mutex_init(&ctrl->mutex);
    cond_init(&ctrl->cond);

    return true;
}

bool controller_start(controller_t *ctrl) {
    return thread_create(&ctrl->thread, controller_thread_func, ctrl);
}

void controller_stop(controller_t *ctrl) {
    mutex_lock(&ctrl->mutex);
    ctrl->stopped = true;
    cond_signal(&ctrl->cond);
    mutex_unlock(&ctrl->mutex);
}

void controller_join(controller_t *ctrl) {
    thread_join(ctrl->thread);
}

void controller_destroy(controller_t *ctrl) {
    mutex_destroy(&ctrl->mutex);
    cond_destroy(&ctrl->cond);
}

bool controller_push_msg(controller_t *ctrl, const uint8_t *data, uint32_t size) {
    if (size > CONTROL_MSG_MAX_SIZE) {
        log_error("Control message too large: %u", size);
        return false;
    }

    mutex_lock(&ctrl->mutex);

    if (ctrl->queue_count >= CONTROLLER_QUEUE_SIZE) {
        log_warn("Control message queue full, dropping message");
        mutex_unlock(&ctrl->mutex);
        return false;
    }

    controller_msg_t *msg = &ctrl->queue[ctrl->queue_tail];
    memcpy(msg->data, data, size);
    msg->size = size;

    ctrl->queue_tail = (ctrl->queue_tail + 1) % CONTROLLER_QUEUE_SIZE;
    ctrl->queue_count++;

    cond_signal(&ctrl->cond);
    mutex_unlock(&ctrl->mutex);

    return true;
}
