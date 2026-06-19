#include "greg_thread.h"

#ifdef _WIN32

int greg_thread_create(greg_thread_t *thread, greg_thread_fn_t func, void *arg) {
    *thread = CreateThread(NULL, 0, func, arg, 0, NULL);
    return (*thread != NULL) ? 0 : -1;
}

int greg_thread_join(greg_thread_t thread) {
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return 0;
}

int greg_mutex_init(greg_mutex_t *mutex) {
    InitializeCriticalSection(mutex);
    return 0;
}

int greg_mutex_destroy(greg_mutex_t *mutex) {
    DeleteCriticalSection(mutex);
    return 0;
}

int greg_mutex_lock(greg_mutex_t *mutex) {
    EnterCriticalSection(mutex);
    return 0;
}

int greg_mutex_unlock(greg_mutex_t *mutex) {
    LeaveCriticalSection(mutex);
    return 0;
}

int greg_cond_init(greg_cond_t *cond) {
    InitializeConditionVariable(cond);
    return 0;
}

int greg_cond_destroy(greg_cond_t *cond) {
    (void)cond; // No explicit destroy needed for Windows Condition Variables
    return 0;
}

int greg_cond_wait(greg_cond_t *cond, greg_mutex_t *mutex) {
    SleepConditionVariableCS(cond, mutex, INFINITE);
    return 0;
}

int greg_cond_signal(greg_cond_t *cond) {
    WakeConditionVariable(cond);
    return 0;
}

int greg_cond_broadcast(greg_cond_t *cond) {
    WakeAllConditionVariable(cond);
    return 0;
}

#else // POSIX

// ... (other posix stuff omitted for targeting, but let's be careful about exact block)

int greg_thread_create(greg_thread_t *thread, greg_thread_fn_t func, void *arg) {
    return pthread_create(thread, NULL, func, arg);
}

int greg_thread_join(greg_thread_t thread) {
    return pthread_join(thread, NULL);
}

int greg_mutex_init(greg_mutex_t *mutex) {
    return pthread_mutex_init(mutex, NULL);
}

int greg_mutex_destroy(greg_mutex_t *mutex) {
    return pthread_mutex_destroy(mutex);
}

int greg_mutex_lock(greg_mutex_t *mutex) {
    return pthread_mutex_lock(mutex);
}

int greg_mutex_unlock(greg_mutex_t *mutex) {
    return pthread_mutex_unlock(mutex);
}

int greg_cond_init(greg_cond_t *cond) {
    return pthread_cond_init(cond, NULL);
}

int greg_cond_destroy(greg_cond_t *cond) {
    return pthread_cond_destroy(cond);
}

int greg_cond_wait(greg_cond_t *cond, greg_mutex_t *mutex) {
    return pthread_cond_wait(cond, mutex);
}

int greg_cond_signal(greg_cond_t *cond) {
    return pthread_cond_signal(cond);
}

int greg_cond_broadcast(greg_cond_t *cond) {
    return pthread_cond_broadcast(cond);
}

#endif
