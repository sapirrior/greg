#ifndef GREG_THREAD_H
#define GREG_THREAD_H

#ifdef _WIN32
    #include <windows.h>
    typedef HANDLE greg_thread_t;
    typedef CRITICAL_SECTION greg_mutex_t;
    typedef CONDITION_VARIABLE greg_cond_t;
    #define GREG_THREAD_ROUTINE DWORD WINAPI
    typedef DWORD (WINAPI *greg_thread_fn_t)(LPVOID);
#else
    #include <pthread.h>
    typedef pthread_t greg_thread_t;
    typedef pthread_mutex_t greg_mutex_t;
    typedef pthread_cond_t greg_cond_t;
    #define GREG_THREAD_ROUTINE void*
    typedef void* (*greg_thread_fn_t)(void*);
#endif

// Thread functions
int greg_thread_create(greg_thread_t *thread, greg_thread_fn_t func, void *arg);
int greg_thread_join(greg_thread_t thread);

// Mutex functions
int greg_mutex_init(greg_mutex_t *mutex);
int greg_mutex_destroy(greg_mutex_t *mutex);
int greg_mutex_lock(greg_mutex_t *mutex);
int greg_mutex_unlock(greg_mutex_t *mutex);

// Condition Variable functions
int greg_cond_init(greg_cond_t *cond);
int greg_cond_destroy(greg_cond_t *cond);
int greg_cond_wait(greg_cond_t *cond, greg_mutex_t *mutex);
int greg_cond_signal(greg_cond_t *cond);
int greg_cond_broadcast(greg_cond_t *cond);

#endif // GREG_THREAD_H
