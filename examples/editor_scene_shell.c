#include "croft/editor_document.h"
#include "croft/editor_document_fs.h"
#include "croft/host_gesture.h"
#include "croft/host_render.h"
#include "croft/host_ui.h"
#include "croft/scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_running = 1;
static viewport_node g_root_vp;
static text_editor_node g_editor;
static scene_node* g_focused_node = NULL;
static double g_mouse_x = 0;
static double g_mouse_y = 0;
static croft_editor_document* g_document = NULL;

enum {
    CROFT_EDITOR_WINDOW_PADDING = 16
};

static int env_flag_enabled(const char* value) {
    return value && value[0] != '\0' && !(value[0] == '0' && value[1] == '\0');
}

static double usec_to_msec(uint64_t usec) {
    return (double)usec / 1000.0;
}

static void print_editor_profile_summary(const char* variant, const text_editor_node* editor) {
    croft_text_editor_profile_snapshot profile = {0};

    text_editor_node_get_profile(editor, &profile);
    if (!profile.enabled) {
        return;
    }

    printf("editor-scene-profile variant=%s kind=core draw_calls=%llu draw_ms=%.3f layout_calls=%llu layout_ms=%.3f ensure_cursor_calls=%llu ensure_cursor_ms=%.3f hit_calls=%llu hit_ms=%.3f hit_offsets=%llu\n",
           variant,
           (unsigned long long)profile.draw_calls,
           usec_to_msec(profile.draw_total_usec),
           (unsigned long long)profile.layout_calls,
           usec_to_msec(profile.layout_total_usec),
           (unsigned long long)profile.ensure_cursor_visible_calls,
           usec_to_msec(profile.ensure_cursor_visible_total_usec),
           (unsigned long long)profile.hit_index_calls,
           usec_to_msec(profile.hit_index_total_usec),
           (unsigned long long)profile.hit_index_offsets_scanned);
    printf("editor-scene-profile variant=%s kind=line-map visible_count_calls=%llu visible_count_ms=%.3f visible_count_steps=%llu visible_lookup_calls=%llu visible_lookup_ms=%.3f visible_lookup_steps=%llu model_lookup_calls=%llu model_lookup_ms=%.3f model_lookup_steps=%llu\n",
           variant,
           (unsigned long long)profile.visible_line_count_calls,
           usec_to_msec(profile.visible_line_count_total_usec),
           (unsigned long long)profile.visible_line_count_steps,
           (unsigned long long)profile.visible_line_lookup_calls,
           usec_to_msec(profile.visible_line_lookup_total_usec),
           (unsigned long long)profile.visible_line_lookup_steps,
           (unsigned long long)profile.model_line_lookup_calls,
           usec_to_msec(profile.model_line_lookup_total_usec),
           (unsigned long long)profile.model_line_lookup_steps);
    printf("editor-scene-profile variant=%s kind=text measure_calls=%llu measure_bytes=%llu measure_ms=%.3f background_lines=%llu text_lines=%llu gutter_lines=%llu search_calls=%llu search_ms=%.3f bracket_calls=%llu bracket_ms=%.3f\n",
           variant,
           (unsigned long long)profile.measure_text_calls,
           (unsigned long long)profile.measure_text_total_bytes,
           usec_to_msec(profile.measure_text_total_usec),
           (unsigned long long)profile.background_pass_lines,
           (unsigned long long)profile.text_pass_lines,
           (unsigned long long)profile.gutter_pass_lines,
           (unsigned long long)profile.search_draw_calls,
           usec_to_msec(profile.search_draw_total_usec),
           (unsigned long long)profile.bracket_draw_calls,
           usec_to_msec(profile.bracket_draw_total_usec));
}

static void on_ui_event(int32_t type, int32_t arg0, int32_t arg1) {
    if (type == CROFT_UI_EVENT_KEY) {
        uint32_t modifiers = host_ui_get_modifiers();
        text_editor_node_set_modifiers(&g_editor, modifiers);
        if (text_editor_node_is_find_active(&g_editor)) {
            if (g_focused_node && g_focused_node->vtbl && g_focused_node->vtbl->on_key_event) {
                g_focused_node->vtbl->on_key_event(g_focused_node, arg0, arg1);
            }
            return;
        }
        if (arg0 == 256 && arg1 == 1) {
            g_running = 0;
            return;
        }
        if (arg0 == 81 && arg1 == 1
                && (modifiers & (CROFT_UI_MOD_SUPER | CROFT_UI_MOD_CONTROL)) != 0u) {
            g_running = 0;
            return;
        }
        if (g_focused_node && g_focused_node->vtbl && g_focused_node->vtbl->on_key_event) {
            g_focused_node->vtbl->on_key_event(g_focused_node, arg0, arg1);
        }
    } else if (type == CROFT_UI_EVENT_CHAR) {
        text_editor_node_set_modifiers(&g_editor, host_ui_get_modifiers());
        if (g_focused_node && g_focused_node->vtbl && g_focused_node->vtbl->on_char_event) {
            g_focused_node->vtbl->on_char_event(g_focused_node, (uint32_t)arg0);
        }
    } else if (type == CROFT_UI_EVENT_SCROLL) {
        float dy = (float)arg1 / 1000.0f;
        float dx = (float)arg0 / 1000.0f;
        g_editor.scroll_y += dy * 20.0f;
        g_editor.scroll_x += dx * 20.0f;
        if (g_editor.scroll_y > 0.0f) {
            g_editor.scroll_y = 0.0f;
        }
        if (g_editor.scroll_x > 0.0f) {
            g_editor.scroll_x = 0.0f;
        }
    } else if (type == CROFT_UI_EVENT_ZOOM_GESTURE) {
        float delta = (float)arg0 / 1000000.0f;
        float old_scale = g_root_vp.scale;
        float new_scale = old_scale * (1.0f + delta);
        uint32_t fw = 0;
        uint32_t fh = 0;
        float cx;
        float cy;
        float scale_ratio;

        if (new_scale < 0.1f) {
            new_scale = 0.1f;
        }
        if (new_scale > 10.0f) {
            new_scale = 10.0f;
        }

        host_ui_get_framebuffer_size(&fw, &fh);
        cx = (float)fw / 2.0f;
        cy = (float)fh / 2.0f;
        scale_ratio = new_scale / old_scale;

        g_root_vp.scroll_x = cx - (cx - g_root_vp.scroll_x) * scale_ratio;
        g_root_vp.scroll_y = cy - (cy - g_root_vp.scroll_y) * scale_ratio;
        g_root_vp.scale = new_scale;
    } else if (type == CROFT_UI_EVENT_MOUSE) {
        if (arg1 == 1) {
            hit_result hit;
            host_ui_get_mouse_pos(&g_mouse_x, &g_mouse_y);
            scene_node_hit_test_tree(&g_root_vp.base, (float)g_mouse_x, (float)g_mouse_y, &hit);
            if (hit.node) {
                g_focused_node = hit.node;
                if (hit.node->vtbl && hit.node->vtbl->on_mouse_event) {
                    hit.node->vtbl->on_mouse_event(hit.node, 1, hit.local_x, hit.local_y);
                }
            }
        } else if (arg1 == 0) {
            if (g_focused_node && g_focused_node->vtbl && g_focused_node->vtbl->on_mouse_event) {
                g_focused_node->vtbl->on_mouse_event(g_focused_node, 0, 0, 0);
            }
        }
    } else if (type == CROFT_UI_EVENT_CURSOR_POS) {
        if (g_focused_node && g_focused_node->vtbl && g_focused_node->vtbl->on_mouse_event) {
            float lx;
            float ly;

            host_ui_get_mouse_pos(&g_mouse_x, &g_mouse_y);
            lx = (float)g_mouse_x;
            ly = (float)g_mouse_y;
            if (g_root_vp.base.vtbl && g_root_vp.base.vtbl->transform_coords) {
                g_root_vp.base.vtbl->transform_coords(&g_root_vp.base, &lx, &ly);
            }
            lx -= g_editor.base.x;
            ly -= g_editor.base.y;
            g_focused_node->vtbl->on_mouse_event(g_focused_node, 3, lx, ly);
        }
    }
}

int main(int argc, char** argv) {
    const char* target_file = argc > 1 ? argv[1] : NULL;
    const char* auto_close_env = getenv("CROFT_EDITOR_AUTO_CLOSE_MS");
    const char* profile_env = getenv("CROFT_EDITOR_PROFILE");
    const char* fallback =
        "Big analysis, small binaries.\n"
        "\n"
        "This scene-based editor reuses the shared Sapling document layer.\n";
    double start_time = 0.0;
    double end_time = 0.0;
    uint32_t frame_count = 0u;
    int profile_enabled = env_flag_enabled(profile_env);

    g_document = croft_editor_document_open(argc > 0 ? argv[0] : NULL,
                                            target_file,
                                            (const uint8_t*)fallback,
                                            strlen(fallback));
    if (!g_document) {
        return 1;
    }

    if (host_ui_init() != 0) {
        croft_editor_document_destroy(g_document);
        return 1;
    }
    if (host_ui_create_window(1000, 800, "Croft Text Editor Scene Shell") != 0) {
        host_ui_terminate();
        croft_editor_document_destroy(g_document);
        return 1;
    }
    if (host_render_init() != 0) {
        host_ui_terminate();
        croft_editor_document_destroy(g_document);
        return 1;
    }

#ifdef __APPLE__
    host_gesture_mac_init(host_ui_get_native_window(), (void*)on_ui_event);
#endif
    host_ui_set_event_callback(on_ui_event);

    {
        uint32_t fw = 0;
        uint32_t fh = 0;
        host_ui_get_framebuffer_size(&fw, &fh);
        viewport_node_init(&g_root_vp, 0, 0, (float)fw, (float)fh);
        text_editor_node_init(&g_editor,
                              croft_editor_document_env(g_document),
                              (float)CROFT_EDITOR_WINDOW_PADDING,
                              (float)CROFT_EDITOR_WINDOW_PADDING,
                              (float)fw - (float)(CROFT_EDITOR_WINDOW_PADDING * 2),
                              (float)fh - (float)(CROFT_EDITOR_WINDOW_PADDING * 2),
                              croft_editor_document_text(g_document));
        text_editor_node_bind_document(&g_editor, g_document);
        text_editor_node_set_profiling(&g_editor, profile_enabled);
        scene_node_add_child(&g_root_vp.base, &g_editor.base);
    }

    start_time = host_ui_get_time();
    while (g_running && !host_ui_should_close()) {
        uint32_t fw = 0;
        uint32_t fh = 0;

        if (auto_close_env && auto_close_env[0] != '\0') {
            int auto_close_ms = atoi(auto_close_env);
            if (auto_close_ms > 0 && ((host_ui_get_time() - start_time) * 1000.0) >= (double)auto_close_ms) {
                break;
            }
        }

        host_ui_poll_events();
        host_ui_get_framebuffer_size(&fw, &fh);
        g_root_vp.base.sx = (float)fw;
        g_root_vp.base.sy = (float)fh;
        g_editor.base.sx = (float)fw - (float)(CROFT_EDITOR_WINDOW_PADDING * 2);
        g_editor.base.sy = (float)fh - (float)(CROFT_EDITOR_WINDOW_PADDING * 2);

        if (host_render_begin_frame(fw, fh) == 0) {
            render_ctx rc;
            host_render_clear(0xF3F4F6FF);

            rc.fg_color = 0x111111FF;
            rc.bg_color = 0xFAFBFCFF;
            rc.time = host_ui_get_time();
            scene_node_draw_tree(&g_root_vp.base, &rc);

            host_render_end_frame();
            host_ui_swap_buffers();
            frame_count++;
        }
    }

    end_time = host_ui_get_time();
    printf("editor-scene frames=%u wall_ms=%llu\n",
           frame_count,
           (unsigned long long)((end_time - start_time) * 1000.0));
    print_editor_profile_summary("scene", &g_editor);
    fflush(stdout);

    host_render_terminate();
    host_ui_terminate();
    text_editor_node_dispose(&g_editor);
    croft_editor_document_destroy(g_document);
    return 0;
}
