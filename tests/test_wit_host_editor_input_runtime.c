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
    if (reply->val.action.case_tag == SAP_WIT_HOST_EDITOR_INPUT_ACTION_RESULT_EMPTY) {
        return 0;
    }
    if (reply->val.action.case_tag != SAP_WIT_HOST_EDITOR_INPUT_ACTION_RESULT_OK) {
        return -1;
    }
    *action_out = reply->val.action.val.ok;
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
