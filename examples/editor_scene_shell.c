#include "croft/editor_document.h"
#include "croft/editor_document_fs.h"
#include "croft/editor_menu_ids.h"
#include "croft/editor_scene_runtime.h"
#include "croft/host_file_dialog.h"
#include "croft/host_popup_menu.h"
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
static croft_editor_scene_runtime_state g_runtime;

enum {
    CROFT_EDITOR_WINDOW_PADDING = 16
};

typedef struct render_frame_profile {
    uint64_t begin_frame_total_usec;
    uint64_t draw_tree_total_usec;
    uint64_t end_frame_total_usec;
    uint64_t present_total_usec;
} render_frame_profile;

static int env_flag_enabled(const char* value) {
    return value && value[0] != '\0' && !(value[0] == '0' && value[1] == '\0');
}

static double usec_to_msec(uint64_t usec) {
    return (double)usec / 1000.0;
}

static uint64_t monotonic_usec_now(void) {
    double now = host_ui_get_time();
    if (now <= 0.0) {
        return 0u;
    }
    return (uint64_t)(now * 1000000.0);
}

static void request_redraw(void) {
    croft_editor_scene_runtime_request_redraw(&g_runtime);
}

static void bind_document_to_editor(croft_editor_document* document) {
    g_document = document;
    text_editor_node_bind_document(&g_editor, g_document);
    g_editor.scroll_x = 0.0f;
    g_editor.scroll_y = 0.0f;
    g_editor.sel_start = 0u;
    g_editor.sel_end = 0u;
    g_editor.preferred_column = 1u;
    g_editor.selection = croft_editor_selection_create(croft_editor_position_create(1u, 1u),
                                                       croft_editor_position_create(1u, 1u));
    text_editor_node_find_close(&g_editor);
}

static int open_document_via_dialog(void) {
    char* path = host_file_dialog_open_path();
    croft_editor_document* document;

    if (!path) {
        return 1;
    }

    document = croft_editor_document_open(NULL, path, NULL, 0u);
    if (!document) {
        fprintf(stderr, "example_editor_text: failed to open %s\n", path);
        host_file_dialog_free_path(path);
        return 0;
    }

    host_file_dialog_free_path(path);
    croft_editor_document_destroy(g_document);
    bind_document_to_editor(document);
    request_redraw();
    return 1;
}

static int save_document_via_dialog(int force_save_as) {
    int32_t rc;

    if (!force_save_as && croft_editor_document_path(g_document)) {
        rc = croft_editor_document_save(g_document);
    } else {
        char* path = host_file_dialog_save_path(croft_editor_document_path(g_document));
        if (!path) {
            return 1;
        }
        rc = croft_editor_document_save_as(g_document, path);
        host_file_dialog_free_path(path);
    }

    if (rc != 0) {
        fprintf(stderr, "example_editor_text: save failed (%d)\n", rc);
        return 0;
    }

    request_redraw();
    return 1;
}

static int set_clipboard_from_selection(void) {
    char* utf8 = NULL;
    size_t utf8_len = 0u;
    int ok;

    if (text_editor_node_copy_selection_utf8(&g_editor, &utf8, &utf8_len) != 0) {
        return 0;
    }
    ok = host_ui_set_clipboard_text(utf8 ? utf8 : "", utf8_len) == 0;
    free(utf8);
    return ok;
}

static int paste_from_clipboard(void) {
    char* utf8 = NULL;
    size_t utf8_len = 0u;
    int ok;

    if (host_ui_get_clipboard_text(&utf8, &utf8_len) != 0) {
        return 0;
    }
    if (!utf8 || utf8_len == 0u) {
        free(utf8);
        return 1;
    }
    ok = text_editor_node_replace_selection_utf8(&g_editor, (const uint8_t*)utf8, utf8_len) == 0;
    free(utf8);
    return ok;
}

static int editor_apply_menu_action(int32_t action_id) {
    switch (action_id) {
        case CROFT_EDITOR_MENU_OPEN:
            return open_document_via_dialog();
        case CROFT_EDITOR_MENU_SAVE:
            return save_document_via_dialog(0);
        case CROFT_EDITOR_MENU_SAVE_AS:
            return save_document_via_dialog(1);
        case CROFT_EDITOR_MENU_QUIT:
            g_running = 0;
            return 1;
        case CROFT_EDITOR_MENU_UNDO:
            return text_editor_node_undo(&g_editor) == 0;
        case CROFT_EDITOR_MENU_REDO:
            return text_editor_node_redo(&g_editor) == 0;
        case CROFT_EDITOR_MENU_SELECT_ALL:
            text_editor_node_select_all(&g_editor);
            return 1;
        case CROFT_EDITOR_MENU_COPY:
            return set_clipboard_from_selection();
        case CROFT_EDITOR_MENU_CUT:
            if (!set_clipboard_from_selection()) {
                return 0;
            }
            return text_editor_node_delete_selection(&g_editor, 1) == 0;
        case CROFT_EDITOR_MENU_PASTE:
            return paste_from_clipboard();
        case CROFT_EDITOR_MENU_FIND:
            text_editor_node_find_activate(&g_editor);
            return 1;
        case CROFT_EDITOR_MENU_FIND_NEXT:
            return text_editor_node_find_next(&g_editor) == 0;
        case CROFT_EDITOR_MENU_FIND_PREVIOUS:
            return text_editor_node_find_previous(&g_editor) == 0;
        case CROFT_EDITOR_MENU_INDENT:
            return text_editor_node_indent(&g_editor) == 0;
        case CROFT_EDITOR_MENU_OUTDENT:
            return text_editor_node_outdent(&g_editor) == 0;
        case CROFT_EDITOR_MENU_FOLD:
            return text_editor_node_fold(&g_editor) == 0;
        case CROFT_EDITOR_MENU_UNFOLD:
            return text_editor_node_unfold(&g_editor) == 0;
        default:
            return 1;
    }
}

static void show_editor_context_menu(float x, float y) {
    int32_t action_id = 0;
    host_popup_menu_item items[] = {
        { CROFT_EDITOR_MENU_OPEN, "Open...", 1u, 0u },
        { CROFT_EDITOR_MENU_SAVE, "Save", 1u, 0u },
        { CROFT_EDITOR_MENU_SAVE_AS, "Save As...", 1u, 0u },
        { 0, NULL, 0u, 1u },
        { CROFT_EDITOR_MENU_UNDO, "Undo", 1u, 0u },
        { CROFT_EDITOR_MENU_REDO, "Redo", 1u, 0u },
        { 0, NULL, 0u, 1u },
        { CROFT_EDITOR_MENU_CUT, "Cut", 1u, 0u },
        { CROFT_EDITOR_MENU_COPY, "Copy", 1u, 0u },
        { CROFT_EDITOR_MENU_PASTE, "Paste", 1u, 0u },
        { CROFT_EDITOR_MENU_SELECT_ALL, "Select All", 1u, 0u },
        { 0, NULL, 0u, 1u },
        { CROFT_EDITOR_MENU_FIND, "Find...", 1u, 0u },
        { CROFT_EDITOR_MENU_FIND_NEXT, "Find Next", 1u, 0u },
        { CROFT_EDITOR_MENU_FIND_PREVIOUS, "Find Previous", 1u, 0u },
        { 0, NULL, 0u, 1u },
        { CROFT_EDITOR_MENU_INDENT, "Indent Line", 1u, 0u },
        { CROFT_EDITOR_MENU_OUTDENT, "Outdent Line", 1u, 0u },
        { CROFT_EDITOR_MENU_FOLD, "Fold Region", 1u, 0u },
        { CROFT_EDITOR_MENU_UNFOLD, "Unfold Region", 1u, 0u }
    };
    if (host_popup_menu_show(items,
                             (uint32_t)(sizeof(items) / sizeof(items[0])),
                             x,
                             y,
                             &action_id) == HOST_POPUP_MENU_RESULT_OK
            && !editor_apply_menu_action(action_id)) {
        g_running = 0;
    }
    if (action_id != 0) {
        request_redraw();
    }
}

static void print_font_probe_summary(const char* variant,
                                     const char* backend,
                                     float editor_line_height,
                                     float font_size) {
    croft_editor_font_probe probe = {0};

    if (host_render_probe_font(font_size,
                               CROFT_EDITOR_FONT_PROBE_SAMPLE,
                               (uint32_t)strlen(CROFT_EDITOR_FONT_PROBE_SAMPLE),
                               &probe) != 0) {
        return;
    }

    printf("editor-font-probe variant=%s backend=%s role=text requested_family=%s requested_style=%s resolved_family=%s resolved_style=%s point_size=%.1f sample_width=%.3f font_line_height=%.3f editor_line_height=%.3f\n",
           variant,
           backend,
           probe.requested_family,
           probe.requested_style,
           probe.resolved_family,
           probe.resolved_style,
           probe.point_size,
           probe.sample_width,
           probe.line_height,
           editor_line_height);
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

static void print_frame_profile_summary(const char* variant, const render_frame_profile* profile) {
    if (!profile) {
        return;
    }

    printf("editor-scene-frame variant=%s begin_ms=%.3f draw_tree_ms=%.3f end_frame_ms=%.3f present_ms=%.3f\n",
           variant,
           usec_to_msec(profile->begin_frame_total_usec),
           usec_to_msec(profile->draw_tree_total_usec),
           usec_to_msec(profile->end_frame_total_usec),
           usec_to_msec(profile->present_total_usec));
}

static void print_render_profile_summary(const char* variant) {
    croft_host_render_profile_snapshot profile = {0};

    host_render_get_profile(&profile);
    if (!profile.enabled) {
        return;
    }

    printf("editor-render-profile variant=%s begin_calls=%llu begin_ms=%.3f lock_ms=%.3f target_ms=%.3f drawable_ms=%.3f surface_ms=%.3f command_buffer_ms=%.3f encoder_ms=%.3f flush_ms=%.3f submit_ms=%.3f wait_ms=%.3f present_ms=%.3f unlock_ms=%.3f blit_ms=%.3f end_calls=%llu end_ms=%.3f\n",
           variant,
           (unsigned long long)profile.begin_frame_calls,
           usec_to_msec(profile.begin_frame_total_usec),
           usec_to_msec(profile.context_lock_total_usec),
           usec_to_msec(profile.target_update_total_usec),
           usec_to_msec(profile.acquire_drawable_total_usec),
           usec_to_msec(profile.surface_create_total_usec),
           usec_to_msec(profile.command_buffer_total_usec),
           usec_to_msec(profile.encoder_start_total_usec),
           usec_to_msec(profile.flush_total_usec),
           usec_to_msec(profile.submit_total_usec),
           usec_to_msec(profile.wait_total_usec),
           usec_to_msec(profile.present_total_usec),
           usec_to_msec(profile.unlock_total_usec),
           usec_to_msec(profile.blit_total_usec),
           (unsigned long long)profile.end_frame_calls,
           usec_to_msec(profile.end_frame_total_usec));
}

static void on_ui_event(int32_t type, int32_t arg0, int32_t arg1) {
    if (type == CROFT_UI_EVENT_KEY) {
        uint32_t modifiers = host_ui_get_modifiers();
        text_editor_node_set_modifiers(&g_editor, modifiers);
        if (text_editor_node_is_find_active(&g_editor)) {
            if (g_focused_node && g_focused_node->vtbl && g_focused_node->vtbl->on_key_event) {
                g_focused_node->vtbl->on_key_event(g_focused_node, arg0, arg1);
            }
            request_redraw();
            return;
        }
        if (arg0 == 256 && arg1 == 1) {
            g_running = 0;
            request_redraw();
            return;
        }
        if (arg0 == 81 && arg1 == 1
                && (modifiers & (CROFT_UI_MOD_SUPER | CROFT_UI_MOD_CONTROL)) != 0u) {
            g_running = 0;
            request_redraw();
            return;
        }
        if (arg1 == 1 && (modifiers & (CROFT_UI_MOD_SUPER | CROFT_UI_MOD_CONTROL)) != 0u) {
            if (arg0 == 79) {
                if (!open_document_via_dialog()) {
                    g_running = 0;
                }
                return;
            }
            if (arg0 == 83) {
                if (!save_document_via_dialog((modifiers & CROFT_UI_MOD_SHIFT) != 0u)) {
                    g_running = 0;
                }
                return;
            }
        }
        if (g_focused_node && g_focused_node->vtbl && g_focused_node->vtbl->on_key_event) {
            g_focused_node->vtbl->on_key_event(g_focused_node, arg0, arg1);
        }
        request_redraw();
    } else if (type == CROFT_UI_EVENT_CHAR) {
        text_editor_node_set_modifiers(&g_editor, host_ui_get_modifiers());
        if (g_focused_node && g_focused_node->vtbl && g_focused_node->vtbl->on_char_event) {
            g_focused_node->vtbl->on_char_event(g_focused_node, (uint32_t)arg0);
        }
        request_redraw();
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
        request_redraw();
    } else if (type == CROFT_UI_EVENT_MOUSE) {
        if (arg1 == 1 && arg0 == 1) {
            hit_result hit;
            host_ui_get_mouse_pos(&g_mouse_x, &g_mouse_y);
            scene_node_hit_test_tree(&g_root_vp.base, (float)g_mouse_x, (float)g_mouse_y, &hit);
            if (hit.node == &g_editor.base) {
                show_editor_context_menu((float)g_mouse_x, (float)g_mouse_y);
                return;
            }
        }
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
        request_redraw();
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
        request_redraw();
    }
}

int main(int argc, char** argv) {
    const char* target_file = argc > 1 ? argv[1] : NULL;
    const char* auto_close_env = getenv("CROFT_EDITOR_AUTO_CLOSE_MS");
    const char* font_probe_env = getenv("CROFT_EDITOR_FONT_PROBE");
    const char* profile_env = getenv("CROFT_EDITOR_PROFILE");
    const char* fallback =
        "Big analysis, small binaries.\n"
        "\n"
        "This scene-based editor reuses the shared Sapling document layer.\n";
    double start_time = 0.0;
    double end_time = 0.0;
    uint32_t frame_count = 0u;
    int profile_enabled = env_flag_enabled(profile_env);
    int font_probe_enabled = env_flag_enabled(font_probe_env);
    render_frame_profile frame_profile = {0};
    uint32_t auto_close_ms = 0u;

    g_document = croft_editor_document_open(argc > 0 ? argv[0] : NULL,
                                            target_file,
                                            (const uint8_t*)fallback,
                                            strlen(fallback));
    if (!g_document) {
        return 1;
    }

    croft_editor_scene_runtime_state_init(&g_runtime);
    if (auto_close_env && auto_close_env[0] != '\0') {
        int parsed = atoi(auto_close_env);
        if (parsed > 0) {
            auto_close_ms = (uint32_t)parsed;
        }
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
    host_render_set_profiling(profile_enabled);

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
        bind_document_to_editor(g_document);
        text_editor_node_set_profiling(&g_editor, profile_enabled);
        scene_node_add_child(&g_root_vp.base, &g_editor.base);
        if (font_probe_enabled) {
            print_font_probe_summary("scene", "tgfx", g_editor.line_height, g_editor.font_size);
        }
    }

    start_time = host_ui_get_time();
    while (g_running && !host_ui_should_close()) {
        uint32_t fw = 0;
        uint32_t fh = 0;
        uint64_t now_ms = monotonic_usec_now() / 1000u;

        if (croft_editor_scene_runtime_should_auto_close((uint64_t)(start_time * 1000.0),
                                                         now_ms,
                                                         auto_close_ms)) {
            break;
        }

        host_ui_poll_events();
        host_ui_get_framebuffer_size(&fw, &fh);
        croft_editor_scene_runtime_sync_bounds(&g_runtime,
                                               &g_root_vp,
                                               &g_editor,
                                               fw,
                                               fh,
                                               (float)CROFT_EDITOR_WINDOW_PADDING);
        croft_editor_scene_runtime_sync_cursor_blink(&g_runtime, &g_editor, now_ms);

        if (!croft_editor_scene_runtime_needs_redraw(&g_runtime)) {
            continue;
        }

        {
            uint64_t phase_start_usec = profile_enabled ? monotonic_usec_now() : 0u;

            if (host_render_begin_frame(fw, fh) == 0) {
                uint64_t phase_end_usec;
                render_ctx rc;

                if (profile_enabled) {
                    phase_end_usec = monotonic_usec_now();
                    if (phase_end_usec >= phase_start_usec) {
                        frame_profile.begin_frame_total_usec += phase_end_usec - phase_start_usec;
                    }
                    phase_start_usec = phase_end_usec;
                }

                host_render_clear(0xF3F4F6FF);

                rc.fg_color = 0x111111FF;
                rc.bg_color = 0xFAFBFCFF;
                rc.time = host_ui_get_time();
                scene_node_draw_tree(&g_root_vp.base, &rc);
                if (profile_enabled) {
                    phase_end_usec = monotonic_usec_now();
                    if (phase_end_usec >= phase_start_usec) {
                        frame_profile.draw_tree_total_usec += phase_end_usec - phase_start_usec;
                    }
                    phase_start_usec = phase_end_usec;
                }

                host_render_end_frame();
                if (profile_enabled) {
                    phase_end_usec = monotonic_usec_now();
                    if (phase_end_usec >= phase_start_usec) {
                        frame_profile.end_frame_total_usec += phase_end_usec - phase_start_usec;
                    }
                    phase_start_usec = phase_end_usec;
                }

                host_ui_swap_buffers();
                if (profile_enabled) {
                    phase_end_usec = monotonic_usec_now();
                    if (phase_end_usec >= phase_start_usec) {
                        frame_profile.present_total_usec += phase_end_usec - phase_start_usec;
                    }
                }
                frame_count++;
                croft_editor_scene_runtime_note_frame_rendered(&g_runtime);
            }
        }
    }

    end_time = host_ui_get_time();
    printf("editor-scene frames=%u wall_ms=%llu\n",
           frame_count,
           (unsigned long long)((end_time - start_time) * 1000.0));
    print_frame_profile_summary("scene", &frame_profile);
    print_render_profile_summary("scene");
    print_editor_profile_summary("scene", &g_editor);
    fflush(stdout);

    host_render_terminate();
    host_ui_terminate();
    text_editor_node_dispose(&g_editor);
    croft_editor_document_destroy(g_document);
    return 0;
}
