#ifndef CROFT_WIT_HOST_FS_RUNTIME_H
#define CROFT_WIT_HOST_FS_RUNTIME_H

#include "generated/wit_host_fs.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_wit_host_fs_runtime croft_wit_host_fs_runtime;

croft_wit_host_fs_runtime* croft_wit_host_fs_runtime_create(void);

void croft_wit_host_fs_runtime_destroy(croft_wit_host_fs_runtime* runtime);

/*
 * This boundary translates the existing native `host_fs` handle vocabulary into
 * WIT-visible resource IDs so model programs never observe raw native pointers.
 */
int32_t croft_wit_host_fs_runtime_dispatch(croft_wit_host_fs_runtime* runtime,
                                           const SapWitHostFsFsCommand* command,
                                           SapWitHostFsFsReply* reply_out);

void croft_wit_host_fs_reply_dispose(SapWitHostFsFsReply* reply);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_HOST_FS_RUNTIME_H */
