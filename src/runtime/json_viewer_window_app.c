#include "croft/json_viewer_window_app.h"
#include "croft/wit_runtime_support.h"

#if defined(__has_include) && __has_include("generated/wit_host_clock.h")
#include "generated/wit_host_clock.h"
#include "generated/wit_host_gpu2d.h"
#include "generated/wit_host_window.h"
#else
#include "wit_host_clock.h"
#include "wit_host_gpu2d.h"
#include "wit_host_window.h"
#endif

enum {
    CROFT_JSON_VIEWER_MOUSE_PRESS = 1,
    CROFT_JSON_VIEWER_MOUSE_LEFT = 0
};

typedef struct {
    float sidebar_x;
    float sidebar_y;
    float sidebar_w;
    float sidebar_h;
    float sidebar_rows_y;
    float sidebar_row_h;
    float content_x;
    float content_y;
    float content_w;
    float content_h;
    float content_text_y;
    float content_text_x;
    float content_bottom;
    float footer_y;
} CroftJsonViewerWindowLayout;

static int expect_window_ok(const SapWitHostWindowReply *reply,
                            SapWitHostWindowResource *handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_WINDOW_REPLY_WINDOW || !reply->val.window.is_v_ok) {
        return 0;
    }
    *handle_out = reply->val.window.v_val.ok.v;
    return 1;
}

static int expect_window_status_ok(const SapWitHostWindowReply *reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_WINDOW_REPLY_STATUS
        && reply->val.status.is_v_ok;
}

static int expect_window_event(const SapWitHostWindowReply *reply, SapWitHostWindowEvent *event_out)
{
    if (!reply || !event_out) {
        return -1;
    }
    if (reply->case_tag != SAP_WIT_HOST_WINDOW_REPLY_EVENT || !reply->val.event.is_v_ok) {
        return -1;
    }
    if (!reply->val.event.v_val.ok.has_v) {
        return 0;
    }
    *event_out = reply->val.event.v_val.ok.v;
    return 1;
}

static int expect_window_bool(const SapWitHostWindowReply *reply, uint8_t *value_out)
{
    if (!reply || !value_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_WINDOW_REPLY_SHOULD_CLOSE
            || !reply->val.should_close.is_v_ok) {
        return 0;
    }
    *value_out = reply->val.should_close.v_val.ok.v;
    return 1;
}

static int expect_window_size(const SapWitHostWindowReply *reply,
                              uint32_t *width_out,
                              uint32_t *height_out)
{
    if (!reply || !width_out || !height_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_WINDOW_REPLY_SIZE || !reply->val.size.is_v_ok) {
        return 0;
    }
    *width_out = reply->val.size.v_val.ok.v.width;
    *height_out = reply->val.size.v_val.ok.v.height;
    return 1;
}

static int expect_surface_ok(const SapWitHostGpu2dReply *reply,
                             SapWitHostGpu2dSurfaceResource *handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_GPU2D_REPLY_SURFACE || !reply->val.surface.is_v_ok) {
        return 0;
    }
    *handle_out = reply->val.surface.v_val.ok.v;
    return 1;
}

static int expect_gpu_status_ok(const SapWitHostGpu2dReply *reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_GPU2D_REPLY_STATUS
        && reply->val.status.is_v_ok;
}

static int expect_gpu_caps_ok(const SapWitHostGpu2dReply *reply, uint32_t *caps_out)
{
    if (!reply || !caps_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_GPU2D_REPLY_CAPABILITIES
            || !reply->val.capabilities.is_v_ok) {
        return 0;
    }
    *caps_out = reply->val.capabilities.v_val.ok.v;
    return 1;
}

static int expect_measure_ok(const SapWitHostGpu2dReply *reply, float *width_out)
{
    if (!reply || !width_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_GPU2D_REPLY_MEASURE || !reply->val.measure.is_v_ok) {
        return 0;
    }
    *width_out = reply->val.measure.v_val.ok.v;
    return 1;
}

static int expect_clock_now(const SapWitHostClockReply *reply, uint64_t *now_out)
{
    if (!reply || !now_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_CLOCK_REPLY_NOW || !reply->val.now.is_v_ok) {
        return 0;
    }
    *now_out = reply->val.now.v_val.ok.v;
    return 1;
}

static void croft_json_viewer_layout_init(CroftJsonViewerWindowLayout *layout,
                                          uint32_t width,
                                          uint32_t height)
{
    if (!layout) {
        return;
    }
    layout->sidebar_x = 20.0f;
    layout->sidebar_y = 92.0f;
    layout->sidebar_w = 252.0f;
    layout->sidebar_h = (float)height - 166.0f;
    layout->sidebar_rows_y = 156.0f;
    layout->sidebar_row_h = 26.0f;
    layout->content_x = 292.0f;
    layout->content_y = 92.0f;
    layout->content_w = (float)width - 312.0f;
    layout->content_h = (float)height - 166.0f;
    layout->content_text_y = 176.0f;
    layout->content_text_x = 316.0f;
    layout->content_bottom = (float)height - 74.0f;
    layout->footer_y = (float)height - 32.0f;
}

static int croft_json_viewer_gpu_draw_rect(const CroftJsonViewerWindowAppConfig *config,
                                           SapWitHostGpu2dSurfaceResource surface,
                                           float x,
                                           float y,
                                           float w,
                                           float h,
                                           uint32_t color_rgba)
{
    SapWitHostGpu2dCommand gpu_cmd = {0};
    SapWitHostGpu2dReply gpu_reply = {0};
    int rc;

    gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_DRAW_RECT;
    gpu_cmd.val.draw_rect.surface = surface;
    gpu_cmd.val.draw_rect.x = x;
    gpu_cmd.val.draw_rect.y = y;
    gpu_cmd.val.draw_rect.w = w;
    gpu_cmd.val.draw_rect.h = h;
    gpu_cmd.val.draw_rect.color_rgba = color_rgba;
    rc = config->gpu_dispatch(config->dispatch_ctx, &gpu_cmd, &gpu_reply);
    if (rc == ERR_OK) {
        rc = expect_gpu_status_ok(&gpu_reply) ? ERR_OK : ERR_TYPE;
        sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
    }
    return rc;
}

static int croft_json_viewer_gpu_measure_text(const CroftJsonViewerWindowAppConfig *config,
                                              SapWitHostGpu2dSurfaceResource surface,
                                              const uint8_t *text,
                                              uint32_t text_len,
                                              float font_size,
                                              uint8_t font_role,
                                              float *width_out)
{
    SapWitHostGpu2dCommand gpu_cmd = {0};
    SapWitHostGpu2dReply gpu_reply = {0};
    int rc;

    if (!width_out || (!text && text_len > 0u)) {
        return ERR_INVALID;
    }

    gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_MEASURE_TEXT;
    gpu_cmd.val.measure_text.surface = surface;
    gpu_cmd.val.measure_text.utf8_data = text;
    gpu_cmd.val.measure_text.utf8_len = text_len;
    gpu_cmd.val.measure_text.font_size = font_size;
    gpu_cmd.val.measure_text.font_role = font_role;
    rc = config->gpu_dispatch(config->dispatch_ctx, &gpu_cmd, &gpu_reply);
    if (rc == ERR_OK) {
        rc = expect_measure_ok(&gpu_reply, width_out) ? ERR_OK : ERR_TYPE;
        sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
    }
    return rc;
}

static int croft_json_viewer_gpu_draw_text(const CroftJsonViewerWindowAppConfig *config,
                                           SapWitHostGpu2dSurfaceResource surface,
                                           float x,
                                           float y,
                                           const uint8_t *text,
                                           uint32_t text_len,
                                           float font_size,
                                           uint8_t font_role,
                                           uint32_t color_rgba)
{
    SapWitHostGpu2dCommand gpu_cmd = {0};
    SapWitHostGpu2dReply gpu_reply = {0};
    int rc;

    gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_DRAW_TEXT;
    gpu_cmd.val.draw_text.surface = surface;
    gpu_cmd.val.draw_text.x = x;
    gpu_cmd.val.draw_text.y = y;
    gpu_cmd.val.draw_text.utf8_data = text;
    gpu_cmd.val.draw_text.utf8_len = text_len;
    gpu_cmd.val.draw_text.font_size = font_size;
    gpu_cmd.val.draw_text.font_role = font_role;
    gpu_cmd.val.draw_text.color_rgba = color_rgba;
    rc = config->gpu_dispatch(config->dispatch_ctx, &gpu_cmd, &gpu_reply);
    if (rc == ERR_OK) {
        rc = expect_gpu_status_ok(&gpu_reply) ? ERR_OK : ERR_TYPE;
        sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
    }
    return rc;
}

static const char *croft_json_viewer_display_path(const char *path)
{
    if (!path || (path[0] == '.' && path[1] == '\0')) {
        return "root";
    }
    return path;
}

static uint32_t croft_json_viewer_copy_text(char *out, uint32_t out_cap, const char *text)
{
    uint32_t len = 0u;

    if (!out || out_cap == 0u) {
        return 0u;
    }
    out[0] = '\0';
    if (!text) {
        return 0u;
    }
    len = (uint32_t)sap_wit_rt_strlen(text);
    if (len >= out_cap) {
        len = out_cap - 1u;
    }
    if (len > 0u) {
        sap_wit_rt_memcpy(out, text, len);
    }
    out[len] = '\0';
    return len;
}

static uint32_t croft_json_viewer_fit_text(const CroftJsonViewerWindowAppConfig *config,
                                           SapWitHostGpu2dSurfaceResource surface,
                                           const char *text,
                                           float font_size,
                                           uint8_t font_role,
                                           float max_width,
                                           char *out,
                                           uint32_t out_cap)
{
    static const char k_ellipsis[] = "...";
    uint32_t len;
    float width = 0.0f;

    if (!out || out_cap == 0u || max_width <= 0.0f) {
        return 0u;
    }

    len = croft_json_viewer_copy_text(out, out_cap, text);
    if (len == 0u) {
        return 0u;
    }
    if (croft_json_viewer_gpu_measure_text(config,
                                           surface,
                                           (const uint8_t *)out,
                                           len,
                                           font_size,
                                           font_role,
                                           &width) != ERR_OK) {
        return len;
    }
    if (width <= max_width) {
        return len;
    }
    if (out_cap <= sizeof(k_ellipsis)) {
        out[0] = '\0';
        return 0u;
    }
    if (croft_json_viewer_gpu_measure_text(config,
                                           surface,
                                           (const uint8_t *)k_ellipsis,
                                           3u,
                                           font_size,
                                           font_role,
                                           &width) != ERR_OK
            || width > max_width) {
        out[0] = '\0';
        return 0u;
    }

    while (len > 0u) {
        uint32_t prefix_len = len - 1u;

        if (prefix_len > out_cap - sizeof(k_ellipsis)) {
            prefix_len = out_cap - sizeof(k_ellipsis);
        }
        if (prefix_len > 0u) {
            sap_wit_rt_memcpy(out, text, prefix_len);
        }
        sap_wit_rt_memcpy(out + prefix_len, k_ellipsis, 3u);
        out[prefix_len + 3u] = '\0';
        if (croft_json_viewer_gpu_measure_text(config,
                                               surface,
                                               (const uint8_t *)out,
                                               prefix_len + 3u,
                                               font_size,
                                               font_role,
                                               &width) == ERR_OK
                && width <= max_width) {
            return prefix_len + 3u;
        }
        len = prefix_len;
    }

    sap_wit_rt_memcpy(out, k_ellipsis, 3u);
    out[3] = '\0';
    return 3u;
}

static uint32_t croft_json_viewer_pick_footer_help(const CroftJsonViewerWindowAppConfig *config,
                                                   SapWitHostGpu2dSurfaceResource surface,
                                                   float font_size,
                                                   uint8_t font_role,
                                                   float max_width,
                                                   char *out,
                                                   uint32_t out_cap)
{
    static const char *const k_candidates[] = {
        "Click sidebar or JSON lines to select  click fold gutter  wheel scrolls",
        "Click rows or JSON lines to select  click fold gutter  wheel scrolls",
        "Click to select  click fold gutter  wheel scrolls",
        "Click to select  wheel scrolls"
    };
    uint32_t i;

    if (!out || out_cap == 0u || max_width <= 0.0f) {
        return 0u;
    }

    for (i = 0u; i < (uint32_t)(sizeof(k_candidates) / sizeof(k_candidates[0])); i++) {
        float width = 0.0f;
        const char *candidate = k_candidates[i];
        uint32_t len = (uint32_t)sap_wit_rt_strlen(candidate);

        if (croft_json_viewer_gpu_measure_text(config,
                                               surface,
                                               (const uint8_t *)candidate,
                                               len,
                                               font_size,
                                               font_role,
                                               &width) == ERR_OK
                && width <= max_width) {
            return croft_json_viewer_copy_text(out, out_cap, candidate);
        }
    }

    return croft_json_viewer_fit_text(config,
                                      surface,
                                      "Click to select  wheel scrolls",
                                      font_size,
                                      font_role,
                                      max_width,
                                      out,
                                      out_cap);
}

static int croft_json_viewer_sidebar_index_at_point(const CroftJsonViewerWindowAppState *state,
                                                    const CroftJsonViewerWindowLayout *layout,
                                                    float x,
                                                    float y,
                                                    uint32_t *index_out)
{
    uint32_t index;

    if (!state || !layout || !index_out) {
        return 0;
    }
    if (x < layout->sidebar_x + 8.0f || x > layout->sidebar_x + layout->sidebar_w - 8.0f) {
        return 0;
    }
    if (y < layout->sidebar_rows_y || y > layout->sidebar_y + layout->sidebar_h - 8.0f) {
        return 0;
    }
    index = (uint32_t)((y - layout->sidebar_rows_y) / layout->sidebar_row_h);
    if (index >= state->viewer.path_count) {
        return 0;
    }
    *index_out = index;
    return 1;
}

static int croft_json_viewer_handle_event(CroftJsonViewerWindowAppState *state,
                                          const SapWitHostWindowEvent *event,
                                          const CroftJsonViewerWindowLayout *layout)
{
    float content_viewport_height;

    if (!state || !event || !layout) {
        return 0;
    }

    content_viewport_height = layout->content_bottom - layout->content_text_y;
    switch (event->case_tag) {
        case SAP_WIT_HOST_WINDOW_EVENT_KEY:
            return croft_json_viewer_state_handle_key(&state->viewer,
                                                      event->val.key.key,
                                                      event->val.key.action,
                                                      22.0f,
                                                      content_viewport_height);
        case SAP_WIT_HOST_WINDOW_EVENT_SCROLL:
            croft_json_viewer_state_scroll_milli(&state->viewer,
                                                 event->val.scroll.y_milli,
                                                 22.0f,
                                                 content_viewport_height);
            return 1;
        case SAP_WIT_HOST_WINDOW_EVENT_CURSOR:
            state->cursor_x = (float)event->val.cursor.x_milli / 1000.0f;
            state->cursor_y = (float)event->val.cursor.y_milli / 1000.0f;
            return 1;
        case SAP_WIT_HOST_WINDOW_EVENT_MOUSE:
        {
            uint32_t line_index = 0u;

            if (event->val.mouse.action != CROFT_JSON_VIEWER_MOUSE_PRESS
                    || event->val.mouse.button != CROFT_JSON_VIEWER_MOUSE_LEFT) {
                return 0;
            }

            {
                uint32_t index = 0u;

                if (croft_json_viewer_sidebar_index_at_point(state,
                                                             layout,
                                                             state->cursor_x,
                                                             state->cursor_y,
                                                             &index)) {
                    if (croft_json_viewer_state_select_path_index(&state->viewer, index) != ERR_OK) {
                        return 0;
                    }
                    if (state->cursor_x <= layout->sidebar_x + 50.0f
                            && croft_json_viewer_state_path_is_expandable(&state->viewer, index)) {
                        return croft_json_viewer_state_toggle_path_index(&state->viewer, index) == ERR_OK;
                    }
                    return 1;
                }
            }

            if (state->cursor_x >= layout->content_x
                    && state->cursor_x <= layout->content_x + layout->content_w
                    && state->cursor_y >= layout->content_text_y
                    && state->cursor_y <= layout->content_bottom
                    && croft_json_viewer_state_content_line_index(&state->viewer,
                                                                  state->cursor_y - layout->content_text_y,
                                                                  22.0f,
                                                                  &line_index) == ERR_OK) {
                const char *line = NULL;
                uint32_t line_len = 0u;
                uint32_t path_index = 0u;
                uint32_t leading_spaces = 0u;
                float toggle_limit_x = layout->content_text_x + 20.0f;

                if (croft_json_viewer_state_select_line(&state->viewer, line_index) != ERR_OK) {
                    return 0;
                }
                if (croft_json_viewer_state_line(&state->viewer, line_index, &line, &line_len) == ERR_OK) {
                    while (leading_spaces < line_len && line[leading_spaces] == ' ') {
                        leading_spaces++;
                    }
                    toggle_limit_x += (float)leading_spaces * 4.0f;
                }
                if (state->cursor_x <= toggle_limit_x
                        && croft_json_viewer_state_line_path_index(&state->viewer,
                                                                   line_index,
                                                                   &path_index) == ERR_OK
                        && croft_json_viewer_state_path_is_expandable(&state->viewer, path_index)) {
                    return croft_json_viewer_state_toggle_path_index(&state->viewer, path_index) == ERR_OK;
                }
                return 1;
            }
            return 0;
        }
        default:
            return 0;
    }
}

static int croft_json_viewer_render_frame(CroftJsonViewerWindowAppState *state,
                                          const CroftJsonViewerWindowAppConfig *config,
                                          SapWitHostGpu2dSurfaceResource surface,
                                          uint32_t width,
                                          uint32_t height)
{
    CroftJsonViewerWindowLayout layout;
    char content_badge[128];
    char footer_help[96];
    char footer_selected[128];
    const char *selected_path;
    const char *selected_display;
    const char *selected_label = "Selected:";
    uint32_t content_badge_len = 0u;
    uint32_t footer_help_len = 0u;
    uint32_t footer_selected_len = 0u;
    uint32_t i;
    float content_badge_width = 0.0f;
    float content_badge_max_width;
    float footer_font = 15.0f;
    float footer_help_x = 32.0f;
    float footer_help_width = 0.0f;
    float footer_help_max_width;
    float footer_label_width = 0.0f;
    float footer_selected_width = 0.0f;
    float footer_selected_x;
    float line_height = 22.0f;
    float content_viewport_height;
    float max_scroll;
    uint8_t mono_font_role = SAP_WIT_HOST_GPU2D_FONT_ROLE_MONOSPACE;
    uint8_t ui_font_role = SAP_WIT_HOST_GPU2D_FONT_ROLE_UI;
    uint32_t selected_line = 0u;
    int rc;

    croft_json_viewer_layout_init(&layout, width, height);
    selected_path = croft_json_viewer_state_selected_path(&state->viewer);
    selected_display = croft_json_viewer_display_path(selected_path);
    content_viewport_height = layout.content_bottom - layout.content_text_y;
    croft_json_viewer_state_clamp_scroll(&state->viewer, line_height, content_viewport_height);
    max_scroll = croft_json_viewer_state_max_scroll(&state->viewer,
                                                    line_height,
                                                    content_viewport_height);

    content_badge_max_width = layout.content_w - 40.0f;
    content_badge_len = croft_json_viewer_fit_text(config,
                                                   surface,
                                                   selected_display,
                                                   14.0f,
                                                   ui_font_role,
                                                   content_badge_max_width - 16.0f,
                                                   content_badge,
                                                   sizeof(content_badge));
    if (content_badge_len == 0u) {
        content_badge_len = croft_json_viewer_copy_text(content_badge,
                                                        sizeof(content_badge),
                                                        selected_display);
    }
    if (content_badge_len > 0u) {
        rc = croft_json_viewer_gpu_measure_text(config,
                                                surface,
                                                (const uint8_t *)content_badge,
                                                content_badge_len,
                                                14.0f,
                                                ui_font_role,
                                                &content_badge_width);
        if (rc != ERR_OK) {
            return rc;
        }
    }
    if (content_badge_width < 80.0f) {
        content_badge_width = 80.0f;
    }
    if (content_badge_width > content_badge_max_width - 16.0f) {
        content_badge_width = content_badge_max_width - 16.0f;
    }

    rc = croft_json_viewer_gpu_measure_text(config,
                                            surface,
                                            (const uint8_t *)selected_label,
                                            9u,
                                            footer_font,
                                            ui_font_role,
                                            &footer_label_width);
    if (rc != ERR_OK) {
        return rc;
    }
    footer_selected_len = croft_json_viewer_fit_text(config,
                                                     surface,
                                                     selected_display,
                                                     footer_font,
                                                     ui_font_role,
                                                     (float)width * 0.24f,
                                                     footer_selected,
                                                     sizeof(footer_selected));
    if (footer_selected_len > 0u) {
        rc = croft_json_viewer_gpu_measure_text(config,
                                                surface,
                                                (const uint8_t *)footer_selected,
                                                footer_selected_len,
                                                footer_font,
                                                ui_font_role,
                                                &footer_selected_width);
        if (rc != ERR_OK) {
            return rc;
        }
    }
    footer_selected_x = (float)width - 28.0f - footer_selected_width - footer_label_width;
    if (footer_selected_len > 0u) {
        footer_selected_x -= 8.0f;
    }
    footer_help_max_width = footer_selected_x - footer_help_x - 24.0f;
    footer_help_len = croft_json_viewer_pick_footer_help(config,
                                                         surface,
                                                         footer_font,
                                                         ui_font_role,
                                                         footer_help_max_width,
                                                         footer_help,
                                                         sizeof(footer_help));
    if (footer_help_len > 0u) {
        rc = croft_json_viewer_gpu_measure_text(config,
                                                surface,
                                                (const uint8_t *)footer_help,
                                                footer_help_len,
                                                footer_font,
                                                ui_font_role,
                                                &footer_help_width);
        if (rc != ERR_OK) {
            return rc;
        }
    }
    if (footer_help_width > footer_help_max_width) {
        footer_help_len = 0u;
        footer_help_width = 0.0f;
    }
    if (footer_selected_x < footer_help_x + footer_help_width + 24.0f) {
        footer_selected_x = footer_help_x + footer_help_width + 24.0f;
    }

    rc = croft_json_viewer_gpu_draw_rect(config, surface, 0.0f, 0.0f, (float)width, 68.0f, 0x17324DFFu);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_json_viewer_gpu_draw_rect(config,
                                         surface,
                                         layout.sidebar_x,
                                         layout.sidebar_y,
                                         layout.sidebar_w,
                                         layout.sidebar_h,
                                         0xFFF8F0FFu);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_json_viewer_gpu_draw_rect(config,
                                         surface,
                                         layout.content_x,
                                         layout.content_y,
                                         layout.content_w,
                                         layout.content_h,
                                         0xFFFCF8FFu);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_json_viewer_gpu_draw_text(config,
                                         surface,
                                         28.0f,
                                         42.0f,
                                         config->title_data,
                                         config->title_len,
                                         30.0f,
                                         ui_font_role,
                                         0xF6EFE5FFu);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_json_viewer_gpu_draw_text(config,
                                         surface,
                                         32.0f,
                                         118.0f,
                                         (const uint8_t *)"Nodes",
                                         5u,
                                         20.0f,
                                         ui_font_role,
                                         0x17324DFFu);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_json_viewer_gpu_draw_text(config,
                                         surface,
                                         316.0f,
                                         118.0f,
                                         (const uint8_t *)"Rendered JSON",
                                         13u,
                                         20.0f,
                                         ui_font_role,
                                         0x17324DFFu);
    if (rc != ERR_OK) {
        return rc;
    }

    for (i = 0u; i < state->viewer.path_count; i++) {
        char label[CROFT_JSON_VIEWER_PATH_LABEL_CAP];
        float row_y = layout.sidebar_rows_y + (float)i * layout.sidebar_row_h;
        int selected = i == state->viewer.selected_path_index;
        int expanded = croft_json_viewer_state_path_is_expanded(&state->viewer, i);

        if (row_y + layout.sidebar_row_h > layout.sidebar_y + layout.sidebar_h - 8.0f) {
            break;
        }
        if (selected || expanded) {
            rc = croft_json_viewer_gpu_draw_rect(config,
                                                 surface,
                                                 layout.sidebar_x + 10.0f,
                                                 row_y - 16.0f,
                                                 layout.sidebar_w - 20.0f,
                                                 22.0f,
                                                 selected ? 0xD8EBF7FFu : 0xF1E4D3FFu);
            if (rc != ERR_OK) {
                return rc;
            }
        }
        if (selected) {
            rc = croft_json_viewer_gpu_draw_rect(config,
                                                 surface,
                                                 layout.sidebar_x + 10.0f,
                                                 row_y - 16.0f,
                                                 4.0f,
                                                 22.0f,
                                                 0x0F5B7AFFu);
            if (rc != ERR_OK) {
                return rc;
            }
        }
        if (croft_json_viewer_state_format_path_label(&state->viewer, i, label, sizeof(label)) != ERR_OK) {
            continue;
        }
        rc = croft_json_viewer_gpu_draw_text(config,
                                             surface,
                                             32.0f,
                                             row_y,
                                             (const uint8_t *)label,
                                             (uint32_t)sap_wit_rt_strlen(label),
                                             16.0f,
                                             ui_font_role,
                                             selected ? 0x0C4A63FFu : (expanded ? 0x35566DFFu : 0x52697DFFu));
        if (rc != ERR_OK) {
            return rc;
        }
    }

    rc = croft_json_viewer_gpu_draw_rect(config,
                                         surface,
                                         layout.content_x + 16.0f,
                                         128.0f,
                                         content_badge_width + 16.0f,
                                         24.0f,
                                         0xE8F2F7FFu);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_json_viewer_gpu_draw_text(config,
                                         surface,
                                         layout.content_x + 24.0f,
                                         145.0f,
                                         (const uint8_t *)content_badge,
                                         content_badge_len,
                                         14.0f,
                                         ui_font_role,
                                         0x0F5B7AFFu);
    if (rc != ERR_OK) {
        return rc;
    }

    {
        uint32_t first_line = (uint32_t)(state->viewer.scroll_y / line_height);
        float line_offset = state->viewer.scroll_y - (float)first_line * line_height;
        float y = layout.content_text_y - line_offset;
        int has_selected_line =
            croft_json_viewer_state_selected_line(&state->viewer, &selected_line) == ERR_OK;

        for (i = first_line;
                i < croft_json_viewer_state_line_count(&state->viewer) && y < layout.content_bottom;
                i++) {
            const char *line = NULL;
            uint32_t line_len = 0u;

                if (croft_json_viewer_state_line(&state->viewer, i, &line, &line_len) != ERR_OK) {
                    continue;
                }
                if (y >= layout.content_text_y) {
                    if (has_selected_line && i == selected_line) {
                        rc = croft_json_viewer_gpu_draw_rect(config,
                                                             surface,
                                                             layout.content_x + 10.0f,
                                                             y - 16.0f,
                                                             layout.content_w - 24.0f,
                                                             22.0f,
                                                             0xE8F2F7FFu);
                        if (rc != ERR_OK) {
                            return rc;
                        }
                        rc = croft_json_viewer_gpu_draw_rect(config,
                                                             surface,
                                                             layout.content_x + 10.0f,
                                                             y - 16.0f,
                                                             4.0f,
                                                             22.0f,
                                                             0x0F5B7AFFu);
                        if (rc != ERR_OK) {
                            return rc;
                        }
                    }
                    rc = croft_json_viewer_gpu_draw_text(config,
                                                         surface,
                                                         layout.content_text_x,
                                                         y,
                                                     (const uint8_t *)line,
                                                     line_len,
                                                     17.0f,
                                                     mono_font_role,
                                                     0x17324DFFu);
                if (rc != ERR_OK) {
                    return rc;
                }
            }
            y += line_height;
        }
    }

    if (max_scroll > 0.0f) {
        float track_x = layout.content_x + layout.content_w - 12.0f;
        float track_y = layout.content_text_y;
        float track_h = content_viewport_height;
        float content_h = (float)croft_json_viewer_state_line_count(&state->viewer) * line_height;
        float thumb_h = (track_h * track_h) / content_h;
        float thumb_y;

        if (thumb_h < 36.0f) {
            thumb_h = 36.0f;
        }
        if (thumb_h > track_h) {
            thumb_h = track_h;
        }
        thumb_y = track_y + (state->viewer.scroll_y / max_scroll) * (track_h - thumb_h);
        rc = croft_json_viewer_gpu_draw_rect(config, surface, track_x, track_y, 4.0f, track_h, 0xE1D6C8FFu);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_json_viewer_gpu_draw_rect(config, surface, track_x, thumb_y, 4.0f, thumb_h, 0x0F5B7AFFu);
        if (rc != ERR_OK) {
            return rc;
        }
    }

    if (footer_help_len > 0u) {
        rc = croft_json_viewer_gpu_draw_text(config,
                                             surface,
                                             footer_help_x,
                                             layout.footer_y,
                                             (const uint8_t *)footer_help,
                                             footer_help_len,
                                             footer_font,
                                             ui_font_role,
                                             0x52697DFFu);
        if (rc != ERR_OK) {
            return rc;
        }
    }
    rc = croft_json_viewer_gpu_draw_text(config,
                                         surface,
                                         footer_selected_x,
                                         layout.footer_y,
                                         (const uint8_t *)selected_label,
                                         9u,
                                         footer_font,
                                         ui_font_role,
                                         0x52697DFFu);
    if (rc != ERR_OK) {
        return rc;
    }
    if (footer_selected_len == 0u) {
        return ERR_OK;
    }
    return croft_json_viewer_gpu_draw_text(config,
                                           surface,
                                           footer_selected_x + footer_label_width + 8.0f,
                                           layout.footer_y,
                                           (const uint8_t *)footer_selected,
                                           footer_selected_len,
                                           footer_font,
                                           ui_font_role,
                                           0x0F5B7AFFu);
}

int croft_json_viewer_window_app_run(CroftJsonViewerWindowAppState *state,
                                     const CroftJsonViewerWindowAppConfig *config,
                                     const uint8_t *json,
                                     uint32_t json_len,
                                     uint32_t auto_close_ms,
                                     uint32_t *frame_count_out)
{
    SapWitHostWindowResource window = SAP_WIT_HOST_WINDOW_RESOURCE_INVALID;
    SapWitHostGpu2dSurfaceResource surface = SAP_WIT_HOST_GPU2D_SURFACE_RESOURCE_INVALID;
    SapWitHostWindowCommand window_cmd = {0};
    SapWitHostWindowReply window_reply = {0};
    SapWitHostGpu2dCommand gpu_cmd = {0};
    SapWitHostGpu2dReply gpu_reply = {0};
    SapWitHostClockCommand clock_cmd = {0};
    SapWitHostClockReply clock_reply = {0};
    uint32_t caps = 0u;
    uint64_t start_ms = 0u;
    uint32_t frame_count = 0u;
    int rc = ERR_OK;

    if (!state || !config || !config->window_dispatch || !config->gpu_dispatch
            || !config->clock_dispatch || (!json && json_len > 0u)
            || !config->title_data || config->title_len == 0u) {
        return ERR_INVALID;
    }

    state->cursor_x = 0.0f;
    state->cursor_y = 0.0f;
    rc = croft_json_viewer_state_load(&state->viewer, json, json_len);
    if (rc != ERR_OK) {
        return rc;
    }

    window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_OPEN;
    window_cmd.val.open.width = 1080u;
    window_cmd.val.open.height = 720u;
    window_cmd.val.open.title_data = config->title_data;
    window_cmd.val.open.title_len = config->title_len;
    rc = config->window_dispatch(config->dispatch_ctx, &window_cmd, &window_reply);
    if (rc != ERR_OK || !expect_window_ok(&window_reply, &window)) {
        if (rc == ERR_OK) {
            sap_wit_dispose_host_window_reply(&window_reply);
            rc = ERR_TYPE;
        }
        return rc;
    }
    sap_wit_dispose_host_window_reply(&window_reply);

    gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_CAPABILITIES;
    rc = config->gpu_dispatch(config->dispatch_ctx, &gpu_cmd, &gpu_reply);
    if (rc != ERR_OK || !expect_gpu_caps_ok(&gpu_reply, &caps)) {
        if (rc == ERR_OK) {
            sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
            rc = ERR_TYPE;
        }
        goto cleanup;
    }
    sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
    if ((caps & SAP_WIT_HOST_GPU2D_CAPABILITIES_TEXT) == 0u
            || (caps & SAP_WIT_HOST_GPU2D_CAPABILITIES_PRESENT) == 0u) {
        rc = ERR_TYPE;
        goto cleanup;
    }

    gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_OPEN;
    rc = config->gpu_dispatch(config->dispatch_ctx, &gpu_cmd, &gpu_reply);
    if (rc != ERR_OK || !expect_surface_ok(&gpu_reply, &surface)) {
        if (rc == ERR_OK) {
            sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
            rc = ERR_TYPE;
        }
        goto cleanup;
    }
    sap_wit_dispose_host_gpu2d_reply(&gpu_reply);

    clock_cmd.case_tag = SAP_WIT_HOST_CLOCK_COMMAND_MONOTONIC_NOW;
    rc = config->clock_dispatch(config->dispatch_ctx, &clock_cmd, &clock_reply);
    if (rc != ERR_OK || !expect_clock_now(&clock_reply, &start_ms)) {
        if (rc == ERR_OK) {
            sap_wit_dispose_host_clock_reply(&clock_reply);
            rc = ERR_TYPE;
        }
        goto cleanup;
    }
    sap_wit_dispose_host_clock_reply(&clock_reply);

    for (;;) {
        CroftJsonViewerWindowLayout layout;
        uint64_t now_ms = 0u;
        uint8_t should_close = 0u;
        uint32_t width = 0u;
        uint32_t height = 0u;

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_POLL;
        window_cmd.val.poll.window = window;
        rc = config->window_dispatch(config->dispatch_ctx, &window_cmd, &window_reply);
        if (rc != ERR_OK || !expect_window_status_ok(&window_reply)) {
            if (rc == ERR_OK) {
                sap_wit_dispose_host_window_reply(&window_reply);
                rc = ERR_TYPE;
            }
            goto cleanup;
        }
        sap_wit_dispose_host_window_reply(&window_reply);

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_FRAMEBUFFER_SIZE;
        window_cmd.val.framebuffer_size.window = window;
        rc = config->window_dispatch(config->dispatch_ctx, &window_cmd, &window_reply);
        if (rc != ERR_OK || !expect_window_size(&window_reply, &width, &height)) {
            if (rc == ERR_OK) {
                sap_wit_dispose_host_window_reply(&window_reply);
                rc = ERR_TYPE;
            }
            goto cleanup;
        }
        sap_wit_dispose_host_window_reply(&window_reply);
        croft_json_viewer_layout_init(&layout, width, height);

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_NEXT_EVENT;
        window_cmd.val.next_event.window = window;
        for (;;) {
            SapWitHostWindowEvent event = {0};
            int event_status;

            rc = config->window_dispatch(config->dispatch_ctx, &window_cmd, &window_reply);
            if (rc != ERR_OK) {
                goto cleanup;
            }
            event_status = expect_window_event(&window_reply, &event);
            sap_wit_dispose_host_window_reply(&window_reply);
            if (event_status < 0) {
                rc = ERR_TYPE;
                goto cleanup;
            }
            if (event_status == 0) {
                break;
            }
            (void)croft_json_viewer_handle_event(state, &event, &layout);
        }

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_SHOULD_CLOSE;
        window_cmd.val.should_close.window = window;
        rc = config->window_dispatch(config->dispatch_ctx, &window_cmd, &window_reply);
        if (rc != ERR_OK || !expect_window_bool(&window_reply, &should_close)) {
            if (rc == ERR_OK) {
                sap_wit_dispose_host_window_reply(&window_reply);
                rc = ERR_TYPE;
            }
            goto cleanup;
        }
        sap_wit_dispose_host_window_reply(&window_reply);
        if (should_close) {
            break;
        }

        rc = config->clock_dispatch(config->dispatch_ctx, &clock_cmd, &clock_reply);
        if (rc != ERR_OK || !expect_clock_now(&clock_reply, &now_ms)) {
            if (rc == ERR_OK) {
                sap_wit_dispose_host_clock_reply(&clock_reply);
                rc = ERR_TYPE;
            }
            goto cleanup;
        }
        sap_wit_dispose_host_clock_reply(&clock_reply);
        if (now_ms - start_ms >= (uint64_t)auto_close_ms) {
            break;
        }

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_BEGIN_FRAME;
        gpu_cmd.val.begin_frame.surface = surface;
        gpu_cmd.val.begin_frame.width = width;
        gpu_cmd.val.begin_frame.height = height;
        rc = config->gpu_dispatch(config->dispatch_ctx, &gpu_cmd, &gpu_reply);
        if (rc != ERR_OK || !expect_gpu_status_ok(&gpu_reply)) {
            if (rc == ERR_OK) {
                sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
                rc = ERR_TYPE;
            }
            goto cleanup;
        }
        sap_wit_dispose_host_gpu2d_reply(&gpu_reply);

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_CLEAR;
        gpu_cmd.val.clear.surface = surface;
        gpu_cmd.val.clear.color_rgba = 0xF6EFE5FFu;
        rc = config->gpu_dispatch(config->dispatch_ctx, &gpu_cmd, &gpu_reply);
        if (rc != ERR_OK || !expect_gpu_status_ok(&gpu_reply)) {
            if (rc == ERR_OK) {
                sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
                rc = ERR_TYPE;
            }
            goto cleanup;
        }
        sap_wit_dispose_host_gpu2d_reply(&gpu_reply);

        rc = croft_json_viewer_render_frame(state, config, surface, width, height);
        if (rc != ERR_OK) {
            goto cleanup;
        }

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_END_FRAME;
        gpu_cmd.val.end_frame.surface = surface;
        rc = config->gpu_dispatch(config->dispatch_ctx, &gpu_cmd, &gpu_reply);
        if (rc != ERR_OK || !expect_gpu_status_ok(&gpu_reply)) {
            if (rc == ERR_OK) {
                sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
                rc = ERR_TYPE;
            }
            goto cleanup;
        }
        sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
        frame_count++;
    }

    if (frame_count_out) {
        *frame_count_out = frame_count;
    }

cleanup:
    if (surface != SAP_WIT_HOST_GPU2D_SURFACE_RESOURCE_INVALID) {
        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_DROP;
        gpu_cmd.val.drop.surface = surface;
        if (config->gpu_dispatch(config->dispatch_ctx, &gpu_cmd, &gpu_reply) == ERR_OK) {
            sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
        }
    }
    if (window != SAP_WIT_HOST_WINDOW_RESOURCE_INVALID) {
        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_CLOSE;
        window_cmd.val.close.window = window;
        if (config->window_dispatch(config->dispatch_ctx, &window_cmd, &window_reply) == ERR_OK) {
            sap_wit_dispose_host_window_reply(&window_reply);
        }
    }
    return rc;
}
