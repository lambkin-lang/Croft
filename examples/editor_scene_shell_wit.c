#include "croft/editor_document.h"
#include "croft/editor_document_fs.h"
#include "croft/editor_menu_ids.h"
#include "croft/host_render.h"
#include "croft/host_ui.h"
#include "croft/scene.h"
#include "croft/scene_a11y_bridge.h"
#include "croft/wit_host_a11y_runtime.h"
#include "croft/wit_host_clipboard_runtime.h"
#include "croft/wit_host_clock_runtime.h"
#include "croft/wit_host_editor_input_runtime.h"
#include "croft/wit_host_menu_runtime.h"
#include "croft/wit_host_window_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_running = 1;
static viewport_node g_root_vp;
static text_editor_node g_editor;
static scene_node* g_focused_node = NULL;
static double g_mouse_x = 0.0;
static double g_mouse_y = 0.0;
static croft_editor_document* g_document = NULL;
static int g_needs_redraw = 1;

enum {
    CROFT_EDITOR_WINDOW_PADDING = 16
};

typedef struct render_frame_profile {
    uint64_t begin_frame_total_usec;
    uint64_t draw_tree_total_usec;
    uint64_t end_frame_total_usec;
    uint64_t present_total_usec;
} render_frame_profile;

static int env_flag_enabled(const char* value)
{
    return value && value[0] != '\0' && !(value[0] == '0' && value[1] == '\0');
}

static double usec_to_msec(uint64_t usec)
{
    return (double)usec / 1000.0;
}

static void request_redraw(void)
{
    g_needs_redraw = 1;
}

static void print_frame_profile_summary(const char* variant, const render_frame_profile* profile)
{
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

static void print_render_profile_summary(const char* variant)
{
    croft_host_render_profile_snapshot profile = {0};

    host_render_get_profile(&profile);
    if (!profile.enabled) {
        return;
    }

    printf("editor-render-profile variant=%s begin_calls=%llu begin_ms=%.3f lock_ms=%.3f target_ms=%.3f drawable_ms=%.3f surface_ms=%.3f command_buffer_ms=%.3f encoder_ms=%.3f submit_ms=%.3f present_ms=%.3f unlock_ms=%.3f blit_ms=%.3f end_calls=%llu end_ms=%.3f\n",
           variant,
           (unsigned long long)profile.begin_frame_calls,
           usec_to_msec(profile.begin_frame_total_usec),
           usec_to_msec(profile.context_lock_total_usec),
           usec_to_msec(profile.target_update_total_usec),
           usec_to_msec(profile.acquire_drawable_total_usec),
           usec_to_msec(profile.surface_create_total_usec),
           usec_to_msec(profile.command_buffer_total_usec),
           usec_to_msec(profile.encoder_start_total_usec),
           usec_to_msec(profile.submit_total_usec),
           usec_to_msec(profile.present_total_usec),
           usec_to_msec(profile.unlock_total_usec),
           usec_to_msec(profile.blit_total_usec),
           (unsigned long long)profile.end_frame_calls,
           usec_to_msec(profile.end_frame_total_usec));
}

static void print_editor_profile_summary(const char* variant, const text_editor_node* editor)
{
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

static int window_expect_ok(const SapWitHostWindowReply* reply, SapWitHostWindowResource* window_out)
{
    if (!reply || !window_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_WINDOW_REPLY_WINDOW
            || reply->val.window.case_tag != SAP_WIT_HOST_WINDOW_OP_RESULT_OK) {
        return 0;
    }
    *window_out = reply->val.window.val.ok;
    return 1;
}

static int window_expect_status_ok(const SapWitHostWindowReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_WINDOW_REPLY_STATUS
        && reply->val.status.case_tag == SAP_WIT_HOST_WINDOW_STATUS_OK;
}

static int window_expect_event(const SapWitHostWindowReply* reply, SapWitHostWindowEvent* event_out)
{
    if (!reply || !event_out) {
        return -1;
    }
    if (reply->case_tag != SAP_WIT_HOST_WINDOW_REPLY_EVENT) {
        return -1;
    }
    if (reply->val.event.case_tag == SAP_WIT_HOST_WINDOW_EVENT_RESULT_EMPTY) {
        return 0;
    }
    if (reply->val.event.case_tag != SAP_WIT_HOST_WINDOW_EVENT_RESULT_OK) {
        return -1;
    }
    *event_out = reply->val.event.val.ok;
    return 1;
}

static int window_expect_bool(const SapWitHostWindowReply* reply, uint8_t* value_out)
{
    if (!reply || !value_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_WINDOW_REPLY_SHOULD_CLOSE
            || reply->val.should_close.case_tag != SAP_WIT_HOST_WINDOW_BOOL_RESULT_OK) {
        return 0;
    }
    *value_out = reply->val.should_close.val.ok;
    return 1;
}

static int window_expect_size(const SapWitHostWindowReply* reply, uint32_t* width_out, uint32_t* height_out)
{
    if (!reply || !width_out || !height_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_WINDOW_REPLY_SIZE
            || reply->val.size.case_tag != SAP_WIT_HOST_WINDOW_SIZE_RESULT_OK) {
        return 0;
    }
    *width_out = reply->val.size.val.ok.width;
    *height_out = reply->val.size.val.ok.height;
    return 1;
}

static int clock_expect_now(const SapWitHostClockReply* reply, uint64_t* now_out)
{
    if (!reply || !now_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_CLOCK_REPLY_NOW
            || reply->val.now.case_tag != SAP_WIT_HOST_CLOCK_NOW_RESULT_OK) {
        return 0;
    }
    *now_out = reply->val.now.val.ok;
    return 1;
}

static int clock_now_usec(croft_wit_host_clock_runtime* runtime, uint64_t* now_usec_out)
{
    SapWitHostClockCommand command = {0};
    SapWitHostClockReply reply = {0};
    uint64_t now_ms = 0u;

    if (!runtime || !now_usec_out) {
        return 0;
    }

    command.case_tag = SAP_WIT_HOST_CLOCK_COMMAND_MONOTONIC_NOW;
    if (croft_wit_host_clock_runtime_dispatch(runtime, &command, &reply) != 0
            || !clock_expect_now(&reply, &now_ms)) {
        return 0;
    }

    *now_usec_out = now_ms * 1000u;
    return 1;
}

static int menu_expect_status_ok(const SapWitHostMenuReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_MENU_REPLY_STATUS
        && reply->val.status.case_tag == SAP_WIT_HOST_MENU_STATUS_OK;
}

static int menu_expect_action(const SapWitHostMenuReply* reply, int32_t* action_id_out)
{
    if (!reply || !action_id_out) {
        return -1;
    }
    if (reply->case_tag != SAP_WIT_HOST_MENU_REPLY_ACTION) {
        return -1;
    }
    if (reply->val.action.case_tag == SAP_WIT_HOST_MENU_ACTION_RESULT_EMPTY) {
        return 0;
    }
    if (reply->val.action.case_tag != SAP_WIT_HOST_MENU_ACTION_RESULT_OK) {
        return -1;
    }
    *action_id_out = reply->val.action.val.ok;
    return 1;
}

static int editor_input_expect_status_ok(const SapWitHostEditorInputReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_EDITOR_INPUT_REPLY_STATUS
        && reply->val.status.case_tag == SAP_WIT_HOST_EDITOR_INPUT_STATUS_OK;
}

static int editor_input_expect_action(const SapWitHostEditorInputReply* reply,
                                      SapWitHostEditorInputEditorAction* action_out)
{
    if (!reply || !action_out) {
        return -1;
    }
    if (reply->case_tag != SAP_WIT_HOST_EDITOR_INPUT_REPLY_ACTION) {
        return -1;
    }
    if (reply->val.action.case_tag == SAP_WIT_HOST_EDITOR_INPUT_ACTION_RESULT_EMPTY) {
        return 0;
    }
    if (reply->val.action.case_tag != SAP_WIT_HOST_EDITOR_INPUT_ACTION_RESULT_OK) {
        return -1;
    }
    *action_out = reply->val.action.val.ok;
    return 1;
}

static int clipboard_expect_status_ok(const SapWitHostClipboardReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_CLIPBOARD_REPLY_STATUS
        && reply->val.status.case_tag == SAP_WIT_HOST_CLIPBOARD_STATUS_OK;
}

static int clipboard_expect_text(const SapWitHostClipboardReply* reply,
                                 const uint8_t** data_out,
                                 uint32_t* len_out)
{
    if (!reply || !data_out || !len_out) {
        return -1;
    }
    if (reply->case_tag != SAP_WIT_HOST_CLIPBOARD_REPLY_TEXT) {
        return -1;
    }
    if (reply->val.text.case_tag == SAP_WIT_HOST_CLIPBOARD_TEXT_RESULT_EMPTY) {
        return 0;
    }
    if (reply->val.text.case_tag != SAP_WIT_HOST_CLIPBOARD_TEXT_RESULT_OK) {
        return -1;
    }
    *data_out = reply->val.text.val.ok.data;
    *len_out = reply->val.text.val.ok.len;
    return 1;
}

static int apply_menu_command(croft_wit_host_menu_runtime* runtime,
                              SapWitHostMenuCommand* command,
                              SapWitHostMenuReply* reply)
{
    return croft_wit_host_menu_runtime_dispatch(runtime, command, reply) == 0
        && menu_expect_status_ok(reply);
}

static int menu_add_item(croft_wit_host_menu_runtime* runtime,
                         SapWitHostMenuReply* reply,
                         int32_t action_id,
                         int32_t parent_action_id,
                         const char* label,
                         const char* shortcut,
                         uint32_t mods)
{
    SapWitHostMenuCommand command = {0};

    command.case_tag = SAP_WIT_HOST_MENU_COMMAND_ADD_ITEM;
    command.val.add_item.action_id = action_id;
    command.val.add_item.parent_action_id = parent_action_id;
    command.val.add_item.label_data = (const uint8_t*)label;
    command.val.add_item.label_len = (uint32_t)strlen(label);
    command.val.add_item.has_shortcut = shortcut ? 1u : 0u;
    command.val.add_item.shortcut_data = (const uint8_t*)(shortcut ? shortcut : "");
    command.val.add_item.shortcut_len = shortcut ? (uint32_t)strlen(shortcut) : 0u;
    command.val.add_item.mods = mods;
    return apply_menu_command(runtime, &command, reply);
}

static int editor_install_menu(croft_wit_host_menu_runtime* runtime)
{
    SapWitHostMenuCommand command = {0};
    SapWitHostMenuReply reply = {0};

    command.case_tag = SAP_WIT_HOST_MENU_COMMAND_BEGIN_UPDATE;
    if (!apply_menu_command(runtime, &command, &reply)) {
        return 0;
    }

    if (!menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_APP_ROOT, -1, "App", NULL, 0u)
            || !menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_FILE_ROOT, -1, "File", NULL, 0u)
            || !menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_EDIT_ROOT, -1, "Edit", NULL, 0u)
            || !menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_SAVE, CROFT_EDITOR_MENU_FILE_ROOT,
                              "Save", "s", SAP_WIT_HOST_MENU_MODIFIERS_CMD)
            || !menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_QUIT, CROFT_EDITOR_MENU_APP_ROOT,
                              "Quit Croft", "q", SAP_WIT_HOST_MENU_MODIFIERS_CMD)
            || !menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_UNDO, CROFT_EDITOR_MENU_EDIT_ROOT,
                              "Undo", "z", SAP_WIT_HOST_MENU_MODIFIERS_CMD)
            || !menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_REDO, CROFT_EDITOR_MENU_EDIT_ROOT,
                              "Redo", "z", SAP_WIT_HOST_MENU_MODIFIERS_CMD | SAP_WIT_HOST_MENU_MODIFIERS_SHIFT)
            || !menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_SELECT_ALL, CROFT_EDITOR_MENU_EDIT_ROOT,
                              "Select All", "a", SAP_WIT_HOST_MENU_MODIFIERS_CMD)
            || !menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_COPY, CROFT_EDITOR_MENU_EDIT_ROOT,
                              "Copy", "c", SAP_WIT_HOST_MENU_MODIFIERS_CMD)
            || !menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_CUT, CROFT_EDITOR_MENU_EDIT_ROOT,
                              "Cut", "x", SAP_WIT_HOST_MENU_MODIFIERS_CMD)
            || !menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_PASTE, CROFT_EDITOR_MENU_EDIT_ROOT,
                              "Paste", "v", SAP_WIT_HOST_MENU_MODIFIERS_CMD)
            || !menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_INDENT, CROFT_EDITOR_MENU_EDIT_ROOT,
                              "Indent Line", "]", SAP_WIT_HOST_MENU_MODIFIERS_CMD)
            || !menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_OUTDENT, CROFT_EDITOR_MENU_EDIT_ROOT,
                              "Outdent Line", "[", SAP_WIT_HOST_MENU_MODIFIERS_CMD)
            || !menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_FOLD, CROFT_EDITOR_MENU_EDIT_ROOT,
                              "Fold Region", "[",
                              SAP_WIT_HOST_MENU_MODIFIERS_CMD | SAP_WIT_HOST_MENU_MODIFIERS_ALT)
            || !menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_UNFOLD, CROFT_EDITOR_MENU_EDIT_ROOT,
                              "Unfold Region", "]",
                              SAP_WIT_HOST_MENU_MODIFIERS_CMD | SAP_WIT_HOST_MENU_MODIFIERS_ALT)
            || !menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_FIND, CROFT_EDITOR_MENU_EDIT_ROOT,
                              "Find", "f", SAP_WIT_HOST_MENU_MODIFIERS_CMD)
            || !menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_FIND_NEXT, CROFT_EDITOR_MENU_EDIT_ROOT,
                              "Find Next", "g", SAP_WIT_HOST_MENU_MODIFIERS_CMD)
            || !menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_FIND_PREVIOUS, CROFT_EDITOR_MENU_EDIT_ROOT,
                              "Find Previous", "g",
                              SAP_WIT_HOST_MENU_MODIFIERS_CMD | SAP_WIT_HOST_MENU_MODIFIERS_SHIFT)) {
        return 0;
    }

    command.case_tag = SAP_WIT_HOST_MENU_COMMAND_COMMIT_UPDATE;
    return apply_menu_command(runtime, &command, &reply);
}

static int editor_apply_action(croft_wit_host_clipboard_runtime* clipboard_runtime,
                               const SapWitHostEditorInputEditorAction* action)
{
    if (!action) {
        return 0;
    }

    switch (action->case_tag) {
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_LEFT:
            text_editor_node_set_modifiers(&g_editor,
                (action->val.move_left.flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_SELECTING)
                    ? CROFT_UI_MOD_SHIFT : 0u);
            g_editor.modifiers |=
                (action->val.move_left.flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_WORD_PART)
                    ? (CROFT_UI_MOD_ALT | CROFT_UI_MOD_CONTROL)
                    : ((action->val.move_left.flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_WORD)
                        ? CROFT_UI_MOD_CONTROL : 0u);
            g_editor.base.vtbl->on_key_event(&g_editor.base, 263, 1);
            return 1;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_RIGHT:
            text_editor_node_set_modifiers(&g_editor,
                (action->val.move_right.flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_SELECTING)
                    ? CROFT_UI_MOD_SHIFT : 0u);
            g_editor.modifiers |=
                (action->val.move_right.flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_WORD_PART)
                    ? (CROFT_UI_MOD_ALT | CROFT_UI_MOD_CONTROL)
                    : ((action->val.move_right.flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_WORD)
                        ? CROFT_UI_MOD_CONTROL : 0u);
            g_editor.base.vtbl->on_key_event(&g_editor.base, 262, 1);
            return 1;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_UP:
            text_editor_node_set_modifiers(&g_editor,
                (action->val.move_up.flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_SELECTING)
                    ? CROFT_UI_MOD_SHIFT : 0u);
            g_editor.base.vtbl->on_key_event(&g_editor.base, 265, 1);
            return 1;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_DOWN:
            text_editor_node_set_modifiers(&g_editor,
                (action->val.move_down.flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_SELECTING)
                    ? CROFT_UI_MOD_SHIFT : 0u);
            g_editor.base.vtbl->on_key_event(&g_editor.base, 264, 1);
            return 1;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_HOME:
            text_editor_node_set_modifiers(&g_editor,
                (action->val.move_home.flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_SELECTING)
                    ? CROFT_UI_MOD_SHIFT : 0u);
            g_editor.base.vtbl->on_key_event(&g_editor.base, 268, 1);
            return 1;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_END:
            text_editor_node_set_modifiers(&g_editor,
                (action->val.move_end.flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_SELECTING)
                    ? CROFT_UI_MOD_SHIFT : 0u);
            g_editor.base.vtbl->on_key_event(&g_editor.base, 269, 1);
            return 1;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_DELETE_LEFT:
            text_editor_node_set_modifiers(&g_editor,
                (action->val.delete_left.flags & SAP_WIT_HOST_EDITOR_INPUT_DELETE_FLAGS_WORD_PART)
                    ? (CROFT_UI_MOD_ALT | CROFT_UI_MOD_CONTROL)
                    : ((action->val.delete_left.flags & SAP_WIT_HOST_EDITOR_INPUT_DELETE_FLAGS_WORD)
                        ? CROFT_UI_MOD_CONTROL : 0u));
            g_editor.base.vtbl->on_key_event(&g_editor.base, 259, 1);
            return 1;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_DELETE_RIGHT:
            text_editor_node_set_modifiers(&g_editor,
                (action->val.delete_right.flags & SAP_WIT_HOST_EDITOR_INPUT_DELETE_FLAGS_WORD_PART)
                    ? (CROFT_UI_MOD_ALT | CROFT_UI_MOD_CONTROL)
                    : ((action->val.delete_right.flags & SAP_WIT_HOST_EDITOR_INPUT_DELETE_FLAGS_WORD)
                        ? CROFT_UI_MOD_CONTROL : 0u));
            g_editor.base.vtbl->on_key_event(&g_editor.base, 261, 1);
            return 1;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_INDENT:
            return text_editor_node_indent(&g_editor) == 0;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_OUTDENT:
            return text_editor_node_outdent(&g_editor) == 0;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_FOLD:
            return text_editor_node_fold(&g_editor) == 0;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_UNFOLD:
            return text_editor_node_unfold(&g_editor) == 0;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_INSERT_CODEPOINT:
            g_editor.base.vtbl->on_char_event(&g_editor.base, action->val.insert_codepoint);
            return 1;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_UNDO:
            return text_editor_node_undo(&g_editor) == 0;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_REDO:
            return text_editor_node_redo(&g_editor) == 0;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_SELECT_ALL:
            text_editor_node_select_all(&g_editor);
            return 1;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_COPY:
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_CUT: {
            char* utf8 = NULL;
            size_t utf8_len = 0u;
            SapWitHostClipboardCommand command = {0};
            SapWitHostClipboardReply reply = {0};

            if (text_editor_node_copy_selection_utf8(&g_editor, &utf8, &utf8_len) != 0) {
                return 0;
            }
            command.case_tag = SAP_WIT_HOST_CLIPBOARD_COMMAND_SET_TEXT;
            command.val.set_text.utf8_data = (const uint8_t*)utf8;
            command.val.set_text.utf8_len = (uint32_t)utf8_len;
            if (croft_wit_host_clipboard_runtime_dispatch(clipboard_runtime, &command, &reply) != 0
                    || !clipboard_expect_status_ok(&reply)) {
                free(utf8);
                croft_wit_host_clipboard_reply_dispose(&reply);
                return 0;
            }
            croft_wit_host_clipboard_reply_dispose(&reply);
            if (action->case_tag == SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_CUT) {
                int ok = text_editor_node_delete_selection(&g_editor, 1) == 0;
                free(utf8);
                return ok;
            }
            free(utf8);
            return 1;
        }
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_PASTE: {
            SapWitHostClipboardCommand command = {0};
            SapWitHostClipboardReply reply = {0};
            const uint8_t* data = NULL;
            uint32_t len = 0u;
            int result;

            command.case_tag = SAP_WIT_HOST_CLIPBOARD_COMMAND_GET_TEXT;
            if (croft_wit_host_clipboard_runtime_dispatch(clipboard_runtime, &command, &reply) != 0) {
                return 0;
            }
            result = clipboard_expect_text(&reply, &data, &len);
            if (result <= 0) {
                croft_wit_host_clipboard_reply_dispose(&reply);
                return result == 0 ? 1 : 0;
            }
            result = text_editor_node_replace_selection_utf8(&g_editor, data, len) == 0;
            croft_wit_host_clipboard_reply_dispose(&reply);
            return result;
        }
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_SAVE:
            return croft_editor_document_save(g_document) == 0;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_QUIT:
            g_running = 0;
            return 1;
        default:
            return 0;
    }
}

static int editor_handle_find_key(const SapWitHostWindowEvent* event)
{
    uint32_t modifiers;
    int command_mode;

    if (!event || event->case_tag != SAP_WIT_HOST_WINDOW_EVENT_KEY) {
        return 0;
    }

    modifiers = event->val.key.modifiers;
    command_mode = (modifiers & (CROFT_UI_MOD_SUPER | CROFT_UI_MOD_CONTROL)) != 0u;
    text_editor_node_set_modifiers(&g_editor, modifiers);

    if (text_editor_node_is_find_active(&g_editor)
            || (command_mode
                && (event->val.key.key == 70 || event->val.key.key == 71))) {
        g_editor.base.vtbl->on_key_event(&g_editor.base,
                                         event->val.key.key,
                                         event->val.key.action);
        request_redraw();
        return 1;
    }

    return 0;
}

static int editor_handle_find_char(const SapWitHostWindowEvent* event)
{
    if (!event || event->case_tag != SAP_WIT_HOST_WINDOW_EVENT_CHAR_EVENT) {
        return 0;
    }
    if (!text_editor_node_is_find_active(&g_editor)) {
        return 0;
    }

    g_editor.base.vtbl->on_char_event(&g_editor.base, event->val.char_event.codepoint);
    request_redraw();
    return 1;
}

static int pump_editor_actions(croft_wit_host_editor_input_runtime* editor_input_runtime,
                               croft_wit_host_clipboard_runtime* clipboard_runtime,
                               int* out_did_action)
{
    SapWitHostEditorInputCommand input_command = {0};
    SapWitHostEditorInputReply input_reply = {0};
    int did_action = 0;

    for (;;) {
        SapWitHostEditorInputEditorAction action = {0};
        int status;

        input_command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_NEXT_ACTION;
        if (croft_wit_host_editor_input_runtime_dispatch(editor_input_runtime,
                                                         &input_command,
                                                         &input_reply) != 0) {
            return 0;
        }
        status = editor_input_expect_action(&input_reply, &action);
        if (status < 0) {
            return 0;
        }
        if (status == 0) {
            if (out_did_action) {
                *out_did_action = did_action;
            }
            return 1;
        }
        if (!editor_apply_action(clipboard_runtime, &action)) {
            return 0;
        }
        did_action = 1;
    }
}

int main(int argc, char** argv)
{
    const char* target_file = argc > 1 ? argv[1] : NULL;
    const char* auto_close_env = getenv("CROFT_EDITOR_AUTO_CLOSE_MS");
    const char* profile_env = getenv("CROFT_EDITOR_PROFILE");
    const char* fallback =
        "Big analysis, small binaries.\n"
        "\n"
        "This WIT-facing scene shell routes window, menu, clipboard,\n"
        "input, and accessibility through generated host mix-ins.\n";
    croft_wit_host_window_runtime* window_runtime = NULL;
    croft_wit_host_clock_runtime* clock_runtime = NULL;
    croft_wit_host_menu_runtime* menu_runtime = NULL;
    croft_wit_host_clipboard_runtime* clipboard_runtime = NULL;
    croft_wit_host_editor_input_runtime* editor_input_runtime = NULL;
    croft_wit_host_a11y_runtime* a11y_runtime = NULL;
    SapWitHostWindowResource window = SAP_WIT_HOST_WINDOW_RESOURCE_INVALID;
    SapWitHostWindowCommand window_command = {0};
    SapWitHostWindowReply window_reply = {0};
    SapWitHostClockCommand clock_command = {0};
    SapWitHostClockReply clock_reply = {0};
    uint32_t auto_close_ms = 0u;
    uint64_t start_ms = 0u;
    uint64_t end_ms = 0u;
    uint32_t frame_count = 0u;
    int profile_enabled = env_flag_enabled(profile_env);
    render_frame_profile frame_profile = {0};
    int last_cursor_blink_visible = -1;
    uint32_t last_fw = 0u;
    uint32_t last_fh = 0u;
    int rc = 1;

    if (auto_close_env && auto_close_env[0] != '\0') {
        int parsed = atoi(auto_close_env);
        if (parsed > 0) {
            auto_close_ms = (uint32_t)parsed;
        }
    }

    g_document = croft_editor_document_open(argc > 0 ? argv[0] : NULL,
                                            target_file,
                                            (const uint8_t*)fallback,
                                            strlen(fallback));
    if (!g_document) {
        return 1;
    }

    window_runtime = croft_wit_host_window_runtime_create();
    clock_runtime = croft_wit_host_clock_runtime_create();
    menu_runtime = croft_wit_host_menu_runtime_create();
    clipboard_runtime = croft_wit_host_clipboard_runtime_create();
    editor_input_runtime = croft_wit_host_editor_input_runtime_create();
    a11y_runtime = croft_wit_host_a11y_runtime_create();
    if (!window_runtime || !clock_runtime || !menu_runtime || !clipboard_runtime
            || !editor_input_runtime || !a11y_runtime) {
        goto cleanup;
    }

    window_command.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_OPEN;
    window_command.val.open.width = 1000u;
    window_command.val.open.height = 800u;
    window_command.val.open.title_data = (const uint8_t*)"Croft Text Editor Scene Shell (WIT)";
    window_command.val.open.title_len = (uint32_t)strlen((const char*)window_command.val.open.title_data);
    if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_command, &window_reply) != 0
            || !window_expect_ok(&window_reply, &window)) {
        goto cleanup;
    }

    if (host_render_init() != 0) {
        goto cleanup;
    }
    host_render_set_profiling(profile_enabled);

    croft_wit_host_a11y_runtime_install_bridge(a11y_runtime);
    if (croft_scene_a11y_open() != 0) {
        goto cleanup;
    }

    if (!editor_install_menu(menu_runtime)) {
        goto cleanup;
    }

    {
        uint32_t fw = 0u;
        uint32_t fh = 0u;

        window_command.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_FRAMEBUFFER_SIZE;
        window_command.val.framebuffer_size.window = window;
        if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_command, &window_reply) != 0
                || !window_expect_size(&window_reply, &fw, &fh)) {
            goto cleanup;
        }

        viewport_node_init(&g_root_vp, 0.0f, 0.0f, (float)fw, (float)fh);
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

    clock_command.case_tag = SAP_WIT_HOST_CLOCK_COMMAND_MONOTONIC_NOW;
    if (croft_wit_host_clock_runtime_dispatch(clock_runtime, &clock_command, &clock_reply) != 0
            || !clock_expect_now(&clock_reply, &start_ms)) {
        goto cleanup;
    }

    while (g_running) {
        uint8_t should_close = 0u;
        uint32_t fw = 0u;
        uint32_t fh = 0u;
        uint64_t now_ms = 0u;

        window_command.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_POLL;
        window_command.val.poll.window = window;
        if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_command, &window_reply) != 0
                || !window_expect_status_ok(&window_reply)) {
            goto cleanup;
        }

        for (;;) {
            SapWitHostWindowEvent event = {0};
            int status;

            window_command.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_NEXT_EVENT;
            window_command.val.next_event.window = window;
            if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_command, &window_reply) != 0) {
                goto cleanup;
            }
            status = window_expect_event(&window_reply, &event);
            if (status < 0) {
                goto cleanup;
            }
            if (status == 0) {
                break;
            }

            switch (event.case_tag) {
                case SAP_WIT_HOST_WINDOW_EVENT_KEY: {
                    SapWitHostEditorInputCommand input_command = {0};
                    SapWitHostEditorInputReply input_reply = {0};
                    if (editor_handle_find_key(&event)) {
                        break;
                    }
                    input_command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_WINDOW_KEY;
                    input_command.val.window_key.key = event.val.key.key;
                    input_command.val.window_key.action = event.val.key.action;
                    input_command.val.window_key.modifiers = event.val.key.modifiers;
                    if (croft_wit_host_editor_input_runtime_dispatch(editor_input_runtime,
                                                                     &input_command,
                                                                     &input_reply) != 0
                            || !editor_input_expect_status_ok(&input_reply)) {
                        goto cleanup;
                    }
                    if (event.val.key.key == 256 && event.val.key.action == 1
                            && !text_editor_node_is_find_active(&g_editor)) {
                        g_running = 0;
                    }
                    request_redraw();
                    break;
                }
                case SAP_WIT_HOST_WINDOW_EVENT_CHAR_EVENT: {
                    SapWitHostEditorInputCommand input_command = {0};
                    SapWitHostEditorInputReply input_reply = {0};
                    if (editor_handle_find_char(&event)) {
                        break;
                    }
                    input_command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_WINDOW_CHAR;
                    input_command.val.window_char.codepoint = event.val.char_event.codepoint;
                    if (croft_wit_host_editor_input_runtime_dispatch(editor_input_runtime,
                                                                     &input_command,
                                                                     &input_reply) != 0
                            || !editor_input_expect_status_ok(&input_reply)) {
                        goto cleanup;
                    }
                    request_redraw();
                    break;
                }
                case SAP_WIT_HOST_WINDOW_EVENT_SCROLL:
                    g_editor.scroll_y += ((float)event.val.scroll.y_milli / 1000.0f) * 20.0f;
                    g_editor.scroll_x += ((float)event.val.scroll.x_milli / 1000.0f) * 20.0f;
                    if (g_editor.scroll_y > 0.0f) g_editor.scroll_y = 0.0f;
                    if (g_editor.scroll_x > 0.0f) g_editor.scroll_x = 0.0f;
                    request_redraw();
                    break;
                case SAP_WIT_HOST_WINDOW_EVENT_ZOOM: {
                    float delta = (float)event.val.zoom.delta_micros / 1000000.0f;
                    float old_scale = g_root_vp.scale;
                    float new_scale = old_scale * (1.0f + delta);
                    float cx = g_editor.base.sx * 0.5f;
                    float cy = g_editor.base.sy * 0.5f;
                    float ratio;

                    if (new_scale < 0.1f) new_scale = 0.1f;
                    if (new_scale > 10.0f) new_scale = 10.0f;
                    ratio = new_scale / old_scale;
                    g_root_vp.scroll_x = cx - (cx - g_root_vp.scroll_x) * ratio;
                    g_root_vp.scroll_y = cy - (cy - g_root_vp.scroll_y) * ratio;
                    g_root_vp.scale = new_scale;
                    request_redraw();
                    break;
                }
                case SAP_WIT_HOST_WINDOW_EVENT_CURSOR:
                    g_mouse_x = (double)event.val.cursor.x_milli / 1000.0;
                    g_mouse_y = (double)event.val.cursor.y_milli / 1000.0;
                    if (g_focused_node && g_focused_node->vtbl && g_focused_node->vtbl->on_mouse_event) {
                        float lx = (float)g_mouse_x;
                        float ly = (float)g_mouse_y;
                        if (g_root_vp.base.vtbl && g_root_vp.base.vtbl->transform_coords) {
                            g_root_vp.base.vtbl->transform_coords(&g_root_vp.base, &lx, &ly);
                        }
                        lx -= g_editor.base.x;
                        ly -= g_editor.base.y;
                        g_focused_node->vtbl->on_mouse_event(g_focused_node, 3, lx, ly);
                    }
                    request_redraw();
                    break;
                case SAP_WIT_HOST_WINDOW_EVENT_MOUSE:
                    if (event.val.mouse.action == 1) {
                        hit_result hit;
                        scene_node_hit_test_tree(&g_root_vp.base, (float)g_mouse_x, (float)g_mouse_y, &hit);
                        if (hit.node) {
                            g_focused_node = hit.node;
                            if (hit.node->vtbl && hit.node->vtbl->on_mouse_event) {
                                hit.node->vtbl->on_mouse_event(hit.node, 1, hit.local_x, hit.local_y);
                            }
                        }
                    } else if (event.val.mouse.action == 0) {
                        if (g_focused_node && g_focused_node->vtbl && g_focused_node->vtbl->on_mouse_event) {
                            g_focused_node->vtbl->on_mouse_event(g_focused_node, 0, 0.0f, 0.0f);
                        }
                    }
                    request_redraw();
                    break;
                default:
                    break;
            }
        }

        for (;;) {
            SapWitHostMenuCommand menu_command = {0};
            SapWitHostMenuReply menu_reply = {0};
            int32_t action_id = 0;
            int status;
            SapWitHostEditorInputCommand input_command = {0};
            SapWitHostEditorInputReply input_reply = {0};

            menu_command.case_tag = SAP_WIT_HOST_MENU_COMMAND_NEXT_ACTION;
            if (croft_wit_host_menu_runtime_dispatch(menu_runtime, &menu_command, &menu_reply) != 0) {
                goto cleanup;
            }
            status = menu_expect_action(&menu_reply, &action_id);
            if (status < 0) {
                goto cleanup;
            }
            if (status == 0) {
                break;
            }

            if (action_id == CROFT_EDITOR_MENU_FIND) {
                text_editor_node_find_activate(&g_editor);
                request_redraw();
                continue;
            }
            if (action_id == CROFT_EDITOR_MENU_FIND_NEXT) {
                text_editor_node_find_next(&g_editor);
                request_redraw();
                continue;
            }
            if (action_id == CROFT_EDITOR_MENU_FIND_PREVIOUS) {
                text_editor_node_find_previous(&g_editor);
                request_redraw();
                continue;
            }

            input_command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_MENU_ACTION;
            input_command.val.menu_action.action_id = action_id;
            if (croft_wit_host_editor_input_runtime_dispatch(editor_input_runtime,
                                                             &input_command,
                                                             &input_reply) != 0
                    || !editor_input_expect_status_ok(&input_reply)) {
                goto cleanup;
            }
            request_redraw();
        }

        {
            int did_action = 0;

            if (!pump_editor_actions(editor_input_runtime, clipboard_runtime, &did_action)) {
                goto cleanup;
            }
            if (did_action) {
                request_redraw();
            }
        }

        window_command.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_SHOULD_CLOSE;
        window_command.val.should_close.window = window;
        if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_command, &window_reply) != 0
                || !window_expect_bool(&window_reply, &should_close)) {
            goto cleanup;
        }
        if (should_close) {
            break;
        }

        if (croft_wit_host_clock_runtime_dispatch(clock_runtime, &clock_command, &clock_reply) != 0
                || !clock_expect_now(&clock_reply, &now_ms)) {
            goto cleanup;
        }
        if (auto_close_ms > 0u && now_ms - start_ms >= (uint64_t)auto_close_ms) {
            break;
        }

        window_command.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_FRAMEBUFFER_SIZE;
        window_command.val.framebuffer_size.window = window;
        if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_command, &window_reply) != 0
                || !window_expect_size(&window_reply, &fw, &fh)) {
            goto cleanup;
        }

        g_root_vp.base.sx = (float)fw;
        g_root_vp.base.sy = (float)fh;
        g_editor.base.sx = (float)fw - (float)(CROFT_EDITOR_WINDOW_PADDING * 2);
        g_editor.base.sy = (float)fh - (float)(CROFT_EDITOR_WINDOW_PADDING * 2);
        if (fw != last_fw || fh != last_fh) {
            last_fw = fw;
            last_fh = fh;
            request_redraw();
        }
        {
            int cursor_blink_visible = (g_editor.sel_start == g_editor.sel_end)
                ? (((int)(now_ms / 500u)) % 2)
                : 1;
            if (cursor_blink_visible != last_cursor_blink_visible) {
                last_cursor_blink_visible = cursor_blink_visible;
                request_redraw();
            }
        }

        if (!g_needs_redraw) {
            continue;
        }

        {
            uint64_t phase_start_usec = 0u;
            uint64_t phase_end_usec = 0u;

            if (profile_enabled && !clock_now_usec(clock_runtime, &phase_start_usec)) {
                goto cleanup;
            }

            if (host_render_begin_frame(fw, fh) == 0) {
                render_ctx rcx;

                if (profile_enabled) {
                    if (!clock_now_usec(clock_runtime, &phase_end_usec)) {
                        goto cleanup;
                    }
                    if (phase_end_usec >= phase_start_usec) {
                        frame_profile.begin_frame_total_usec += phase_end_usec - phase_start_usec;
                    }
                    phase_start_usec = phase_end_usec;
                }

                host_render_clear(0xF3F4F6FF);
                rcx.fg_color = 0x111111FF;
                rcx.bg_color = 0xFAFBFCFF;
                rcx.time = (double)now_ms / 1000.0;
                scene_node_draw_tree(&g_root_vp.base, &rcx);
                if (profile_enabled) {
                    if (!clock_now_usec(clock_runtime, &phase_end_usec)) {
                        goto cleanup;
                    }
                    if (phase_end_usec >= phase_start_usec) {
                        frame_profile.draw_tree_total_usec += phase_end_usec - phase_start_usec;
                    }
                    phase_start_usec = phase_end_usec;
                }

                host_render_end_frame();
                if (profile_enabled) {
                    if (!clock_now_usec(clock_runtime, &phase_end_usec)) {
                        goto cleanup;
                    }
                    if (phase_end_usec >= phase_start_usec) {
                        frame_profile.end_frame_total_usec += phase_end_usec - phase_start_usec;
                    }
                }
                frame_count++;
                g_needs_redraw = 0;
            }
        }
    }

    end_ms = start_ms;
    if (croft_wit_host_clock_runtime_dispatch(clock_runtime, &clock_command, &clock_reply) == 0
            && clock_expect_now(&clock_reply, &end_ms)) {
    }

    printf("editor-scene-wit frames=%u wall_ms=%llu\n",
           frame_count,
           (unsigned long long)(end_ms - start_ms));
    print_frame_profile_summary("scene-wit", &frame_profile);
    print_render_profile_summary("scene-wit");
    print_editor_profile_summary("scene-wit", &g_editor);
    fflush(stdout);
    rc = 0;

cleanup:
    text_editor_node_dispose(&g_editor);
    croft_scene_a11y_close();
    croft_scene_a11y_reset_bridge();
    host_render_terminate();
    if (window_runtime && window != SAP_WIT_HOST_WINDOW_RESOURCE_INVALID) {
        window_command.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_CLOSE;
        window_command.val.close.window = window;
        croft_wit_host_window_runtime_dispatch(window_runtime, &window_command, &window_reply);
    }
    croft_wit_host_a11y_runtime_destroy(a11y_runtime);
    croft_wit_host_editor_input_runtime_destroy(editor_input_runtime);
    croft_wit_host_clipboard_runtime_destroy(clipboard_runtime);
    croft_wit_host_menu_runtime_destroy(menu_runtime);
    croft_wit_host_clock_runtime_destroy(clock_runtime);
    croft_wit_host_window_runtime_destroy(window_runtime);
    croft_editor_document_destroy(g_document);
    g_document = NULL;
    return rc;
}
