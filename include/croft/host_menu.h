#ifndef CROFT_HOST_MENU_H
#define CROFT_HOST_MENU_H

#include "generated/wit_menu_schema.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*host_menu_callback_t)(int32_t action_id);

/**
 * Apply a streamed menu intent representing an abstract node update.
 * Implemented natively per-OS (e.g. mac/host_menu.m).
 */
void host_menu_apply_intent(const SapWitMenuIntent* intent);

/**
 * Register a global callback to receive action_ids when the user clicks an item.
 */
void host_menu_set_callback(host_menu_callback_t cb);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_HOST_MENU_H */
