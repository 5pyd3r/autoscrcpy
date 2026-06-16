#include "packet_queue.h"
#include <stdlib.h>
#include <string.h>

bool packet_queue_init(packet_queue_t *queue, int capacity) {
    queue->packets = calloc(capacity, sizeof(packet_t));
    if (!queue->packets) return false;

    queue->capacity = capacity;
    queue->count = 0;
    queue->head = 0;
    queue->tail = 0;
    queue->finished = false;

    mutex_init(&queue->mutex);
    cond_init(&queue->cond);

    return true;
}

void packet_queue_destroy(packet_queue_t *queue) {
    mutex_lock(&queue->mutex);
    for (int i = 0; i < queue->count; i++) {
        int idx = (queue->head + i) % queue->capacity;
        free(queue->packets[idx].data);
    }
    free(queue->packets);
    mutex_unlock(&queue->mutex);

    mutex_destroy(&queue->mutex);
    cond_destroy(&queue->cond);
}

bool packet_queue_push(packet_queue_t *queue, const uint8_t *data, uint32_t size, int64_t pts) {
    mutex_lock(&queue->mutex);

    while (queue->count >= queue->capacity && !queue->finished) {
        cond_wait(&queue->cond, &queue->mutex);
    }

    if (queue->finished) {
        mutex_unlock(&queue->mutex);
        return false;
    }

    packet_t *pkt = &queue->packets[queue->tail];
    pkt->data = malloc(size);
    if (!pkt->data) {
        mutex_unlock(&queue->mutex);
        return false;
    }

    memcpy(pkt->data, data, size);
    pkt->size = size;
    pkt->pts = pts;

    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;

    cond_signal(&queue->cond);
    mutex_unlock(&queue->mutex);

    return true;
}

bool packet_queue_pop(packet_queue_t *queue, packet_t *packet) {
    mutex_lock(&queue->mutex);

    while (queue->count == 0 && !queue->finished) {
        cond_wait(&queue->cond, &queue->mutex);
    }

    if (queue->count == 0) {
        mutex_unlock(&queue->mutex);
        return false;
    }

    *packet = queue->packets[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;

    cond_signal(&queue->cond);
    mutex_unlock(&queue->mutex);

    return true;
}

void packet_queue_finish(packet_queue_t *queue) {
    mutex_lock(&queue->mutex);
    queue->finished = true;
    cond_broadcast(&queue->cond);
    mutex_unlock(&queue->mutex);
}
