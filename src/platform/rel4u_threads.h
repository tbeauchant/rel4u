#ifndef REL4U_THREADS_H
#define REL4U_THREADS_H

#include <stdint.h>
#include <stdbool.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef HANDLE rel4u_thread_t;
typedef CRITICAL_SECTION rel4u_mutex_t;
typedef CONDITION_VARIABLE rel4u_cond_t;

#else
#include <pthread.h>

typedef pthread_t rel4u_thread_t;
typedef pthread_mutex_t rel4u_mutex_t;
typedef pthread_cond_t rel4u_cond_t;

#endif

typedef void* (*rel4u_thread_func_t)(void*);

/* Mutex Functions */
int  rel4u_mutex_init(rel4u_mutex_t* mutex);
void rel4u_mutex_destroy(rel4u_mutex_t* mutex);
void rel4u_mutex_lock(rel4u_mutex_t* mutex);
void rel4u_mutex_unlock(rel4u_mutex_t* mutex);

/* Condition Variable Functions */
int  rel4u_cond_init(rel4u_cond_t* cond);
void rel4u_cond_destroy(rel4u_cond_t* cond);
void rel4u_cond_wait(rel4u_cond_t* cond, rel4u_mutex_t* mutex);
bool rel4u_cond_timedwait_ms(rel4u_cond_t* cond, rel4u_mutex_t* mutex, uint32_t timeout_ms);
void rel4u_cond_signal(rel4u_cond_t* cond);
void rel4u_cond_broadcast(rel4u_cond_t* cond);

/* Thread Functions */
int  rel4u_thread_create(rel4u_thread_t* thread, rel4u_thread_func_t func, void* arg);
int  rel4u_thread_join(rel4u_thread_t thread);

#endif /* REL4U_THREADS_H */
