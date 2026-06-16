#ifndef PACKET_QUEUE_H
#define PACKET_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include "../platform/thread.h"

typedef struct {
    uint8_t *data;
    uint32_t size;
    int64_t pts;
} packet_t;

typedef struct {
    packet_t *packets;
    int capacity;
    int count;
    int head;
    int tail;
    mutex_t mutex;
    cond_t cond;
    bool finished;
} packet_queue_t;

bool packet_queue_init(packet_queue_t *queue, int capacity);
void packet_queue_destroy(packet_queue_t *queue);
bool packet_queue_push(packet_queue_t *queue, const uint8_t *data, uint32_t size, int64_t pts);
bool packet_queue_pop(packet_queue_t *queue, packet_t *packet);
void packet_queue_finish(packet_queue_t *queue);

#endif /* PACKET_QUEUE_H */
