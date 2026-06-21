#include "message_queue.h"
#include "../platform/log.h"
#include <string.h>

bool
script_msg_queue_init(script_msg_queue_t *q)
{
    memset(q, 0, sizeof(*q));
    InitializeCriticalSection(&q->cs);
    q->semaphore = CreateSemaphoreA(NULL, 0, SCRIPT_MSG_QUEUE_CAPACITY, NULL);
    if (!q->semaphore) {
        log_error("Failed to create message queue semaphore");
        DeleteCriticalSection(&q->cs);
        return false;
    }
    q->initialized = true;
    return true;
}

void
script_msg_queue_destroy(script_msg_queue_t *q)
{
    if (!q->initialized) return;
    DeleteCriticalSection(&q->cs);
    CloseHandle(q->semaphore);
    q->semaphore = NULL;
    q->initialized = false;
}

bool
script_msg_queue_send(script_msg_queue_t *q, const script_msg_t *msg)
{
    EnterCriticalSection(&q->cs);
    if (q->count >= SCRIPT_MSG_QUEUE_CAPACITY) {
        LeaveCriticalSection(&q->cs);
        log_warn("Message queue full, dropping message type %d", msg->type);
        return false;
    }
    q->items[q->tail] = *msg;
    q->tail = (q->tail + 1) % SCRIPT_MSG_QUEUE_CAPACITY;
    q->count++;
    LeaveCriticalSection(&q->cs);
    ReleaseSemaphore(q->semaphore, 1, NULL);
    return true;
}

bool
script_msg_queue_recv(script_msg_queue_t *q, script_msg_t *msg, uint32_t timeout_ms)
{
    DWORD result = WaitForSingleObject(q->semaphore, (DWORD)timeout_ms);
    if (result != WAIT_OBJECT_0) {
        return false;
    }
    EnterCriticalSection(&q->cs);
    *msg = q->items[q->head];
    q->head = (q->head + 1) % SCRIPT_MSG_QUEUE_CAPACITY;
    q->count--;
    LeaveCriticalSection(&q->cs);
    return true;
}

bool
script_msg_queue_try_recv(script_msg_queue_t *q, script_msg_t *msg)
{
    return script_msg_queue_recv(q, msg, 0);
}

void
script_msg_queue_drain(script_msg_queue_t *q)
{
    EnterCriticalSection(&q->cs);
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    LeaveCriticalSection(&q->cs);
    /* Drain the semaphore */
    while (WaitForSingleObject(q->semaphore, 0) == WAIT_OBJECT_0) {
        /* discard */
    }
}
