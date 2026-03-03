/*
 * croft/host_thread.h — Portable thread / mutex / condvar wrappers.
 *
 * Wraps pthreads on macOS/Linux.  A Win32 implementation will be added
 * when Windows support lands (DEVELOPMENT_PLAN §3.5).
 */

#ifndef CROFT_HOST_THREAD_H
#define CROFT_HOST_THREAD_H

#include "croft/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Opaque types ─────────────────────────────────────────────────── */

#if defined(CROFT_OS_MACOS) || defined(CROFT_OS_LINUX)
    #include <pthread.h>
    typedef pthread_t       host_thread_t;
    typedef pthread_mutex_t host_mutex_t;
    typedef pthread_cond_t  host_cond_t;
#elif defined(CROFT_OS_WINDOWS)
    /* Placeholder — will use Win32 CRITICAL_SECTION / CONDITION_VARIABLE. */
    typedef void* host_thread_t;
    typedef void* host_mutex_t;
    typedef void* host_cond_t;
#endif

/* ── Thread entry point ───────────────────────────────────────────── */

typedef void *(*host_thread_fn)(void *arg);

/* ── Thread API ───────────────────────────────────────────────────── */

int  host_thread_create(host_thread_t *out, host_thread_fn fn, void *arg);
int  host_thread_join(host_thread_t thread, void **retval);

/* ── Mutex API ────────────────────────────────────────────────────── */

int  host_mutex_init(host_mutex_t *m);
int  host_mutex_lock(host_mutex_t *m);
int  host_mutex_unlock(host_mutex_t *m);
void host_mutex_destroy(host_mutex_t *m);

/* ── Condition variable API ───────────────────────────────────────── */

int  host_cond_init(host_cond_t *c);
int  host_cond_wait(host_cond_t *c, host_mutex_t *m);
int  host_cond_signal(host_cond_t *c);
int  host_cond_broadcast(host_cond_t *c);
void host_cond_destroy(host_cond_t *c);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_HOST_THREAD_H */
