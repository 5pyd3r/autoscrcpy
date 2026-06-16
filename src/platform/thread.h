#ifndef THREAD_H
#define THREAD_H

#include <stdbool.h>

#ifdef _WIN32
    #include <windows.h>
    typedef HANDLE thread_t;
    typedef CRITICAL_SECTION mutex_t;
    typedef CONDITION_VARIABLE cond_t;
#else
    #include <pthread.h>
    typedef pthread_t thread_t;
    typedef pthread_mutex_t mutex_t;
    typedef pthread_cond_t cond_t;
#endif

typedef void *(*thread_func_t)(void *arg);

bool thread_create(thread_t *thread, thread_func_t func, void *arg);
bool thread_join(thread_t thread);
bool thread_detach(thread_t thread);

bool mutex_init(mutex_t *mutex);
void mutex_destroy(mutex_t *mutex);
void mutex_lock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);

bool cond_init(cond_t *cond);
void cond_destroy(cond_t *cond);
void cond_wait(cond_t *cond, mutex_t *mutex);
void cond_signal(cond_t *cond);
void cond_broadcast(cond_t *cond);

#endif /* THREAD_H */
