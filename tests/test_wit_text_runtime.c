#include "croft/wit_text_runtime.h"

#include <stdint.h>
#include <string.h>

static int wit_reply_expect_text_ok(const SapWitCommonCoreTextReply* reply, SapWitCommonCoreTextResource* handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_COMMON_CORE_TEXT_REPLY_TEXT
            || !reply->val.text.is_v_ok) {
        return 0;
    }
    *handle_out = reply->val.text.v_val.ok.v;
    return 1;
}

static int wit_reply_expect_status_ok(const SapWitCommonCoreTextReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_COMMON_CORE_TEXT_REPLY_STATUS
        && reply->val.status.is_v_ok;
}

static int wit_reply_expect_export_ok(const SapWitCommonCoreTextReply* reply,
                                      const uint8_t** utf8_out,
                                      uint32_t* len_out)
{
    if (!reply || !utf8_out || !len_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_COMMON_CORE_TEXT_REPLY_EXPORT
            || !reply->val.export.is_v_ok) {
        return 0;
    }
    *utf8_out = reply->val.export.v_val.ok.v_data;
    *len_out = reply->val.export.v_val.ok.v_len;
    return 1;
}

static int wit_reply_expect_error(const uint8_t* data, uint32_t len, const char* expected)
{
    size_t expected_len;

    if (!expected) {
        return 0;
    }
    expected_len = strlen(expected);
    return len == (uint32_t)expected_len && memcmp(data, expected, expected_len) == 0;
}

int test_wit_text_runtime_roundtrip(void)
{
    croft_wit_text_runtime* runtime;
    SapWitCommonCoreTextCommand command = {0};
    SapWitCommonCoreTextReply reply = {0};
    SapWitCommonCoreTextResource base = SAP_WIT_COMMON_CORE_TEXT_RESOURCE_INVALID;
    SapWitCommonCoreTextResource edited = SAP_WIT_COMMON_CORE_TEXT_RESOURCE_INVALID;
    const uint8_t* utf8 = NULL;
    uint32_t utf8_len = 0u;

    runtime = croft_wit_text_runtime_create(NULL);
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_COMMON_CORE_TEXT_COMMAND_OPEN;
    command.val.open.initial_data = (const uint8_t*)"small binaries";
    command.val.open.initial_len = 14u;
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_reply_expect_text_ok(&reply, &base)) {
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_text_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_TEXT_COMMAND_CLONE;
    command.val.clone.source = base;
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_reply_expect_text_ok(&reply, &edited)) {
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_text_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_TEXT_COMMAND_INSERT;
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

    command.case_tag = SAP_WIT_COMMON_CORE_TEXT_COMMAND_EXPORT;
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

    command.case_tag = SAP_WIT_COMMON_CORE_TEXT_COMMAND_EXPORT;
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

    command.case_tag = SAP_WIT_COMMON_CORE_TEXT_COMMAND_DROP;
    command.val.drop.text = edited;
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_reply_expect_status_ok(&reply)) {
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_text_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_TEXT_COMMAND_DROP;
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
    SapWitCommonCoreTextCommand command = {0};
    SapWitCommonCoreTextReply reply = {0};

    runtime = croft_wit_text_runtime_create(NULL);
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_COMMON_CORE_TEXT_COMMAND_EXPORT;
    command.val.export.text = 99u;
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || reply.case_tag != SAP_WIT_COMMON_CORE_TEXT_REPLY_EXPORT
            || reply.val.export.is_v_ok
            || !wit_reply_expect_error(reply.val.export.v_val.err.v_data,
                                       reply.val.export.v_val.err.v_len,
                                       "invalid-handle")) {
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_text_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_TEXT_COMMAND_DROP;
    command.val.drop.text = 99u;
    if (croft_wit_text_runtime_dispatch(runtime, &command, &reply) != 0
            || reply.case_tag != SAP_WIT_COMMON_CORE_TEXT_REPLY_STATUS
            || reply.val.status.is_v_ok
            || !wit_reply_expect_error(reply.val.status.v_val.err.v_data,
                                       reply.val.status.v_val.err.v_len,
                                       "invalid-handle")) {
        croft_wit_text_reply_dispose(&reply);
        croft_wit_text_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_text_reply_dispose(&reply);

    croft_wit_text_runtime_destroy(runtime);
    return 0;
}
