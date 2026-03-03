/*
 * croft/platform.h — OS / compiler detection and base types.
 *
 * This header is included by every other Croft header.  It provides:
 *   - CROFT_OS_*   macros  (MACOS, WINDOWS, LINUX)
 *   - CROFT_CC_*   macros  (GCC, CLANG, MSVC)
 *   - Fixed‑width integer types via <stdint.h>
 *   - CROFT_EXPORT / CROFT_INLINE helpers
 */

#ifndef CROFT_PLATFORM_H
#define CROFT_PLATFORM_H

/* ── OS detection ─────────────────────────────────────────────────── */

#if defined(__APPLE__) && defined(__MACH__)
    #define CROFT_OS_MACOS  1
#elif defined(_WIN32)
    #define CROFT_OS_WINDOWS 1
#elif defined(__linux__)
    #define CROFT_OS_LINUX  1
#else
    #error "Unsupported platform"
#endif

/* ── Compiler detection ───────────────────────────────────────────── */

#if defined(__clang__)
    #define CROFT_CC_CLANG  1
#elif defined(__GNUC__)
    #define CROFT_CC_GCC    1
#elif defined(_MSC_VER)
    #define CROFT_CC_MSVC   1
#endif

/* ── Visibility / inline helpers ──────────────────────────────────── */

#if defined(CROFT_OS_WINDOWS) && defined(CROFT_CC_MSVC)
    #define CROFT_EXPORT __declspec(dllexport)
#elif defined(CROFT_CC_GCC) || defined(CROFT_CC_CLANG)
    #define CROFT_EXPORT __attribute__((visibility("default")))
#else
    #define CROFT_EXPORT
#endif

#define CROFT_INLINE static inline

/* ── Fixed‑width types ────────────────────────────────────────────── */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#endif /* CROFT_PLATFORM_H */
