#include "croft/wit_host_editor_input_runtime.h"

#include "croft/editor_menu_ids.h"
#include "croft/host_ui.h"

#include <stdlib.h>
#include <string.h>

#define CROFT_WIT_EDITOR_ACTION_CAP 128u

enum {
    CROFT_KEY_RELEASE = 0,
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
    CROFT_KEY_Q = 81,
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
};

static void croft_wit_host_editor_input_reply_zero(SapWitHostEditorInputReply* reply)
{
    if (reply) {
        memset(reply, 0, sizeof(*reply));
    }
}

static void croft_wit_host_editor_input_reply_status_ok(SapWitHostEditorInputReply* reply)
{
    croft_wit_host_editor_input_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_EDITOR_INPUT_REPLY_STATUS;
    reply->val.status.case_tag = SAP_WIT_HOST_EDITOR_INPUT_STATUS_OK;
}

static void croft_wit_host_editor_input_reply_status_err(SapWitHostEditorInputReply* reply, uint8_t err)
{
    croft_wit_host_editor_input_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_EDITOR_INPUT_REPLY_STATUS;
    reply->val.status.case_tag = SAP_WIT_HOST_EDITOR_INPUT_STATUS_ERR;
    reply->val.status.val.err = err;
}

static void croft_wit_host_editor_input_reply_action_ok(SapWitHostEditorInputReply* reply,
                                                        const SapWitHostEditorInputEditorAction* action)
{
    croft_wit_host_editor_input_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_EDITOR_INPUT_REPLY_ACTION;
    reply->val.action.case_tag = SAP_WIT_HOST_EDITOR_INPUT_ACTION_RESULT_OK;
    reply->val.action.val.ok = *action;
}

static void croft_wit_host_editor_input_reply_action_empty(SapWitHostEditorInputReply* reply)
{
    croft_wit_host_editor_input_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_EDITOR_INPUT_REPLY_ACTION;
    reply->val.action.case_tag = SAP_WIT_HOST_EDITOR_INPUT_ACTION_RESULT_EMPTY;
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
            case CROFT_KEY_Q:
                croft_wit_host_editor_input_enqueue_simple(runtime,
                    SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_QUIT);
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
        default:
            break;
    }
}

int32_t croft_wit_host_editor_input_runtime_dispatch(
    croft_wit_host_editor_input_runtime* runtime,
    const SapWitHostEditorInputCommand* command,
    SapWitHostEditorInputReply* reply_out)
{
    if (!runtime || !command || !reply_out) {
        return -1;
    }

    switch (command->case_tag) {
        case SAP_WIT_HOST_EDITOR_INPUT_COMMAND_WINDOW_KEY:
            croft_wit_host_editor_input_translate_key(runtime, &command->val.window_key);
            croft_wit_host_editor_input_reply_status_ok(reply_out);
            return 0;
        case SAP_WIT_HOST_EDITOR_INPUT_COMMAND_WINDOW_CHAR: {
            SapWitHostEditorInputEditorAction action = {0};
            action.case_tag = SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_INSERT_CODEPOINT;
            action.val.insert_codepoint = command->val.window_char.codepoint;
            croft_wit_host_editor_input_enqueue(runtime, &action);
            croft_wit_host_editor_input_reply_status_ok(reply_out);
            return 0;
        }
        case SAP_WIT_HOST_EDITOR_INPUT_COMMAND_MENU_ACTION:
            croft_wit_host_editor_input_translate_menu(runtime, command->val.menu_action.action_id);
            croft_wit_host_editor_input_reply_status_ok(reply_out);
            return 0;
        case SAP_WIT_HOST_EDITOR_INPUT_COMMAND_NEXT_ACTION:
            if (runtime->action_count == 0u) {
                croft_wit_host_editor_input_reply_action_empty(reply_out);
                return 0;
            }
            croft_wit_host_editor_input_reply_action_ok(reply_out, &runtime->actions[runtime->action_head]);
            runtime->action_head = (runtime->action_head + 1u) % CROFT_WIT_EDITOR_ACTION_CAP;
            runtime->action_count--;
            return 0;
        default:
            croft_wit_host_editor_input_reply_status_err(reply_out,
                                                         SAP_WIT_HOST_EDITOR_INPUT_ERROR_INTERNAL);
            return 0;
    }
}
