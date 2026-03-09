#ifndef CROFT_HOST_POPUP_MENU_H
#define CROFT_HOST_POPUP_MENU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct host_popup_menu_item {
    int32_t action_id;
    const char* label;
    uint8_t enabled;
    uint8_t separator;
} host_popup_menu_item;

typedef enum host_popup_menu_result {
    HOST_POPUP_MENU_RESULT_OK = 0,
    HOST_POPUP_MENU_RESULT_EMPTY = 1,
    HOST_POPUP_MENU_RESULT_UNAVAILABLE = 2,
    HOST_POPUP_MENU_RESULT_INTERNAL = 3
} host_popup_menu_result;

host_popup_menu_result host_popup_menu_show(const host_popup_menu_item* items,
                                            uint32_t item_count,
                                            float x,
                                            float y,
                                            int32_t* action_id_out);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_HOST_POPUP_MENU_H */
