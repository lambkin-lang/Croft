/*
 * croft/host_time.h — Monotonic time with Wasm‑shaped signature.
 *
 * host_time_millis() returns a uint64_t monotonic timestamp matching
 * the Wasm import from DEVELOPMENT_PLAN §4.2.
 */

#ifndef CROFT_HOST_TIME_H
#define CROFT_HOST_TIME_H

#include "croft/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Return monotonic time in milliseconds since an unspecified epoch.
 * The value only increases (or stays equal) between successive calls.
 */
uint64_t host_time_millis(void);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_HOST_TIME_H */
