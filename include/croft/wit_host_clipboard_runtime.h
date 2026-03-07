#ifndef CROFT_WIT_HOST_CLIPBOARD_RUNTIME_H
#define CROFT_WIT_HOST_CLIPBOARD_RUNTIME_H

#include "generated/wit_host_clipboard.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_wit_host_clipboard_runtime croft_wit_host_clipboard_runtime;

croft_wit_host_clipboard_runtime* croft_wit_host_clipboard_runtime_create(void);
void croft_wit_host_clipboard_runtime_destroy(croft_wit_host_clipboard_runtime* runtime);

int32_t croft_wit_host_clipboard_runtime_dispatch(croft_wit_host_clipboard_runtime* runtime,
                                                  const SapWitHostClipboardCommand* command,
                                                  SapWitHostClipboardReply* reply_out);

void croft_wit_host_clipboard_reply_dispose(SapWitHostClipboardReply* reply);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_HOST_CLIPBOARD_RUNTIME_H */
