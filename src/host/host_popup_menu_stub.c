#include "croft/host_popup_menu.h"

host_popup_menu_result host_popup_menu_show_with_text_context(const host_popup_menu_item* items,
                                                              uint32_t item_count,
                                                              float x,
                                                              float y,
                                                              const host_popup_menu_text_context* text_context,
                                                              int32_t* action_id_out)
{
    (void)text_context;
    return host_popup_menu_show(items, item_count, x, y, action_id_out);
}

host_popup_menu_result host_popup_menu_show_with_context(const host_popup_menu_item* items,
                                                         uint32_t item_count,
                                                         float x,
                                                         float y,
                                                         const char* contextual_utf8,
                                                         size_t contextual_utf8_len,
                                                         uint8_t include_native_text_services,
                                                         int32_t* action_id_out)
{
    (void)contextual_utf8;
    (void)contextual_utf8_len;
    (void)include_native_text_services;
    return host_popup_menu_show(items, item_count, x, y, action_id_out);
}

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
