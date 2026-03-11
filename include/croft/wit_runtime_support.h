#ifndef CROFT_WIT_RUNTIME_SUPPORT_H
#define CROFT_WIT_RUNTIME_SUPPORT_H

#include <stddef.h>

#if !defined(CROFT_WIT_FREESTANDING)
#include <stdlib.h>
#include <string.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CROFT_WIT_FREESTANDING)
void *sap_wit_rt_malloc(size_t size);
void *sap_wit_rt_realloc(void *ptr, size_t size);
void sap_wit_rt_free(void *ptr);
void *sap_wit_rt_memcpy(void *dest, const void *src, size_t len);
void *sap_wit_rt_memset(void *dest, int value, size_t len);
size_t sap_wit_rt_strlen(const char *text);
#else
static inline void *sap_wit_rt_malloc(size_t size)
{
    return malloc(size);
}

static inline void *sap_wit_rt_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

static inline void sap_wit_rt_free(void *ptr)
{
    free(ptr);
}

static inline void *sap_wit_rt_memcpy(void *dest, const void *src, size_t len)
{
    return memcpy(dest, src, len);
}

static inline void *sap_wit_rt_memset(void *dest, int value, size_t len)
{
    return memset(dest, value, len);
}

static inline size_t sap_wit_rt_strlen(const char *text)
{
    return strlen(text);
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_RUNTIME_SUPPORT_H */
