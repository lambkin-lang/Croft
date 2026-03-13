#include "croft/host_ui.h"
#include "croft/wit_host_editor_input_runtime.h"

static int wit_editor_input_expect_action(const SapWitHostEditorInputReply* reply,
                                          SapWitHostEditorInputEditorAction* action_out)
{
    if (!reply || !action_out) {
        return -1;
    }
    if (reply->case_tag != SAP_WIT_HOST_EDITOR_INPUT_REPLY_ACTION) {
        return -1;
    }
    if (!reply->val.action.is_v_ok) {
        return -1;
    }
    if (!reply->val.action.v_val.ok.has_v) {
        return 0;
    }
    *action_out = reply->val.action.v_val.ok.v;
    return 1;
}

int test_wit_host_editor_input_runtime_shortcuts(void)
{
    croft_wit_host_editor_input_runtime* runtime;
    SapWitHostEditorInputCommand command = {0};
    SapWitHostEditorInputReply reply = {0};
    SapWitHostEditorInputEditorAction action = {0};

    runtime = croft_wit_host_editor_input_runtime_create();
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_WINDOW_KEY;
    command.val.window_key.key = 83;
    command.val.window_key.action = 1;
    command.val.window_key.modifiers = CROFT_UI_MOD_SUPER;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_NEXT_ACTION;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0
            || wit_editor_input_expect_action(&reply, &action) != 1
            || action.case_tag != SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_SAVE) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_MENU_ACTION;
    command.val.menu_action.action_id = 205;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }
    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_NEXT_ACTION;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0
            || wit_editor_input_expect_action(&reply, &action) != 1
            || action.case_tag != SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_CUT) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    croft_wit_host_editor_input_runtime_destroy(runtime);
    return 0;
}

int test_wit_host_editor_input_runtime_motion_modes(void)
{
    croft_wit_host_editor_input_runtime* runtime;
    SapWitHostEditorInputCommand command = {0};
    SapWitHostEditorInputReply reply = {0};
    SapWitHostEditorInputEditorAction action = {0};

    runtime = croft_wit_host_editor_input_runtime_create();
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_WINDOW_KEY;
    command.val.window_key.key = 263;
    command.val.window_key.action = 1;
    command.val.window_key.modifiers = CROFT_UI_MOD_SHIFT | CROFT_UI_MOD_ALT | CROFT_UI_MOD_CONTROL;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_NEXT_ACTION;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0
            || wit_editor_input_expect_action(&reply, &action) != 1
            || action.case_tag != SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_MOVE_LEFT) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }
    if ((action.val.move_left.flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_SELECTING) == 0u
            || (action.val.move_left.flags & SAP_WIT_HOST_EDITOR_INPUT_MOTION_FLAGS_WORD_PART) == 0u) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_WINDOW_KEY;
    command.val.window_key.key = 259;
    command.val.window_key.action = 1;
    command.val.window_key.modifiers = CROFT_UI_MOD_CONTROL;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_NEXT_ACTION;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0
            || wit_editor_input_expect_action(&reply, &action) != 1
            || action.case_tag != SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_DELETE_LEFT) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }
    if ((action.val.delete_left.flags & SAP_WIT_HOST_EDITOR_INPUT_DELETE_FLAGS_WORD) == 0u) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    croft_wit_host_editor_input_runtime_destroy(runtime);
    return 0;
}

int test_wit_host_editor_input_runtime_indent_actions(void)
{
    croft_wit_host_editor_input_runtime* runtime;
    SapWitHostEditorInputCommand command = {0};
    SapWitHostEditorInputReply reply = {0};
    SapWitHostEditorInputEditorAction action = {0};

    runtime = croft_wit_host_editor_input_runtime_create();
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_WINDOW_KEY;
    command.val.window_key.key = 258;
    command.val.window_key.action = 1;
    command.val.window_key.modifiers = 0u;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_NEXT_ACTION;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0
            || wit_editor_input_expect_action(&reply, &action) != 1
            || action.case_tag != SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_INDENT) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_WINDOW_CHAR;
    command.val.window_char.codepoint = (uint32_t)'\t';
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_NEXT_ACTION;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0
            || wit_editor_input_expect_action(&reply, &action) != 0) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_WINDOW_KEY;
    command.val.window_key.key = 258;
    command.val.window_key.action = 1;
    command.val.window_key.modifiers = CROFT_UI_MOD_SHIFT;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_NEXT_ACTION;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0
            || wit_editor_input_expect_action(&reply, &action) != 1
            || action.case_tag != SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_OUTDENT) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_WINDOW_KEY;
    command.val.window_key.key = 93;
    command.val.window_key.action = 1;
    command.val.window_key.modifiers = CROFT_UI_MOD_SUPER;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_NEXT_ACTION;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0
            || wit_editor_input_expect_action(&reply, &action) != 1
            || action.case_tag != SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_INDENT) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_MENU_ACTION;
    command.val.menu_action.action_id = 211;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_NEXT_ACTION;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0
            || wit_editor_input_expect_action(&reply, &action) != 1
            || action.case_tag != SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_OUTDENT) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    croft_wit_host_editor_input_runtime_destroy(runtime);
    return 0;
}

int test_wit_host_editor_input_runtime_fold_actions(void)
{
    croft_wit_host_editor_input_runtime* runtime;
    SapWitHostEditorInputCommand command = {0};
    SapWitHostEditorInputReply reply = {0};
    SapWitHostEditorInputEditorAction action = {0};

    runtime = croft_wit_host_editor_input_runtime_create();
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_WINDOW_KEY;
    command.val.window_key.key = 91;
    command.val.window_key.action = 1;
    command.val.window_key.modifiers = CROFT_UI_MOD_SUPER | CROFT_UI_MOD_ALT;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_NEXT_ACTION;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0
            || wit_editor_input_expect_action(&reply, &action) != 1
            || action.case_tag != SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_FOLD) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_MENU_ACTION;
    command.val.menu_action.action_id = 213;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_NEXT_ACTION;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0
            || wit_editor_input_expect_action(&reply, &action) != 1
            || action.case_tag != SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_UNFOLD) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    croft_wit_host_editor_input_runtime_destroy(runtime);
    return 0;
}

int test_wit_host_editor_input_runtime_toggle_wrap(void)
{
    croft_wit_host_editor_input_runtime* runtime;
    SapWitHostEditorInputCommand command = {0};
    SapWitHostEditorInputReply reply = {0};
    SapWitHostEditorInputEditorAction action = {0};

    runtime = croft_wit_host_editor_input_runtime_create();
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_WINDOW_KEY;
    command.val.window_key.key = 90;
    command.val.window_key.action = 1;
    command.val.window_key.modifiers = CROFT_UI_MOD_ALT;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_NEXT_ACTION;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0
            || wit_editor_input_expect_action(&reply, &action) != 1
            || action.case_tag != SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_TOGGLE_WRAP) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    croft_wit_host_editor_input_runtime_destroy(runtime);
    return 0;
}

int test_wit_host_editor_input_runtime_composition_actions(void)
{
    croft_wit_host_editor_input_runtime* runtime;
    SapWitHostEditorInputCommand command = {0};
    SapWitHostEditorInputReply reply = {0};
    SapWitHostEditorInputEditorAction action = {0};
    static const uint8_t marked_text[] = "ko";

    runtime = croft_wit_host_editor_input_runtime_create();
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_WINDOW_COMPOSITION;
    command.val.window_composition.utf8_data = marked_text;
    command.val.window_composition.utf8_len = 2u;
    command.val.window_composition.selection_start = 1u;
    command.val.window_composition.selection_end = 2u;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_NEXT_ACTION;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0
            || wit_editor_input_expect_action(&reply, &action) != 1
            || action.case_tag != SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_COMPOSITION_UPDATE) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }
    if (action.val.composition_update.utf8_len != 2u
            || action.val.composition_update.utf8_data == NULL
            || action.val.composition_update.utf8_data[0] != 'k'
            || action.val.composition_update.utf8_data[1] != 'o'
            || action.val.composition_update.selection_start != 1u
            || action.val.composition_update.selection_end != 2u) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_WINDOW_COMPOSITION_CLEAR;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_EDITOR_INPUT_COMMAND_NEXT_ACTION;
    if (croft_wit_host_editor_input_runtime_dispatch(runtime, &command, &reply) != 0
            || wit_editor_input_expect_action(&reply, &action) != 1
            || action.case_tag != SAP_WIT_HOST_EDITOR_INPUT_EDITOR_ACTION_COMPOSITION_CLEAR) {
        croft_wit_host_editor_input_runtime_destroy(runtime);
        return 1;
    }

    croft_wit_host_editor_input_runtime_destroy(runtime);
    return 0;
}
