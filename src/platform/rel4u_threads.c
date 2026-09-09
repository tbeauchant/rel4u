#define _POSIX_C_SOURCE 200112L
#include "rel4u_threads.h"
#include <errno.h>

#if defined(_WIN32)
#include <stdlib.h>

typedef struct {
    rel4u_thread_func_t func;
    void* arg;
} rel4u_win32_thread_context_t;

static DWORD WINAPI rel4u_win32_thread_trampoline(void* lpParam) {
    rel4u_win32_thread_context_t ctx = *(rel4u_win32_thread_context_t*)lpParam;
    free(lpParam);
    ctx.func(ctx.arg);
    return 0;
}

int rel4u_mutex_init(rel4u_mutex_t* mutex) {
    InitializeCriticalSection(mutex);
    return 0;
}

void rel4u_mutex_destroy(rel4u_mutex_t* mutex) {
    DeleteCriticalSection(mutex);
}

void rel4u_mutex_lock(rel4u_mutex_t* mutex) {
    EnterCriticalSection(mutex);
}

void rel4u_mutex_unlock(rel4u_mutex_t* mutex) {
    LeaveCriticalSection(mutex);
}

int rel4u_cond_init(rel4u_cond_t* cond) {
    InitializeConditionVariable(cond);
    return 0;
}

void rel4u_cond_destroy(rel4u_cond_t* cond) {
    (void)cond;
}

void rel4u_cond_wait(rel4u_cond_t* cond, rel4u_mutex_t* mutex) {
    SleepConditionVariableCS(cond, mutex, INFINITE);
}

bool rel4u_cond_timedwait_ms(rel4u_cond_t* cond, rel4u_mutex_t* mutex, uint32_t timeout_ms) {
    BOOL result = SleepConditionVariableCS(cond, mutex, timeout_ms);
    return result != 0;
}

void rel4u_cond_signal(rel4u_cond_t* cond) {
    WakeConditionVariable(cond);
}

void rel4u_cond_broadcast(rel4u_cond_t* cond) {
    WakeAllConditionVariable(cond);
}

int rel4u_thread_create(rel4u_thread_t* thread, rel4u_thread_func_t func, void* arg) {
    rel4u_win32_thread_context_t* ctx = (rel4u_win32_thread_context_t*)malloc(sizeof(rel4u_win32_thread_context_t));
    if (!ctx) return -1;
    ctx->func = func;
    ctx->arg = arg;

    *thread = CreateThread(NULL, 0, rel4u_win32_thread_trampoline, ctx, 0, NULL);
    if (*thread == NULL) {
        free(ctx);
        return -1;
    }
    return 0;
}

int rel4u_thread_join(rel4u_thread_t thread) {
    if (thread == NULL) return 0;
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return 0;
}

#else
#include <time.h>
#include <sys/time.h>

int rel4u_mutex_init(rel4u_mutex_t* mutex) {
    return pthread_mutex_init(mutex, NULL);
}

void rel4u_mutex_destroy(rel4u_mutex_t* mutex) {
    pthread_mutex_destroy(mutex);
}

void rel4u_mutex_lock(rel4u_mutex_t* mutex) {
    pthread_mutex_lock(mutex);
}

void rel4u_mutex_unlock(rel4u_mutex_t* mutex) {
    pthread_mutex_unlock(mutex);
}

int rel4u_cond_init(rel4u_cond_t* cond) {
    return pthread_cond_init(cond, NULL);
}

void rel4u_cond_destroy(rel4u_cond_t* cond) {
    pthread_cond_destroy(cond);
}

void rel4u_cond_wait(rel4u_cond_t* cond, rel4u_mutex_t* mutex) {
    pthread_cond_wait(cond, mutex);
}

bool rel4u_cond_timedwait_ms(rel4u_cond_t* cond, rel4u_mutex_t* mutex, uint32_t timeout_ms) {
    struct timespec ts;
    struct timeval tv;
    gettimeofday(&tv, NULL);

    uint64_t ns = (uint64_t)tv.tv_usec * 1000ULL + (uint64_t)timeout_ms * 1000000ULL;
    ts.tv_sec = tv.tv_sec + (time_t)(ns / 1000000000ULL);
    ts.tv_nsec = (long)(ns % 1000000000ULL);

    int res = pthread_cond_timedwait(cond, mutex, &ts);
    return (res == 0);
}

void rel4u_cond_signal(rel4u_cond_t* cond) {
    pthread_cond_signal(cond);
}

void rel4u_cond_broadcast(rel4u_cond_t* cond) {
    pthread_cond_broadcast(cond);
}

int rel4u_thread_create(rel4u_thread_t* thread, rel4u_thread_func_t func, void* arg) {
    return pthread_create(thread, NULL, func, arg);
}

int rel4u_thread_join(rel4u_thread_t thread) {
    return pthread_join(thread, NULL);
}

#endif
