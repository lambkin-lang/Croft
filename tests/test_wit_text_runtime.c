#include "croft/wit_text_runtime.h"

#include <stdint.h>
#include <string.h>

static int wit_reply_expect_text_ok(const SapWitTextReply* reply, SapWitTextResource* handle_out)
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

static int wit_reply_expect_status_ok(const SapWitTextReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_TEXT_REPLY_STATUS
        && reply->val.status.case_tag == SAP_WIT_STATUS_OK;
}

static int wit_reply_expect_export_ok(const SapWitTextReply* reply,
                                      const uint8_t** utf8_out,
                                      uint32_t* len_out)
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

int test_wit_text_runtime_roundtrip(void)
{
    croft_wit_text_runtime* runtime;
    SapWitTextCommand command = {0};
    SapWitTextReply reply = {0};
    SapWitTextResource base = SAP_WIT_TEXT_RESOURCE_INVALID;
    SapWitTextResource edited = SAP_WIT_TEXT_RESOURCE_INVALID;
    const uint8_t* utf8 = NULL;
    uint32_t utf8_len = 0u;

    runtime = croft_wit_text_runtime_create(NULL);
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_TEXT_COMMAND_OPEN;
    command.val.open.initial_data = (const uint8_t*)"small binaries";
    command.val.open.initial_len = 14u;
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_reply_expect_text_ok(&reply, &base)) {
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_text_reply_dispose(&reply);

    command.case_tag = SAP_WIT_TEXT_COMMAND_CLONE;
    command.val.clone.source = base;
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_reply_expect_text_ok(&reply, &edited)) {
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_text_reply_dispose(&reply);

    command.case_tag = SAP_WIT_TEXT_COMMAND_INSERT;
    command.val.insert.text = edited;
    command.val.insert.offset = 0u;
    command.val.insert.utf8_data = (const uint8_t*)"Big analysis, ";
    command.val.insert.utf8_len = 14u;
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_reply_expect_status_ok(&reply)) {
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_text_reply_dispose(&reply);

    command.case_tag = SAP_WIT_TEXT_COMMAND_EXPORT;
    command.val.export.text = base;
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_reply_expect_export_ok(&reply, &utf8, &utf8_len)
            || utf8_len != 14u
            || memcmp(utf8, "small binaries", 14u) != 0) {
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_text_reply_dispose(&reply);

    command.case_tag = SAP_WIT_TEXT_COMMAND_EXPORT;
    command.val.export.text = edited;
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_reply_expect_export_ok(&reply, &utf8, &utf8_len)
            || utf8_len != 28u
            || memcmp(utf8, "Big analysis, small binaries", 28u) != 0) {
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_text_reply_dispose(&reply);

    command.case_tag = SAP_WIT_TEXT_COMMAND_DROP;
    command.val.drop.text = edited;
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_reply_expect_status_ok(&reply)) {
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_text_reply_dispose(&reply);

    command.case_tag = SAP_WIT_TEXT_COMMAND_DROP;
    command.val.drop.text = base;
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_reply_expect_status_ok(&reply)) {
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_text_reply_dispose(&reply);

    croft_wit_text_runtime_destroy(runtime);
    return 0;
}

int test_wit_text_runtime_invalid_handle(void)
{
    croft_wit_text_runtime* runtime;
    SapWitTextCommand command = {0};
    SapWitTextReply reply = {0};

    runtime = croft_wit_text_runtime_create(NULL);
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_TEXT_COMMAND_EXPORT;
    command.val.export.text = 99u;
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || reply.case_tag != SAP_WIT_TEXT_REPLY_EXPORT
            || reply.val.export.case_tag != SAP_WIT_TEXT_EXPORT_RESULT_ERR
            || reply.val.export.val.err != SAP_WIT_COMMON_ERROR_INVALID_HANDLE) {
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_text_reply_dispose(&reply);

    command.case_tag = SAP_WIT_TEXT_COMMAND_DROP;
    command.val.drop.text = 99u;
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || reply.case_tag != SAP_WIT_TEXT_REPLY_STATUS
            || reply.val.status.case_tag != SAP_WIT_STATUS_ERR
            || reply.val.status.val.err != SAP_WIT_COMMON_ERROR_INVALID_HANDLE) {
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_text_reply_dispose(&reply);

    croft_wit_text_runtime_destroy(runtime);
    return 0;
}
