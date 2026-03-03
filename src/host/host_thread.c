/*
 * host_thread.c — pthread wrapper (macOS / Linux).
 */

#include "croft/host_thread.h"

#if defined(CROFT_OS_MACOS) || defined(CROFT_OS_LINUX)

/* ── Thread ───────────────────────────────────────────────────────── */

int host_thread_create(host_thread_t *out, host_thread_fn fn, void *arg)
{
    return pthread_create(out, NULL, fn, arg);
}

int host_thread_join(host_thread_t thread, void **retval)
{
    return pthread_join(thread, retval);
}

/* ── Mutex ────────────────────────────────────────────────────────── */

int host_mutex_init(host_mutex_t *m)
{
    return pthread_mutex_init(m, NULL);
}

int host_mutex_lock(host_mutex_t *m)
{
    return pthread_mutex_lock(m);
}

int host_mutex_unlock(host_mutex_t *m)
{
    return pthread_mutex_unlock(m);
}

void host_mutex_destroy(host_mutex_t *m)
{
    pthread_mutex_destroy(m);
}

/* ── Condition variable ───────────────────────────────────────────── */

int host_cond_init(host_cond_t *c)
{
    return pthread_cond_init(c, NULL);
}

int host_cond_wait(host_cond_t *c, host_mutex_t *m)
{
    return pthread_cond_wait(c, m);
}

int host_cond_signal(host_cond_t *c)
{
    return pthread_cond_signal(c);
}

int host_cond_broadcast(host_cond_t *c)
{
    return pthread_cond_broadcast(c);
}

void host_cond_destroy(host_cond_t *c)
{
    pthread_cond_destroy(c);
}

#endif /* CROFT_OS_MACOS || CROFT_OS_LINUX */
