/*
 * nomalloc.h — Compile-time enforcement of arena-only allocation
 *
 * When SAP_NO_MALLOC is defined, this header poisons malloc, calloc,
 * realloc, and free so that any accidental use in arena-migrated
 * subsystem files triggers a compile error.
 *
 * Include this as the LAST header in each migrated .c file.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 lambkin-lang
 */
#ifndef SAPLING_NOMALLOC_H
#define SAPLING_NOMALLOC_H

#include <stddef.h>

#ifdef LAMBKIN_CORE
#undef assert
#define assert(x) ((void)0)
#undef malloc
#define malloc(x) (NULL)
#undef calloc
#define calloc(x, y) (NULL)
#undef realloc
#define realloc(x, y) (NULL)
#undef free
#define free(x) ((void)0)
#undef printf
#define printf(...) ((void)0)
#undef fflush
#define fflush(x) ((void)0)
#undef sap_fi_should_fail
#define sap_fi_should_fail(fi, site) (0)

static inline int sap__lamkin_memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = s1, *p2 = s2;
    for(size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return p1[i] < p2[i] ? -1 : 1;
    }
    return 0;
}
#undef __builtin_memcmp
#define __builtin_memcmp sap__lamkin_memcmp
#endif

#ifdef SAP_NO_MALLOC
#pragma GCC poison malloc calloc realloc free
#endif

#endif /* SAPLING_NOMALLOC_H */
