#include "croft/editor_commands.h"
#include "croft/editor_menu_ids.h"
#include "croft/wit_host_clipboard_runtime.h"
#include "croft/wit_host_clock_runtime.h"
#include "croft/wit_host_editor_input_runtime.h"
#include "croft/wit_host_gpu2d_runtime.h"
#include "croft/wit_host_menu_runtime.h"
#include "croft/wit_host_window_runtime.h"
#include "croft/wit_text_program.h"
#include "croft/wit_text_runtime.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct textpad_state {
    croft_wit_text_runtime* text_runtime;
    SapWitCommonCoreTextResource text;
    uint32_t cursor;
    uint32_t anchor;
    uint32_t char_count;
    uint32_t frame_count;
    int running;
} textpad_state;

static uint32_t textpad_min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static uint32_t textpad_max_u32(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
}

static int textpad_has_selection(const textpad_state* state)
{
    return state && state->cursor != state->anchor;
}

static int expect_window_ok(const SapWitHostWindowReply* reply,
                            SapWitHostWindowResource* handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_WINDOW_REPLY_WINDOW
            || reply->val.window.case_tag != SAP_WIT_HOST_WINDOW_OP_RESULT_OK) {
        return 0;
    }
    *handle_out = reply->val.window.val.ok;
    return 1;
}

static int expect_window_status_ok(const SapWitHostWindowReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_WINDOW_REPLY_STATUS
        && reply->val.status.case_tag == SAP_WIT_HOST_WINDOW_STATUS_OK;
}

static int expect_window_event(const SapWitHostWindowReply* reply,
                               SapWitHostWindowEvent* event_out)
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

static int expect_window_bool(const SapWitHostWindowReply* reply, uint8_t* value_out)
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

static int expect_window_size(const SapWitHostWindowReply* reply,
                              uint32_t* width_out,
                              uint32_t* height_out)
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

static int expect_gpu_status_ok(const SapWitHostGpu2dReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_GPU2D_REPLY_STATUS
        && reply->val.status.case_tag == SAP_WIT_HOST_GPU2D_STATUS_OK;
}

static int expect_surface_ok(const SapWitHostGpu2dReply* reply,
                             SapWitHostGpu2dSurfaceResource* handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_GPU2D_REPLY_SURFACE
            || reply->val.surface.case_tag != SAP_WIT_HOST_GPU2D_SURFACE_RESULT_OK) {
        return 0;
    }
    *handle_out = reply->val.surface.val.ok;
    return 1;
}

static int expect_gpu_caps_ok(const SapWitHostGpu2dReply* reply, uint32_t* caps_out)
{
    if (!reply || !caps_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_GPU2D_REPLY_CAPABILITIES
            || reply->val.capabilities.case_tag != SAP_WIT_HOST_GPU2D_CAPABILITIES_RESULT_OK) {
        return 0;
    }
    *caps_out = reply->val.capabilities.val.ok;
    return 1;
}

static int expect_measure_ok(const SapWitHostGpu2dReply* reply, float* width_out)
{
    if (!reply || !width_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_GPU2D_REPLY_MEASURE
            || reply->val.measure.case_tag != SAP_WIT_HOST_GPU2D_MEASURE_RESULT_OK) {
        return 0;
    }
    *width_out = reply->val.measure.val.ok;
    return 1;
}

static int expect_clock_now(const SapWitHostClockReply* reply, uint64_t* now_out)
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

static int expect_menu_status_ok(const SapWitHostMenuReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_MENU_REPLY_STATUS
        && reply->val.status.case_tag == SAP_WIT_HOST_MENU_STATUS_OK;
}

static int expect_menu_action(const SapWitHostMenuReply* reply, int32_t* action_id_out)
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

static int expect_editor_status_ok(const SapWitHostEditorInputReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_EDITOR_INPUT_REPLY_STATUS
        && reply->val.status.case_tag == SAP_WIT_HOST_EDITOR_INPUT_STATUS_OK;
}

static int expect_editor_action(const SapWitHostEditorInputReply* reply,
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

static int expect_clipboard_status_ok(const SapWitHostClipboardReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_CLIPBOARD_REPLY_STATUS
        && reply->val.status.case_tag == SAP_WIT_HOST_CLIPBOARD_STATUS_OK;
}

static int expect_clipboard_text(const SapWitHostClipboardReply* reply,
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

static int expect_text_handle(const SapWitCommonCoreTextReply* reply,
                              SapWitCommonCoreTextResource* text_out)
{
    if (!reply || !text_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_COMMON_CORE_TEXT_REPLY_TEXT
            || reply->val.text.case_tag != SAP_WIT_COMMON_CORE_TEXT_OP_RESULT_OK) {
        return 0;
    }
    *text_out = reply->val.text.val.ok;
    return 1;
}

static int expect_text_status_ok(const SapWitCommonCoreTextReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_COMMON_CORE_TEXT_REPLY_STATUS
        && reply->val.status.case_tag == SAP_WIT_COMMON_CORE_STATUS_OK;
}

static int expect_text_export_ok(const SapWitCommonCoreTextReply* reply,
                                 const uint8_t** data_out,
                                 uint32_t* len_out)
{
    if (!reply || !data_out || !len_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_COMMON_CORE_TEXT_REPLY_EXPORT
            || reply->val.export.case_tag != SAP_WIT_COMMON_CORE_TEXT_EXPORT_RESULT_OK) {
        return 0;
    }
    *data_out = reply->val.export.val.ok.data;
    *len_out = reply->val.export.val.ok.len;
    return 1;
}

static uint32_t utf8_count_codepoints(const uint8_t* data, uint32_t len)
{
    uint32_t i;
    uint32_t count = 0u;

    for (i = 0u; i < len; ++i) {
        if ((data[i] & 0xC0u) != 0x80u) {
            count++;
        }
    }
    return count;
}

static uint32_t utf8_codepoint_to_byte(const uint8_t* data, uint32_t len, uint32_t offset)
{
    uint32_t i;
    uint32_t count = 0u;

    if (offset == 0u) {
        return 0u;
    }

    for (i = 0u; i < len; ++i) {
        if ((data[i] & 0xC0u) != 0x80u) {
            if (count == offset) {
                return i;
            }
            count++;
        }
    }
    return len;
}

static uint32_t utf8_decode_codepoint_at(const uint8_t* data, uint32_t len, uint32_t offset)
{
    uint32_t byte_index = utf8_codepoint_to_byte(data, len, offset);
    uint8_t c0;

    if (byte_index >= len) {
        return 0u;
    }

    c0 = data[byte_index];
    if ((c0 & 0x80u) == 0u) {
        return c0;
    }
    if ((c0 & 0xE0u) == 0xC0u && byte_index + 1u < len) {
        return ((uint32_t)(c0 & 0x1Fu) << 6u)
             | (uint32_t)(data[byte_index + 1u] & 0x3Fu);
    }
    if ((c0 & 0xF0u) == 0xE0u && byte_index + 2u < len) {
        return ((uint32_t)(c0 & 0x0Fu) << 12u)
             | ((uint32_t)(data[byte_index + 1u] & 0x3Fu) << 6u)
             | (uint32_t)(data[byte_index + 2u] & 0x3Fu);
    }
    if ((c0 & 0xF8u) == 0xF0u && byte_index + 3u < len) {
        return ((uint32_t)(c0 & 0x07u) << 18u)
             | ((uint32_t)(data[byte_index + 1u] & 0x3Fu) << 12u)
             | ((uint32_t)(data[byte_index + 2u] & 0x3Fu) << 6u)
             | (uint32_t)(data[byte_index + 3u] & 0x3Fu);
    }
    return c0;
}

static uint32_t utf8_encode_codepoint(uint32_t codepoint, uint8_t out[4])
{
    if (codepoint <= 0x7Fu) {
        out[0] = (uint8_t)codepoint;
        return 1u;
    }
    if (codepoint <= 0x7FFu) {
        out[0] = (uint8_t)(0xC0u | (codepoint >> 6u));
        out[1] = (uint8_t)(0x80u | (codepoint & 0x3Fu));
        return 2u;
    }
    if (codepoint <= 0xFFFFu) {
        out[0] = (uint8_t)(0xE0u | (codepoint >> 12u));
        out[1] = (uint8_t)(0x80u | ((codepoint >> 6u) & 0x3Fu));
        out[2] = (uint8_t)(0x80u | (codepoint & 0x3Fu));
        return 3u;
    }
    out[0] = (uint8_t)(0xF0u | (codepoint >> 18u));
    out[1] = (uint8_t)(0x80u | ((codepoint >> 12u) & 0x3Fu));
    out[2] = (uint8_t)(0x80u | ((codepoint >> 6u) & 0x3Fu));
    out[3] = (uint8_t)(0x80u | (codepoint & 0x3Fu));
    return 4u;
}

enum {
    CHAR_CLASS_SPACE = 0,
    CHAR_CLASS_WORD = 1,
    CHAR_CLASS_OTHER = 2
};

static int textpad_char_class(uint32_t codepoint)
{
    if (codepoint <= 0x7Fu && isspace((int)codepoint)) {
        return CHAR_CLASS_SPACE;
    }
    if (codepoint <= 0x7Fu && (isalnum((int)codepoint) || codepoint == (uint32_t)'_')) {
        return CHAR_CLASS_WORD;
    }
    return CHAR_CLASS_OTHER;
}

static uint32_t textpad_prev_boundary(const uint8_t* data, uint32_t len, uint32_t offset, uint32_t flags)
{
    uint32_t current = offset;

    if (current == 0u) {
        return 0u;
    }
    if ((flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_WORD) == 0u
            && (flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_WORD_PART) == 0u) {
        return current - 1u;
    }

    while (current > 0u) {
        uint32_t cp = utf8_decode_codepoint_at(data, len, current - 1u);
        if (textpad_char_class(cp) != CHAR_CLASS_SPACE) {
            break;
        }
        current--;
    }
    if (current == 0u) {
        return 0u;
    }

    {
        int cls = textpad_char_class(utf8_decode_codepoint_at(data, len, current - 1u));
        while (current > 0u
                && textpad_char_class(utf8_decode_codepoint_at(data, len, current - 1u)) == cls) {
            current--;
        }
    }
    return current;
}

static uint32_t textpad_next_boundary(const uint8_t* data, uint32_t len, uint32_t count, uint32_t offset, uint32_t flags)
{
    uint32_t current = offset;

    if (current >= count) {
        return count;
    }
    if ((flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_WORD) == 0u
            && (flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_WORD_PART) == 0u) {
        return current + 1u;
    }

    while (current < count) {
        uint32_t cp = utf8_decode_codepoint_at(data, len, current);
        if (textpad_char_class(cp) != CHAR_CLASS_SPACE) {
            break;
        }
        current++;
    }
    if (current >= count) {
        return count;
    }

    {
        int cls = textpad_char_class(utf8_decode_codepoint_at(data, len, current));
        while (current < count
                && textpad_char_class(utf8_decode_codepoint_at(data, len, current)) == cls) {
            current++;
        }
    }
    return current;
}

static int textpad_text_dispatch(textpad_state* state,
                                 const SapWitCommonCoreTextCommand* command,
                                 SapWitCommonCoreTextReply* reply_out)
{
    return croft_wit_text_runtime_dispatch(state->text_runtime, command, reply_out);
}

static int textpad_export(textpad_state* state, croft_wit_owned_bytes* out_bytes)
{
    SapWitCommonCoreTextCommand command = {0};
    SapWitCommonCoreTextReply reply = {0};
    const uint8_t* data = NULL;
    uint32_t len = 0u;

    if (!state || !out_bytes) {
        return -1;
    }
    memset(out_bytes, 0, sizeof(*out_bytes));

    command.case_tag = SAP_WIT_COMMON_CORE_TEXT_COMMAND_EXPORT;
    command.val.export.text = state->text;
    if (textpad_text_dispatch(state, &command, &reply) != 0
            || !expect_text_export_ok(&reply, &data, &len)) {
        croft_wit_text_reply_dispose(&reply);
        return -1;
    }

    if (len > 0u) {
        out_bytes->data = (uint8_t*)malloc((size_t)len);
        if (!out_bytes->data) {
            croft_wit_text_reply_dispose(&reply);
            return -1;
        }
        memcpy(out_bytes->data, data, len);
    }
    out_bytes->len = len;
    croft_wit_text_reply_dispose(&reply);
    return 0;
}

static int textpad_delete_range(textpad_state* state, uint32_t start, uint32_t end)
{
    SapWitCommonCoreTextCommand command = {0};
    SapWitCommonCoreTextReply reply = {0};

    if (!state || start > end || end > state->char_count) {
        return -1;
    }
    if (start == end) {
        return 0;
    }

    command.case_tag = SAP_WIT_COMMON_CORE_TEXT_COMMAND_DELETE;
    command.val.delete.text = state->text;
    command.val.delete.start = start;
    command.val.delete.end = end;
    if (textpad_text_dispatch(state, &command, &reply) != 0
            || !expect_text_status_ok(&reply)) {
        croft_wit_text_reply_dispose(&reply);
        return -1;
    }
    croft_wit_text_reply_dispose(&reply);
    state->char_count -= (end - start);
    state->cursor = start;
    state->anchor = start;
    return 0;
}

static int textpad_insert_utf8(textpad_state* state,
                               uint32_t offset,
                               const uint8_t* data,
                               uint32_t len,
                               uint32_t inserted_codepoints)
{
    SapWitCommonCoreTextCommand command = {0};
    SapWitCommonCoreTextReply reply = {0};

    if (!state || offset > state->char_count) {
        return -1;
    }

    command.case_tag = SAP_WIT_COMMON_CORE_TEXT_COMMAND_INSERT;
    command.val.insert.text = state->text;
    command.val.insert.offset = offset;
    command.val.insert.utf8_data = data;
    command.val.insert.utf8_len = len;
    if (textpad_text_dispatch(state, &command, &reply) != 0
            || !expect_text_status_ok(&reply)) {
        croft_wit_text_reply_dispose(&reply);
        return -1;
    }
    croft_wit_text_reply_dispose(&reply);
    state->char_count += inserted_codepoints;
    state->cursor = offset + inserted_codepoints;
    state->anchor = state->cursor;
    return 0;
}

static int textpad_replace_selection_utf8(textpad_state* state, const uint8_t* data, uint32_t len)
{
    uint32_t start = textpad_min_u32(state->cursor, state->anchor);
    uint32_t end = textpad_max_u32(state->cursor, state->anchor);
    uint32_t inserted = utf8_count_codepoints(data, len);

    if (textpad_delete_range(state, start, end) != 0) {
        return -1;
    }
    return textpad_insert_utf8(state, start, data, len, inserted);
}

static int textpad_replace_range_utf8(textpad_state* state,
                                      uint32_t start,
                                      uint32_t end,
                                      const uint8_t* data,
                                      uint32_t len,
                                      uint32_t next_anchor,
                                      uint32_t next_cursor)
{
    uint32_t inserted = utf8_count_codepoints(data, len);

    if (!state || start > end || end > state->char_count) {
        return -1;
    }
    if (textpad_delete_range(state, start, end) != 0) {
        return -1;
    }
    if (textpad_insert_utf8(state, start, data, len, inserted) != 0) {
        return -1;
    }
    if (next_anchor > state->char_count) {
        next_anchor = state->char_count;
    }
    if (next_cursor > state->char_count) {
        next_cursor = state->char_count;
    }
    state->anchor = next_anchor;
    state->cursor = next_cursor;
    return 0;
}

static int textpad_insert_codepoint(textpad_state* state, uint32_t codepoint)
{
    uint8_t encoded[4];
    uint32_t encoded_len;

    if (codepoint == (uint32_t)'\r' || codepoint == (uint32_t)'\n') {
        codepoint = (uint32_t)' ';
    }
    encoded_len = utf8_encode_codepoint(codepoint, encoded);
    return textpad_replace_selection_utf8(state, encoded, encoded_len);
}

static int textpad_apply_indent_action(textpad_state* state, int outdent)
{
    croft_wit_owned_bytes exported = {0};
    croft_editor_text_model model;
    croft_editor_tab_settings settings;
    croft_editor_tab_edit edit = {0};
    int result = 0;

    if (!state) {
        return 0;
    }

    croft_editor_text_model_init(&model);
    croft_editor_tab_settings_default(&settings);
    if (textpad_export(state, &exported) != 0) {
        goto cleanup;
    }
    if (croft_editor_text_model_set_text(&model,
                                         (const char*)exported.data,
                                         exported.len) != CROFT_EDITOR_OK) {
        goto cleanup;
    }
    if (!croft_editor_command_build_tab_edit(&model,
                                             state->anchor,
                                             state->cursor,
                                             &settings,
                                             outdent,
                                             &edit)) {
        result = 1;
        goto cleanup;
    }
    if (textpad_replace_range_utf8(state,
                                   edit.replace_start_offset,
                                   edit.replace_end_offset,
                                   (const uint8_t*)edit.replacement_utf8,
                                   (uint32_t)edit.replacement_utf8_len,
                                   edit.next_anchor_offset,
                                   edit.next_active_offset) != 0) {
        goto cleanup;
    }

    result = 1;

cleanup:
    croft_editor_tab_edit_dispose(&edit);
    croft_editor_text_model_dispose(&model);
    croft_wit_owned_bytes_dispose(&exported);
    return result;
}

static void textpad_move_cursor(textpad_state* state, uint32_t next, int selecting)
{
    if (!state) {
        return;
    }
    if (next > state->char_count) {
        next = state->char_count;
    }
    if (!selecting) {
        state->anchor = next;
    }
    state->cursor = next;
}

static int textpad_copy_selection(textpad_state* state,
                                  croft_wit_host_clipboard_runtime* clipboard_runtime)
{
    croft_wit_owned_bytes exported = {0};
    SapWitHostClipboardCommand command = {0};
    SapWitHostClipboardReply reply = {0};
    uint32_t start_cp;
    uint32_t end_cp;
    uint32_t start_byte;
    uint32_t end_byte;
    int rc = -1;

    if (!textpad_has_selection(state)) {
        return 0;
    }
    if (textpad_export(state, &exported) != 0) {
        return -1;
    }

    start_cp = textpad_min_u32(state->cursor, state->anchor);
    end_cp = textpad_max_u32(state->cursor, state->anchor);
    start_byte = utf8_codepoint_to_byte(exported.data, exported.len, start_cp);
    end_byte = utf8_codepoint_to_byte(exported.data, exported.len, end_cp);

    command.case_tag = SAP_WIT_HOST_CLIPBOARD_COMMAND_SET_TEXT;
    command.val.set_text.utf8_data = exported.data ? exported.data + start_byte : NULL;
    command.val.set_text.utf8_len = end_byte - start_byte;
    if (croft_wit_host_clipboard_runtime_dispatch(clipboard_runtime, &command, &reply) == 0
            && expect_clipboard_status_ok(&reply)) {
        rc = 0;
    }
    croft_wit_host_clipboard_reply_dispose(&reply);
    croft_wit_owned_bytes_dispose(&exported);
    return rc;
}

static int textpad_paste(textpad_state* state, croft_wit_host_clipboard_runtime* clipboard_runtime)
{
    SapWitHostClipboardCommand command = {0};
    SapWitHostClipboardReply reply = {0};
    const uint8_t* data = NULL;
    uint32_t len = 0u;
    int status;
    int rc = -1;

    command.case_tag = SAP_WIT_HOST_CLIPBOARD_COMMAND_GET_TEXT;
    if (croft_wit_host_clipboard_runtime_dispatch(clipboard_runtime, &command, &reply) != 0) {
        return -1;
    }
    status = expect_clipboard_text(&reply, &data, &len);
    if (status == 0) {
        rc = 0;
    } else if (status > 0) {
        rc = textpad_replace_selection_utf8(state, data, len);
    }
    croft_wit_host_clipboard_reply_dispose(&reply);
    return rc;
}

static int textpad_measure_text(croft_wit_host_gpu2d_runtime* gpu_runtime,
                                SapWitHostGpu2dSurfaceResource surface,
                                const uint8_t* data,
                                uint32_t len,
                                float font_size,
                                float* width_out)
{
    SapWitHostGpu2dCommand gpu_cmd = {0};
    SapWitHostGpu2dReply gpu_reply = {0};

    gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_MEASURE_TEXT;
    gpu_cmd.val.measure_text.surface = surface;
    gpu_cmd.val.measure_text.utf8_data = data;
    gpu_cmd.val.measure_text.utf8_len = len;
    gpu_cmd.val.measure_text.font_size = font_size;
    if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
            || !expect_measure_ok(&gpu_reply, width_out)) {
        return -1;
    }
    return 0;
}

static int apply_menu_command(croft_wit_host_menu_runtime* runtime,
                              SapWitHostMenuCommand* command,
                              SapWitHostMenuReply* reply)
{
    return croft_wit_host_menu_runtime_dispatch(runtime, command, reply) == 0
        && expect_menu_status_ok(reply);
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

static int install_textpad_menu(croft_wit_host_menu_runtime* runtime)
{
    SapWitHostMenuCommand command = {0};
    SapWitHostMenuReply reply = {0};

    command.case_tag = SAP_WIT_HOST_MENU_COMMAND_BEGIN_UPDATE;
    if (!apply_menu_command(runtime, &command, &reply)) {
        return 0;
    }

    if (!menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_APP_ROOT, -1, "App", NULL, 0u)
            || !menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_EDIT_ROOT, -1, "Edit", NULL, 0u)
            || !menu_add_item(runtime, &reply, CROFT_EDITOR_MENU_QUIT, CROFT_EDITOR_MENU_APP_ROOT,
                              "Quit Croft", "q", SAP_WIT_HOST_MENU_MODIFIERS_CMD)
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
                              "Outdent Line", "[", SAP_WIT_HOST_MENU_MODIFIERS_CMD)) {
        return 0;
    }

    command.case_tag = SAP_WIT_HOST_MENU_COMMAND_COMMIT_UPDATE;
    return apply_menu_command(runtime, &command, &reply);
}

static int textpad_apply_action(textpad_state* state,
                                croft_wit_host_clipboard_runtime* clipboard_runtime,
                                const SapWitHostEditorInputEditorAction* action)
{
    croft_wit_owned_bytes exported = {0};
    uint32_t selecting = 0u;
    uint32_t next = 0u;

    if (!state || !action) {
        return 0;
    }

    switch (action->case_tag) {
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_LEFT:
            selecting = (action->val.move_left.flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_SELECTING) != 0u;
            if (textpad_export(state, &exported) != 0) {
                return 0;
            }
            next = textpad_prev_boundary(exported.data,
                                         exported.len,
                                         state->cursor,
                                         action->val.move_left.flags);
            croft_wit_owned_bytes_dispose(&exported);
            textpad_move_cursor(state, next, selecting);
            return 1;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_RIGHT:
            selecting = (action->val.move_right.flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_SELECTING) != 0u;
            if (textpad_export(state, &exported) != 0) {
                return 0;
            }
            next = textpad_next_boundary(exported.data,
                                         exported.len,
                                         state->char_count,
                                         state->cursor,
                                         action->val.move_right.flags);
            croft_wit_owned_bytes_dispose(&exported);
            textpad_move_cursor(state, next, selecting);
            return 1;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_HOME:
            selecting = (action->val.move_home.flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_SELECTING) != 0u;
            textpad_move_cursor(state, 0u, selecting);
            return 1;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_END:
            selecting = (action->val.move_end.flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_SELECTING) != 0u;
            textpad_move_cursor(state, state->char_count, selecting);
            return 1;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_UP:
            selecting = (action->val.move_up.flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_SELECTING) != 0u;
            textpad_move_cursor(state, 0u, selecting);
            return 1;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_DOWN:
            selecting = (action->val.move_down.flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_SELECTING) != 0u;
            textpad_move_cursor(state, state->char_count, selecting);
            return 1;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_DELETE_LEFT:
            if (textpad_has_selection(state)) {
                return textpad_delete_range(state,
                                            textpad_min_u32(state->cursor, state->anchor),
                                            textpad_max_u32(state->cursor, state->anchor)) == 0;
            }
            if (textpad_export(state, &exported) != 0) {
                return 0;
            }
            next = textpad_prev_boundary(exported.data,
                                         exported.len,
                                         state->cursor,
                                         action->val.delete_left.flags);
            croft_wit_owned_bytes_dispose(&exported);
            return textpad_delete_range(state, next, state->cursor) == 0;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_DELETE_RIGHT:
            if (textpad_has_selection(state)) {
                return textpad_delete_range(state,
                                            textpad_min_u32(state->cursor, state->anchor),
                                            textpad_max_u32(state->cursor, state->anchor)) == 0;
            }
            if (textpad_export(state, &exported) != 0) {
                return 0;
            }
            next = textpad_next_boundary(exported.data,
                                         exported.len,
                                         state->char_count,
                                         state->cursor,
                                         action->val.delete_right.flags);
            croft_wit_owned_bytes_dispose(&exported);
            return textpad_delete_range(state, state->cursor, next) == 0;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_INDENT:
            return textpad_apply_indent_action(state, 0);
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_OUTDENT:
            return textpad_apply_indent_action(state, 1);
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_INSERT_CODEPOINT:
            return textpad_insert_codepoint(state, action->val.insert_codepoint) == 0;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_SELECT_ALL:
            state->anchor = 0u;
            state->cursor = state->char_count;
            return 1;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_COPY:
            return textpad_copy_selection(state, clipboard_runtime) == 0;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_CUT:
            if (textpad_copy_selection(state, clipboard_runtime) != 0) {
                return 0;
            }
            return textpad_delete_range(state,
                                        textpad_min_u32(state->cursor, state->anchor),
                                        textpad_max_u32(state->cursor, state->anchor)) == 0;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_PASTE:
            return textpad_paste(state, clipboard_runtime) == 0;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_QUIT:
            state->running = 0;
            return 1;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_UNDO:
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_REDO:
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_SAVE:
            return 1;
        default:
            return 0;
    }
}

static int textpad_pump_actions(textpad_state* state,
                                croft_wit_host_editor_input_runtime* editor_input_runtime,
                                croft_wit_host_clipboard_runtime* clipboard_runtime)
{
    SapWitHostEditorInputCommand input_command = {0};
    SapWitHostEditorInputReply input_reply = {0};

    for (;;) {
        SapWitHostEditorInputEditorAction action = {0};
        int status;

        input_command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_NEXT_ACTION;
        if (croft_wit_host_editor_input_runtime_dispatch(editor_input_runtime,
                                                         &input_command,
                                                         &input_reply) != 0) {
            return 0;
        }
        status = expect_editor_action(&input_reply, &action);
        if (status < 0) {
            return 0;
        }
        if (status == 0) {
            return 1;
        }
        if (!textpad_apply_action(state, clipboard_runtime, &action)) {
            return 0;
        }
    }
}

int main(void)
{
    const char* base = "small binaries";
    const char* prefix = "Big analysis, ";
    const char* title = "Croft WIT Textpad Window";
    const char* auto_close_env = getenv("CROFT_WIT_TEXTPAD_AUTO_CLOSE_MS");
    croft_wit_text_runtime* text_runtime = NULL;
    croft_wit_host_window_runtime* window_runtime = NULL;
    croft_wit_host_gpu2d_runtime* gpu_runtime = NULL;
    croft_wit_host_clock_runtime* clock_runtime = NULL;
    croft_wit_host_menu_runtime* menu_runtime = NULL;
    croft_wit_host_clipboard_runtime* clipboard_runtime = NULL;
    croft_wit_host_editor_input_runtime* editor_input_runtime = NULL;
    croft_wit_text_program_host text_host = {0};
    croft_wit_owned_bytes initial = {0};
    textpad_state state = {0};
    SapWitHostWindowResource window = SAP_WIT_HOST_WINDOW_RESOURCE_INVALID;
    SapWitHostGpu2dSurfaceResource surface = SAP_WIT_HOST_GPU2D_SURFACE_RESOURCE_INVALID;
    SapWitHostWindowCommand window_cmd = {0};
    SapWitHostWindowReply window_reply = {0};
    SapWitHostGpu2dCommand gpu_cmd = {0};
    SapWitHostGpu2dReply gpu_reply = {0};
    SapWitHostClockCommand clock_cmd = {0};
    SapWitHostClockReply clock_reply = {0};
    uint32_t caps = 0u;
    uint32_t auto_close_ms = 500u;
    uint64_t start_ms = 0u;
    uint64_t end_ms = 0u;
    int rc = 1;

    if (auto_close_env && auto_close_env[0] != '\0') {
        int parsed = atoi(auto_close_env);
        if (parsed > 0) {
            auto_close_ms = (uint32_t)parsed;
        }
    }

    text_runtime = croft_wit_text_runtime_create(NULL);
    window_runtime = croft_wit_host_window_runtime_create();
    gpu_runtime = croft_wit_host_gpu2d_runtime_create();
    clock_runtime = croft_wit_host_clock_runtime_create();
    menu_runtime = croft_wit_host_menu_runtime_create();
    clipboard_runtime = croft_wit_host_clipboard_runtime_create();
    editor_input_runtime = croft_wit_host_editor_input_runtime_create();
    if (!text_runtime || !window_runtime || !gpu_runtime || !clock_runtime
            || !menu_runtime || !clipboard_runtime || !editor_input_runtime) {
        goto cleanup;
    }

    state.text_runtime = text_runtime;
    state.text = SAP_WIT_COMMON_CORE_TEXT_RESOURCE_INVALID;
    state.running = 1;

    text_host.userdata = text_runtime;
    text_host.dispatch = (croft_wit_text_program_dispatch_fn)croft_wit_text_runtime_dispatch;
    text_host.dispose_reply = croft_wit_text_reply_dispose;
    if (croft_wit_text_program_prepend(&text_host,
                                       (const uint8_t*)base,
                                       (uint32_t)strlen(base),
                                       (const uint8_t*)prefix,
                                       (uint32_t)strlen(prefix),
                                       &initial) != 0) {
        goto cleanup;
    }

    {
        SapWitCommonCoreTextCommand text_command = {0};
        SapWitCommonCoreTextReply text_reply = {0};
        text_command.case_tag = SAP_WIT_COMMON_CORE_TEXT_COMMAND_OPEN;
        text_command.val.open.initial_data = initial.data;
        text_command.val.open.initial_len = initial.len;
        if (textpad_text_dispatch(&state, &text_command, &text_reply) != 0
                || !expect_text_handle(&text_reply, &state.text)) {
            croft_wit_text_reply_dispose(&text_reply);
            goto cleanup;
        }
        croft_wit_text_reply_dispose(&text_reply);
        state.char_count = utf8_count_codepoints(initial.data, initial.len);
        state.cursor = state.char_count;
        state.anchor = state.cursor;
    }

    window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_OPEN;
    window_cmd.val.open.width = 960u;
    window_cmd.val.open.height = 320u;
    window_cmd.val.open.title_data = (const uint8_t*)title;
    window_cmd.val.open.title_len = (uint32_t)strlen(title);
    if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0
            || !expect_window_ok(&window_reply, &window)) {
        goto cleanup;
    }

    if (!install_textpad_menu(menu_runtime)) {
        goto cleanup;
    }

    gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_CAPABILITIES;
    if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
            || !expect_gpu_caps_ok(&gpu_reply, &caps)) {
        goto cleanup;
    }
    if ((caps & SAP_WIT_HOST_GPU2D_CAPABILITIES_TEXT) == 0u
            || (caps & SAP_WIT_HOST_GPU2D_CAPABILITIES_PRESENT) == 0u) {
        goto cleanup;
    }

    gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_OPEN;
    if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
            || !expect_surface_ok(&gpu_reply, &surface)) {
        goto cleanup;
    }

    clock_cmd.case_tag = SAP_WIT_HOST_CLOCK_COMMAND_MONOTONIC_NOW;
    if (croft_wit_host_clock_runtime_dispatch(clock_runtime, &clock_cmd, &clock_reply) != 0
            || !expect_clock_now(&clock_reply, &start_ms)) {
        goto cleanup;
    }

    while (state.running) {
        uint8_t should_close = 0u;
        uint64_t now_ms = 0u;
        uint32_t width = 0u;
        uint32_t height = 0u;
        croft_wit_owned_bytes exported = {0};
        uint32_t sel_start_cp;
        uint32_t sel_end_cp;
        uint32_t cursor_byte;
        uint32_t sel_start_byte;
        uint32_t sel_end_byte;
        float cursor_x = 104.0f;
        float selection_x = 104.0f;
        float selection_w = 0.0f;

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_POLL;
        window_cmd.val.poll.window = window;
        if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0
                || !expect_window_status_ok(&window_reply)) {
            goto cleanup;
        }

        for (;;) {
            SapWitHostWindowEvent event = {0};
            int status;

            window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_NEXT_EVENT;
            window_cmd.val.next_event.window = window;
            if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0) {
                goto cleanup;
            }
            status = expect_window_event(&window_reply, &event);
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
                    input_command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_WINDOW_KEY;
                    input_command.val.window_key.key = event.val.key.key;
                    input_command.val.window_key.action = event.val.key.action;
                    input_command.val.window_key.modifiers = event.val.key.modifiers;
                    if (croft_wit_host_editor_input_runtime_dispatch(editor_input_runtime,
                                                                     &input_command,
                                                                     &input_reply) != 0
                            || !expect_editor_status_ok(&input_reply)) {
                        goto cleanup;
                    }
                    if (event.val.key.key == 256 && event.val.key.action == 1) {
                        state.running = 0;
                    }
                    break;
                }
                case SAP_WIT_HOST_WINDOW_EVENT_CHAR_EVENT: {
                    SapWitHostEditorInputCommand input_command = {0};
                    SapWitHostEditorInputReply input_reply = {0};
                    input_command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_WINDOW_CHAR;
                    input_command.val.window_char.codepoint = event.val.char_event.codepoint;
                    if (croft_wit_host_editor_input_runtime_dispatch(editor_input_runtime,
                                                                     &input_command,
                                                                     &input_reply) != 0
                            || !expect_editor_status_ok(&input_reply)) {
                        goto cleanup;
                    }
                    break;
                }
                default:
                    break;
            }
        }

        for (;;) {
            SapWitHostMenuCommand menu_command = {0};
            SapWitHostMenuReply menu_reply = {0};
            SapWitHostEditorInputCommand input_command = {0};
            SapWitHostEditorInputReply input_reply = {0};
            int32_t action_id = 0;
            int status;

            menu_command.case_tag = SAP_WIT_HOST_MENU_COMMAND_NEXT_ACTION;
            if (croft_wit_host_menu_runtime_dispatch(menu_runtime, &menu_command, &menu_reply) != 0) {
                goto cleanup;
            }
            status = expect_menu_action(&menu_reply, &action_id);
            if (status < 0) {
                goto cleanup;
            }
            if (status == 0) {
                break;
            }

            input_command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_MENU_ACTION;
            input_command.val.menu_action.action_id = action_id;
            if (croft_wit_host_editor_input_runtime_dispatch(editor_input_runtime,
                                                             &input_command,
                                                             &input_reply) != 0
                    || !expect_editor_status_ok(&input_reply)) {
                goto cleanup;
            }
        }

        if (!textpad_pump_actions(&state, editor_input_runtime, clipboard_runtime)) {
            goto cleanup;
        }

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_SHOULD_CLOSE;
        window_cmd.val.should_close.window = window;
        if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0
                || !expect_window_bool(&window_reply, &should_close)) {
            goto cleanup;
        }
        if (should_close) {
            break;
        }

        if (croft_wit_host_clock_runtime_dispatch(clock_runtime, &clock_cmd, &clock_reply) != 0
                || !expect_clock_now(&clock_reply, &now_ms)) {
            goto cleanup;
        }
        if (now_ms - start_ms >= (uint64_t)auto_close_ms) {
            break;
        }

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_FRAMEBUFFER_SIZE;
        window_cmd.val.framebuffer_size.window = window;
        if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0
                || !expect_window_size(&window_reply, &width, &height)) {
            goto cleanup;
        }

        if (textpad_export(&state, &exported) != 0) {
            goto cleanup;
        }

        sel_start_cp = textpad_min_u32(state.cursor, state.anchor);
        sel_end_cp = textpad_max_u32(state.cursor, state.anchor);
        cursor_byte = utf8_codepoint_to_byte(exported.data, exported.len, state.cursor);
        sel_start_byte = utf8_codepoint_to_byte(exported.data, exported.len, sel_start_cp);
        sel_end_byte = utf8_codepoint_to_byte(exported.data, exported.len, sel_end_cp);

        if (sel_start_byte > 0u
                && textpad_measure_text(gpu_runtime, surface,
                                        exported.data, sel_start_byte,
                                        30.0f, &selection_x) != 0) {
            goto cleanup;
        }
        selection_x += 104.0f;

        if (cursor_byte > 0u
                && textpad_measure_text(gpu_runtime, surface,
                                        exported.data, cursor_byte,
                                        30.0f, &cursor_x) != 0) {
            goto cleanup;
        }
        cursor_x += 104.0f;

        if (sel_end_byte > sel_start_byte
                && textpad_measure_text(gpu_runtime, surface,
                                        exported.data + sel_start_byte,
                                        sel_end_byte - sel_start_byte,
                                        30.0f, &selection_w) != 0) {
            goto cleanup;
        }

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_BEGIN_FRAME;
        gpu_cmd.val.begin_frame.surface = surface;
        gpu_cmd.val.begin_frame.width = width;
        gpu_cmd.val.begin_frame.height = height;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            goto cleanup;
        }

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_CLEAR;
        gpu_cmd.val.clear.surface = surface;
        gpu_cmd.val.clear.color_rgba = 0xF7FAFCFF;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            goto cleanup;
        }

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_DRAW_RECT;
        gpu_cmd.val.draw_rect.surface = surface;
        gpu_cmd.val.draw_rect.x = 64.0f;
        gpu_cmd.val.draw_rect.y = 64.0f;
        gpu_cmd.val.draw_rect.w = (float)width - 128.0f;
        gpu_cmd.val.draw_rect.h = 164.0f;
        gpu_cmd.val.draw_rect.color_rgba = 0xD9E2ECFF;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            goto cleanup;
        }

        gpu_cmd.val.draw_rect.x = 80.0f;
        gpu_cmd.val.draw_rect.y = 96.0f;
        gpu_cmd.val.draw_rect.w = (float)width - 160.0f;
        gpu_cmd.val.draw_rect.h = 76.0f;
        gpu_cmd.val.draw_rect.color_rgba = 0xFFFFFFFF;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            goto cleanup;
        }

        if (selection_w > 0.0f) {
            gpu_cmd.val.draw_rect.x = selection_x;
            gpu_cmd.val.draw_rect.y = 110.0f;
            gpu_cmd.val.draw_rect.w = selection_w;
            gpu_cmd.val.draw_rect.h = 36.0f;
            gpu_cmd.val.draw_rect.color_rgba = 0xB3D4FFFF;
            if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                    || !expect_gpu_status_ok(&gpu_reply)) {
                goto cleanup;
            }
        }

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_DRAW_TEXT;
        gpu_cmd.val.draw_text.surface = surface;
        gpu_cmd.val.draw_text.x = 104.0f;
        gpu_cmd.val.draw_text.y = 136.0f;
        gpu_cmd.val.draw_text.utf8_data = exported.data;
        gpu_cmd.val.draw_text.utf8_len = exported.len;
        gpu_cmd.val.draw_text.font_size = 30.0f;
        gpu_cmd.val.draw_text.color_rgba = 0x102A43FF;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            goto cleanup;
        }

        if (!textpad_has_selection(&state)) {
            gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_DRAW_RECT;
            gpu_cmd.val.draw_rect.surface = surface;
            gpu_cmd.val.draw_rect.x = cursor_x;
            gpu_cmd.val.draw_rect.y = 108.0f;
            gpu_cmd.val.draw_rect.w = 2.0f;
            gpu_cmd.val.draw_rect.h = 40.0f;
            gpu_cmd.val.draw_rect.color_rgba = 0x1F2933FF;
            if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                    || !expect_gpu_status_ok(&gpu_reply)) {
                goto cleanup;
            }
        }

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_DRAW_TEXT;
        gpu_cmd.val.draw_text.surface = surface;
        gpu_cmd.val.draw_text.x = 80.0f;
        gpu_cmd.val.draw_text.y = 208.0f;
        gpu_cmd.val.draw_text.utf8_data = (const uint8_t*)
            "Cmd/Ctrl-C/V/X/A and arrows work here through WIT mix-ins.";
        gpu_cmd.val.draw_text.utf8_len = (uint32_t)strlen(
            "Cmd/Ctrl-C/V/X/A and arrows work here through WIT mix-ins.");
        gpu_cmd.val.draw_text.font_size = 18.0f;
        gpu_cmd.val.draw_text.color_rgba = 0x486581FF;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            goto cleanup;
        }

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_END_FRAME;
        gpu_cmd.val.end_frame.surface = surface;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            goto cleanup;
        }

        croft_wit_owned_bytes_dispose(&exported);
        state.frame_count++;
    }

    {
        croft_wit_owned_bytes exported = {0};
        if (textpad_export(&state, &exported) == 0) {
            end_ms = start_ms;
            if (croft_wit_host_clock_runtime_dispatch(clock_runtime, &clock_cmd, &clock_reply) == 0
                    && expect_clock_now(&clock_reply, &end_ms)) {
            }

            printf("textpad=\"%.*s\" frames=%u chars=%u wall_ms=%llu\n",
                   (int)exported.len,
                   exported.data ? (const char*)exported.data : "",
                   state.frame_count,
                   state.char_count,
                   (unsigned long long)(end_ms - start_ms));
            fflush(stdout);
            croft_wit_owned_bytes_dispose(&exported);
        }
    }
    rc = 0;

cleanup:
    if (state.text != SAP_WIT_COMMON_CORE_TEXT_RESOURCE_INVALID) {
        SapWitCommonCoreTextCommand text_command = {0};
        SapWitCommonCoreTextReply text_reply = {0};
        text_command.case_tag = SAP_WIT_COMMON_CORE_TEXT_COMMAND_DROP;
        text_command.val.drop.text = state.text;
        if (textpad_text_dispatch(&state, &text_command, &text_reply) == 0) {
            croft_wit_text_reply_dispose(&text_reply);
        }
    }
    if (surface != SAP_WIT_HOST_GPU2D_SURFACE_RESOURCE_INVALID) {
        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_DROP;
        gpu_cmd.val.drop.surface = surface;
        croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply);
    }
    if (window != SAP_WIT_HOST_WINDOW_RESOURCE_INVALID) {
        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_CLOSE;
        window_cmd.val.close.window = window;
        croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply);
    }

    croft_wit_owned_bytes_dispose(&initial);
    croft_wit_host_editor_input_runtime_destroy(editor_input_runtime);
    croft_wit_host_clipboard_runtime_destroy(clipboard_runtime);
    croft_wit_host_menu_runtime_destroy(menu_runtime);
    croft_wit_host_clock_runtime_destroy(clock_runtime);
    croft_wit_host_gpu2d_runtime_destroy(gpu_runtime);
    croft_wit_host_window_runtime_destroy(window_runtime);
    croft_wit_text_runtime_destroy(text_runtime);
    return rc;
}
