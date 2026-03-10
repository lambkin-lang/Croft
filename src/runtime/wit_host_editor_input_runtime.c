#include "croft/wit_host_editor_input_runtime.h"

#include "croft/editor_menu_ids.h"
#include "croft/host_ui.h"

#include <stdlib.h>
#include <string.h>

#define CROFT_WIT_EDITOR_ACTION_CAP 128u

enum {
    CROFT_KEY_RELEASE = 0,
    CROFT_KEY_TAB = 258,
    CROFT_KEY_BACKSPACE = 259,
    CROFT_KEY_DELETE = 261,
    CROFT_KEY_RIGHT = 262,
    CROFT_KEY_LEFT = 263,
    CROFT_KEY_DOWN = 264,
    CROFT_KEY_UP = 265,
    CROFT_KEY_HOME = 268,
    CROFT_KEY_END = 269,
    CROFT_KEY_ENTER = 257,
    CROFT_KEY_A = 65,
    CROFT_KEY_C = 67,
    CROFT_KEY_LEFT_BRACKET = 91,
    CROFT_KEY_Q = 81,
    CROFT_KEY_RIGHT_BRACKET = 93,
    CROFT_KEY_S = 83,
    CROFT_KEY_V = 86,
    CROFT_KEY_X = 88,
    CROFT_KEY_Y = 89,
    CROFT_KEY_Z = 90,
    CROFT_KEY_KP_ENTER_OLD = 284,
    CROFT_KEY_KP_ENTER = 335
};

struct croft_wit_host_editor_input_runtime {
    SapWitHostEditorInputEditorAction actions[CROFT_WIT_EDITOR_ACTION_CAP];
    uint32_t action_head;
    uint32_t action_count;
    uint8_t suppress_tab_char;
};

static void croft_wit_host_editor_input_reply_zero(SapWitHostEditorInputReply* reply)
{
    sap_wit_zero_host_editor_input_reply(reply);
}

static void croft_wit_set_string_view(const char* text,
                                      const uint8_t** data_out,
                                      uint32_t* len_out)
{
    if (!data_out || !len_out) {
        return;
    }
    if (!text) {
        text = "";
    }
    *data_out = (const uint8_t*)text;
    *len_out = (uint32_t)strlen(text);
}

static void croft_wit_host_editor_input_reply_status_ok(SapWitHostEditorInputReply* reply)
{
    croft_wit_host_editor_input_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_EDITOR_INPUT_REPLY_STATUS;
    reply->val.status.is_v_ok = 1u;
}

static void croft_wit_host_editor_input_reply_status_err(SapWitHostEditorInputReply* reply, const char* err)
{
    croft_wit_host_editor_input_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_EDITOR_INPUT_REPLY_STATUS;
    reply->val.status.is_v_ok = 0u;
    croft_wit_set_string_view(err,
                              &reply->val.status.v_val.err.v_data,
                              &reply->val.status.v_val.err.v_len);
}

static void croft_wit_host_editor_input_reply_action_ok(SapWitHostEditorInputReply* reply,
                                                        const SapWitHostEditorInputEditorAction* action)
{
    croft_wit_host_editor_input_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_EDITOR_INPUT_REPLY_ACTION;
    reply->val.action.is_v_ok = 1u;
    reply->val.action.v_val.ok.has_v = 1u;
    reply->val.action.v_val.ok.v = *action;
}

static void croft_wit_host_editor_input_reply_action_empty(SapWitHostEditorInputReply* reply)
{
    croft_wit_host_editor_input_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_EDITOR_INPUT_REPLY_ACTION;
    reply->val.action.is_v_ok = 1u;
    reply->val.action.v_val.ok.has_v = 0u;
}

static void croft_wit_host_editor_input_enqueue(croft_wit_host_editor_input_runtime* runtime,
                                                const SapWitHostEditorInputEditorAction* action)
{
    uint32_t slot;

    if (!runtime || !action || runtime->action_count >= CROFT_WIT_EDITOR_ACTION_CAP) {
        return;
    }
    slot = (runtime->action_head + runtime->action_count) % CROFT_WIT_EDITOR_ACTION_CAP;
    runtime->actions[slot] = *action;
    runtime->action_count++;
}

static uint32_t croft_wit_host_editor_input_motion_flags(uint32_t modifiers)
{
    uint32_t flags = 0u;
    int word_part_mode = (modifiers & CROFT_UI_MOD_ALT) != 0u
        && (modifiers & CROFT_UI_MOD_CONTROL) != 0u;
    int word_mode = !word_part_mode
        && (modifiers & (CROFT_UI_MOD_ALT | CROFT_UI_MOD_CONTROL)) != 0u;

    if (modifiers & CROFT_UI_MOD_SHIFT) {
        flags |= SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_SELECTING;
    }
    if (word_part_mode) {
        flags |= SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_WORD_PART;
    } else if (word_mode) {
        flags |= SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_WORD;
    }
    return flags;
}

static uint32_t croft_wit_host_editor_input_delete_flags(uint32_t modifiers)
{
    uint32_t flags = 0u;
    int word_part_mode = (modifiers & CROFT_UI_MOD_ALT) != 0u
        && (modifiers & CROFT_UI_MOD_CONTROL) != 0u;
    int word_mode = !word_part_mode
        && (modifiers & (CROFT_UI_MOD_ALT | CROFT_UI_MOD_CONTROL)) != 0u;

    if (word_part_mode) {
        flags |= SAP_WIT_HOST_EDITOR_INPUT_DELETE_FLAGS_WORD_PART;
    } else if (word_mode) {
        flags |= SAP_WIT_HOST_EDITOR_INPUT_DELETE_FLAGS_WORD;
    }
    return flags;
}

static void croft_wit_host_editor_input_enqueue_motion(
    croft_wit_host_editor_input_runtime* runtime,
    uint8_t case_tag,
    uint32_t flags)
{
    SapWitHostEditorInputEditorAction action = {0};
    action.case_tag = case_tag;
    switch (case_tag) {
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_LEFT:
            action.val.move_left.flags = flags;
            break;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_RIGHT:
            action.val.move_right.flags = flags;
            break;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_UP:
            action.val.move_up.flags = flags;
            break;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_DOWN:
            action.val.move_down.flags = flags;
            break;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_HOME:
            action.val.move_home.flags = flags;
            break;
        case SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_END:
            action.val.move_end.flags = flags;
            break;
        default:
            return;
    }
    croft_wit_host_editor_input_enqueue(runtime, &action);
}

static void croft_wit_host_editor_input_enqueue_delete(
    croft_wit_host_editor_input_runtime* runtime,
    uint8_t case_tag,
    uint32_t flags)
{
    SapWitHostEditorInputEditorAction action = {0};
    action.case_tag = case_tag;
    if (case_tag == SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_DELETE_LEFT) {
        action.val.delete_left.flags = flags;
    } else if (case_tag == SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_DELETE_RIGHT) {
        action.val.delete_right.flags = flags;
    } else {
        return;
    }
    croft_wit_host_editor_input_enqueue(runtime, &action);
}

static void croft_wit_host_editor_input_enqueue_simple(
    croft_wit_host_editor_input_runtime* runtime,
    uint8_t case_tag)
{
    SapWitHostEditorInputEditorAction action = {0};
    action.case_tag = case_tag;
    croft_wit_host_editor_input_enqueue(runtime, &action);
}

croft_wit_host_editor_input_runtime* croft_wit_host_editor_input_runtime_create(void)
{
    return (croft_wit_host_editor_input_runtime*)calloc(1u, sizeof(croft_wit_host_editor_input_runtime));
}

void croft_wit_host_editor_input_runtime_destroy(croft_wit_host_editor_input_runtime* runtime)
{
    free(runtime);
}

static void croft_wit_host_editor_input_translate_key(croft_wit_host_editor_input_runtime* runtime,
                                                      const SapWitHostEditorInputKeyEvent* key)
{
    int command_mode;
    uint32_t motion_flags;
    uint32_t delete_flags;

    if (!runtime || !key || key->action == CROFT_KEY_RELEASE) {
        return;
    }

    command_mode = (key->modifiers & (CROFT_UI_MOD_SUPER | CROFT_UI_MOD_CONTROL)) != 0u;
    motion_flags = croft_wit_host_editor_input_motion_flags(key->modifiers);
    delete_flags = croft_wit_host_editor_input_delete_flags(key->modifiers);

    if (command_mode) {
        switch (key->key) {
            case CROFT_KEY_A:
                croft_wit_host_editor_input_enqueue_simple(runtime,
                    SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_SELECT_ALL);
                return;
            case CROFT_KEY_C:
                croft_wit_host_editor_input_enqueue_simple(runtime,
                    SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_COPY);
                return;
            case CROFT_KEY_LEFT_BRACKET:
                croft_wit_host_editor_input_enqueue_simple(runtime,
                    (key->modifiers & CROFT_UI_MOD_ALT)
                        ? SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_FOLD
                        : SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_OUTDENT);
                return;
            case CROFT_KEY_Q:
                croft_wit_host_editor_input_enqueue_simple(runtime,
                    SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_QUIT);
                return;
            case CROFT_KEY_RIGHT_BRACKET:
                croft_wit_host_editor_input_enqueue_simple(runtime,
                    (key->modifiers & CROFT_UI_MOD_ALT)
                        ? SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_UNFOLD
                        : SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_INDENT);
                return;
            case CROFT_KEY_S:
                croft_wit_host_editor_input_enqueue_simple(runtime,
                    SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_SAVE);
                return;
            case CROFT_KEY_V:
                croft_wit_host_editor_input_enqueue_simple(runtime,
                    SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_PASTE);
                return;
            case CROFT_KEY_X:
                croft_wit_host_editor_input_enqueue_simple(runtime,
                    SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_CUT);
                return;
            case CROFT_KEY_Y:
                croft_wit_host_editor_input_enqueue_simple(runtime,
                    SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_REDO);
                return;
            case CROFT_KEY_Z:
                croft_wit_host_editor_input_enqueue_simple(runtime,
                    (key->modifiers & CROFT_UI_MOD_SHIFT)
                        ? SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_REDO
                        : SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_UNDO);
                return;
            default:
                break;
        }
    }

    switch (key->key) {
        case CROFT_KEY_TAB:
            croft_wit_host_editor_input_enqueue_simple(runtime,
                (key->modifiers & CROFT_UI_MOD_SHIFT)
                    ? SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_OUTDENT
                    : SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_INDENT);
            runtime->suppress_tab_char = 1u;
            break;
        case CROFT_KEY_LEFT:
            croft_wit_host_editor_input_enqueue_motion(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_LEFT, motion_flags);
            break;
        case CROFT_KEY_RIGHT:
            croft_wit_host_editor_input_enqueue_motion(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_RIGHT, motion_flags);
            break;
        case CROFT_KEY_UP:
            croft_wit_host_editor_input_enqueue_motion(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_UP, motion_flags);
            break;
        case CROFT_KEY_DOWN:
            croft_wit_host_editor_input_enqueue_motion(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_DOWN, motion_flags);
            break;
        case CROFT_KEY_HOME:
            croft_wit_host_editor_input_enqueue_motion(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_HOME, motion_flags);
            break;
        case CROFT_KEY_END:
            croft_wit_host_editor_input_enqueue_motion(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_END, motion_flags);
            break;
        case CROFT_KEY_BACKSPACE:
            croft_wit_host_editor_input_enqueue_delete(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_DELETE_LEFT, delete_flags);
            break;
        case CROFT_KEY_DELETE:
            croft_wit_host_editor_input_enqueue_delete(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_DELETE_RIGHT, delete_flags);
            break;
        case CROFT_KEY_ENTER:
        case CROFT_KEY_KP_ENTER_OLD:
        case CROFT_KEY_KP_ENTER: {
            SapWitHostEditorInputEditorAction action = {0};
            action.case_tag = SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_INSERT_CODEPOINT;
            action.val.insert_codepoint = (uint32_t)'\n';
            croft_wit_host_editor_input_enqueue(runtime, &action);
            break;
        }
        default:
            break;
    }
}

static void croft_wit_host_editor_input_translate_menu(croft_wit_host_editor_input_runtime* runtime,
                                                       int32_t action_id)
{
    switch (action_id) {
        case CROFT_EDITOR_MENU_SAVE:
            croft_wit_host_editor_input_enqueue_simple(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_SAVE);
            break;
        case CROFT_EDITOR_MENU_QUIT:
            croft_wit_host_editor_input_enqueue_simple(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_QUIT);
            break;
        case CROFT_EDITOR_MENU_UNDO:
            croft_wit_host_editor_input_enqueue_simple(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_UNDO);
            break;
        case CROFT_EDITOR_MENU_REDO:
            croft_wit_host_editor_input_enqueue_simple(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_REDO);
            break;
        case CROFT_EDITOR_MENU_SELECT_ALL:
            croft_wit_host_editor_input_enqueue_simple(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_SELECT_ALL);
            break;
        case CROFT_EDITOR_MENU_COPY:
            croft_wit_host_editor_input_enqueue_simple(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_COPY);
            break;
        case CROFT_EDITOR_MENU_CUT:
            croft_wit_host_editor_input_enqueue_simple(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_CUT);
            break;
        case CROFT_EDITOR_MENU_PASTE:
            croft_wit_host_editor_input_enqueue_simple(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_PASTE);
            break;
        case CROFT_EDITOR_MENU_INDENT:
            croft_wit_host_editor_input_enqueue_simple(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_INDENT);
            break;
        case CROFT_EDITOR_MENU_OUTDENT:
            croft_wit_host_editor_input_enqueue_simple(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_OUTDENT);
            break;
        case CROFT_EDITOR_MENU_FOLD:
            croft_wit_host_editor_input_enqueue_simple(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_FOLD);
            break;
        case CROFT_EDITOR_MENU_UNFOLD:
            croft_wit_host_editor_input_enqueue_simple(runtime,
                SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_UNFOLD);
            break;
        default:
            break;
    }
}

static int32_t croft_wit_host_editor_input_dispatch_window_key(
    void* ctx,
    const SapWitHostEditorInputKeyEvent* event,
    SapWitHostEditorInputReply* reply_out)
{
    croft_wit_host_editor_input_runtime* runtime = (croft_wit_host_editor_input_runtime*)ctx;

    if (!runtime || !event || !reply_out) {
        return -1;
    }
    croft_wit_host_editor_input_translate_key(runtime, event);
    croft_wit_host_editor_input_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_editor_input_dispatch_window_char(
    void* ctx,
    const SapWitHostEditorInputCharEvent* event,
    SapWitHostEditorInputReply* reply_out)
{
    croft_wit_host_editor_input_runtime* runtime = (croft_wit_host_editor_input_runtime*)ctx;
    SapWitHostEditorInputEditorAction action = {0};

    if (!runtime || !event || !reply_out) {
        return -1;
    }
    if (runtime->suppress_tab_char && event->codepoint == (uint32_t)'\t') {
        runtime->suppress_tab_char = 0u;
        croft_wit_host_editor_input_reply_status_ok(reply_out);
        return 0;
    }
    runtime->suppress_tab_char = 0u;
    action.case_tag = SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_INSERT_CODEPOINT;
    action.val.insert_codepoint = event->codepoint;
    croft_wit_host_editor_input_enqueue(runtime, &action);
    croft_wit_host_editor_input_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_editor_input_dispatch_menu_action(
    void* ctx,
    const SapWitHostEditorInputMenuAction* action,
    SapWitHostEditorInputReply* reply_out)
{
    croft_wit_host_editor_input_runtime* runtime = (croft_wit_host_editor_input_runtime*)ctx;

    if (!runtime || !action || !reply_out) {
        return -1;
    }
    croft_wit_host_editor_input_translate_menu(runtime, action->action_id);
    croft_wit_host_editor_input_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_editor_input_dispatch_next_action(
    void* ctx,
    SapWitHostEditorInputReply* reply_out)
{
    croft_wit_host_editor_input_runtime* runtime = (croft_wit_host_editor_input_runtime*)ctx;

    if (!runtime || !reply_out) {
        return -1;
    }
    if (runtime->action_count == 0u) {
        croft_wit_host_editor_input_reply_action_empty(reply_out);
        return 0;
    }
    croft_wit_host_editor_input_reply_action_ok(reply_out, &runtime->actions[runtime->action_head]);
    runtime->action_head = (runtime->action_head + 1u) % CROFT_WIT_EDITOR_ACTION_CAP;
    runtime->action_count--;
    return 0;
}

int32_t croft_wit_host_editor_input_runtime_dispatch(
    croft_wit_host_editor_input_runtime* runtime,
    const SapWitHostEditorInputCommand* command,
    SapWitHostEditorInputReply* reply_out)
{
    int32_t rc;

    static const SapWitHostEditorInputDispatchOps ops = {
        .window_key = croft_wit_host_editor_input_dispatch_window_key,
        .window_char = croft_wit_host_editor_input_dispatch_window_char,
        .menu_action = croft_wit_host_editor_input_dispatch_menu_action,
        .next_action = croft_wit_host_editor_input_dispatch_next_action,
    };

    if (!runtime || !command || !reply_out) {
        return -1;
    }

    rc = sap_wit_dispatch_host_editor_input(runtime, &ops, command, reply_out);
    if (rc == -1) {
        croft_wit_host_editor_input_reply_status_err(reply_out, "internal");
        return 0;
    }
    return rc;
}
