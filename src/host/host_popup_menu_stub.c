#include "croft/host_popup_menu.h"

host_popup_menu_result host_popup_menu_show(const host_popup_menu_item* items,
                                            uint32_t item_count,
                                            float x,
                                            float y,
                                            int32_t* action_id_out)
{
    (void)items;
    (void)item_count;
    (void)x;
    (void)y;
    if (action_id_out) {
        *action_id_out = 0;
    }
    return HOST_POPUP_MENU_RESULT_UNAVAILABLE;
}
