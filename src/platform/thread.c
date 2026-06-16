#include "thread.h"

bool thread_create(thread_t *thread, thread_func_t func, void *arg) {
#ifdef _WIN32
    *thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
    return *thread != NULL;
#else
    return pthread_create(thread, NULL, func, arg) == 0;
#endif
}

bool thread_join(thread_t thread) {
#ifdef _WIN32
    return WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0;
#else
    return pthread_join(thread, NULL) == 0;
#endif
}

bool thread_detach(thread_t thread) {
#ifdef _WIN32
    return CloseHandle(thread);
#else
    return pthread_detach(thread) == 0;
#endif
}

bool mutex_init(mutex_t *mutex) {
#ifdef _WIN32
    InitializeCriticalSection(mutex);
    return true;
#else
    return pthread_mutex_init(mutex, NULL) == 0;
#endif
}

void mutex_destroy(mutex_t *mutex) {
#ifdef _WIN32
    DeleteCriticalSection(mutex);
#else
    pthread_mutex_destroy(mutex);
#endif
}

void mutex_lock(mutex_t *mutex) {
#ifdef _WIN32
    EnterCriticalSection(mutex);
#else
    pthread_mutex_lock(mutex);
#endif
}

void mutex_unlock(mutex_t *mutex) {
#ifdef _WIN32
    LeaveCriticalSection(mutex);
#else
    pthread_mutex_unlock(mutex);
#endif
}

bool cond_init(cond_t *cond) {
#ifdef _WIN32
    InitializeConditionVariable(cond);
    return true;
#else
    return pthread_cond_init(cond, NULL) == 0;
#endif
}

void cond_destroy(cond_t *cond) {
#ifdef _WIN32
    // No cleanup needed for ConditionVariable
    (void)cond;
#else
    pthread_cond_destroy(cond);
#endif
}

void cond_wait(cond_t *cond, mutex_t *mutex) {
#ifdef _WIN32
    SleepConditionVariableCS(cond, mutex, INFINITE);
#else
    pthread_cond_wait(cond, mutex);
#endif
}

void cond_signal(cond_t *cond) {
#ifdef _WIN32
    WakeConditionVariable(cond);
#else
    pthread_cond_signal(cond);
#endif
}

void cond_broadcast(cond_t *cond) {
#ifdef _WIN32
    WakeAllConditionVariable(cond);
#else
    pthread_cond_broadcast(cond);
#endif
}
