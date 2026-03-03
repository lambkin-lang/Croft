/*
 * croft/host_log.h — Logging with Wasm‑shaped signature.
 *
 * The signature  host_log(int level, const char *ptr, uint32_t len)
 * mirrors the Wasm import defined in DEVELOPMENT_PLAN §4.2 so that the
 * same call works from native C or from a Wasm guest.
 */

#ifndef CROFT_HOST_LOG_H
#define CROFT_HOST_LOG_H

#include "croft/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Log levels (0 = trace … 4 = error). */
enum {
    CROFT_LOG_TRACE = 0,
    CROFT_LOG_DEBUG = 1,
    CROFT_LOG_INFO  = 2,
    CROFT_LOG_WARN  = 3,
    CROFT_LOG_ERROR = 4
};

/*
 * Write a log message.
 *
 * @param level  One of the CROFT_LOG_* constants.
 * @param ptr    Pointer to a UTF‑8 string (need not be NUL‑terminated).
 * @param len    Byte length of the string.
 */
void host_log(int level, const char *ptr, uint32_t len);

/*
 * Set the minimum log level.  Messages below this level are silently
 * discarded.  The default is CROFT_LOG_TRACE (everything).
 */
void host_log_set_level(int min_level);

/*
 * Install a custom log sink.  If @p sink is NULL the default (stderr)
 * sink is restored.  The sink receives the same arguments as host_log.
 */
typedef void (*host_log_sink_fn)(int level, const char *ptr, uint32_t len,
                                 void *userdata);

void host_log_set_sink(host_log_sink_fn sink, void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_HOST_LOG_H */
