#include "croft/wit_text_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int expect_text_handle(const SapWitTextReply* reply, SapWitTextResource* handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_TEXT_REPLY_TEXT
            || reply->val.text.case_tag != SAP_WIT_TEXT_OP_RESULT_OK) {
        return 0;
    }
    *handle_out = reply->val.text.val.ok;
    return 1;
}

static int expect_status_ok(const SapWitTextReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_TEXT_REPLY_STATUS
        && reply->val.status.case_tag == SAP_WIT_STATUS_OK;
}

static int expect_export_ok(const SapWitTextReply* reply, const uint8_t** utf8_out, uint32_t* len_out)
{
    if (!reply || !utf8_out || !len_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_TEXT_REPLY_EXPORT
            || reply->val.export.case_tag != SAP_WIT_TEXT_EXPORT_RESULT_OK) {
        return 0;
    }
    *utf8_out = reply->val.export.val.ok.data;
    *len_out = reply->val.export.val.ok.len;
    return 1;
}

int main(void)
{
    croft_wit_text_runtime* runtime;
    SapWitTextReply reply = {0};
    SapWitTextResource base = SAP_WIT_TEXT_RESOURCE_INVALID;
    SapWitTextResource edited = SAP_WIT_TEXT_RESOURCE_INVALID;
    SapWitTextCommand command = {0};
    const uint8_t* exported = NULL;
    uint32_t exported_len = 0u;

    runtime = croft_wit_text_runtime_create(NULL);
    if (!runtime) {
        fprintf(stderr, "example_wit_text_handles: runtime init failed\n");
        return 1;
    }

    command.case_tag = SAP_WIT_TEXT_COMMAND_OPEN;
    command.val.open.initial_data = (const uint8_t*)"small binaries";
    command.val.open.initial_len = (uint32_t)strlen("small binaries");
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || !expect_text_handle(&reply, &base)) {
        fprintf(stderr, "example_wit_text_handles: open failed\n");
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_text_reply_dispose(&reply);

    command.case_tag = SAP_WIT_TEXT_COMMAND_CLONE;
    command.val.clone.source = base;
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || !expect_text_handle(&reply, &edited)) {
        fprintf(stderr, "example_wit_text_handles: clone failed\n");
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_text_reply_dispose(&reply);

    command.case_tag = SAP_WIT_TEXT_COMMAND_INSERT;
    command.val.insert.text = edited;
    command.val.insert.offset = 0u;
    command.val.insert.utf8_data = (const uint8_t*)"Big analysis, ";
    command.val.insert.utf8_len = (uint32_t)strlen("Big analysis, ");
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || !expect_status_ok(&reply)) {
        fprintf(stderr, "example_wit_text_handles: insert failed\n");
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_text_reply_dispose(&reply);

    command.case_tag = SAP_WIT_TEXT_COMMAND_EXPORT;
    command.val.export.text = base;
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || !expect_export_ok(&reply, &exported, &exported_len)) {
        fprintf(stderr, "example_wit_text_handles: export base failed\n");
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    printf("base=\"%.*s\"\n", (int)exported_len, (const char*)exported);
    croft_wit_text_reply_dispose(&reply);

    command.case_tag = SAP_WIT_TEXT_COMMAND_EXPORT;
    command.val.export.text = edited;
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || !expect_export_ok(&reply, &exported, &exported_len)) {
        fprintf(stderr, "example_wit_text_handles: export edited failed\n");
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    printf("edited=\"%.*s\"\n", (int)exported_len, (const char*)exported);
    croft_wit_text_reply_dispose(&reply);

    command.case_tag = SAP_WIT_TEXT_COMMAND_DROP;
    command.val.drop.text = edited;
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || !expect_status_ok(&reply)) {
        fprintf(stderr, "example_wit_text_handles: drop edited failed\n");
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_text_reply_dispose(&reply);

    command.case_tag = SAP_WIT_TEXT_COMMAND_DROP;
    command.val.drop.text = base;
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || !expect_status_ok(&reply)) {
        fprintf(stderr, "example_wit_text_handles: drop base failed\n");
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_text_reply_dispose(&reply);

    croft_wit_text_runtime_destroy(runtime);
    return 0;
}
