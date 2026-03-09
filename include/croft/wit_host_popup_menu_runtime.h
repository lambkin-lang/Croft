#ifndef CROFT_WIT_HOST_POPUP_MENU_RUNTIME_H
#define CROFT_WIT_HOST_POPUP_MENU_RUNTIME_H

#include "generated/wit_host_popup_menu.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_wit_host_popup_menu_runtime croft_wit_host_popup_menu_runtime;

croft_wit_host_popup_menu_runtime* croft_wit_host_popup_menu_runtime_create(void);
void croft_wit_host_popup_menu_runtime_destroy(croft_wit_host_popup_menu_runtime* runtime);

int32_t croft_wit_host_popup_menu_runtime_dispatch(croft_wit_host_popup_menu_runtime* runtime,
                                                   const SapWitHostPopupMenuCommand* command,
                                                   SapWitHostPopupMenuReply* reply_out);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_HOST_POPUP_MENU_RUNTIME_H */
