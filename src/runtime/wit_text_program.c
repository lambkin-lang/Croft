#include "croft/wit_text_program.h"

#include <stdlib.h>
#include <string.h>

static int croft_wit_text_expect_handle(const SapWitCommonCoreTextReply* reply,
                                        SapWitCommonCoreTextResource* handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_COMMON_CORE_TEXT_REPLY_TEXT
            || reply->val.text.case_tag != SAP_WIT_COMMON_CORE_TEXT_OP_RESULT_OK) {
        return 0;
    }
    *handle_out = reply->val.text.val.ok;
    return 1;
}

static int croft_wit_text_expect_status_ok(const SapWitCommonCoreTextReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_COMMON_CORE_TEXT_REPLY_STATUS
        && reply->val.status.case_tag == SAP_WIT_COMMON_CORE_STATUS_OK;
}

static int croft_wit_text_expect_export_ok(const SapWitCommonCoreTextReply* reply,
                                           const uint8_t** utf8_out,
                                           uint32_t* len_out)
{
    if (!reply || !utf8_out || !len_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_COMMON_CORE_TEXT_REPLY_EXPORT
            || reply->val.export.case_tag != SAP_WIT_COMMON_CORE_TEXT_EXPORT_RESULT_OK) {
        return 0;
    }
    *utf8_out = reply->val.export.val.ok.data;
    *len_out = reply->val.export.val.ok.len;
    return 1;
}

static void croft_wit_text_program_cleanup(const croft_wit_text_program_host* host,
                                           SapWitCommonCoreTextResource handle)
{
    SapWitCommonCoreTextCommand command = {0};
    SapWitCommonCoreTextReply reply = {0};

    if (!host || !host->dispatch || !host->dispose_reply
            || handle == SAP_WIT_COMMON_CORE_TEXT_RESOURCE_INVALID) {
        return;
    }

    command.case_tag = SAP_WIT_COMMON_CORE_TEXT_COMMAND_DROP;
    command.val.drop.text = handle;
    if (host->dispatch(host->userdata, &command, &reply) == 0) {
        host->dispose_reply(&reply);
    }
}

int32_t croft_wit_text_program_prepend(
    const croft_wit_text_program_host* host,
    const uint8_t* initial_utf8,
    uint32_t initial_len,
    const uint8_t* prefix_utf8,
    uint32_t prefix_len,
    croft_wit_owned_bytes* out_bytes)
{
    SapWitCommonCoreTextCommand command = {0};
    SapWitCommonCoreTextReply reply = {0};
    SapWitCommonCoreTextResource base = SAP_WIT_COMMON_CORE_TEXT_RESOURCE_INVALID;
    SapWitCommonCoreTextResource edited = SAP_WIT_COMMON_CORE_TEXT_RESOURCE_INVALID;
    const uint8_t* exported = NULL;
    uint32_t exported_len = 0u;
    uint8_t* owned = NULL;

    if (!host || !host->dispatch || !host->dispose_reply || !out_bytes) {
        return -1;
    }
    memset(out_bytes, 0, sizeof(*out_bytes));

    command.case_tag = SAP_WIT_COMMON_CORE_TEXT_COMMAND_OPEN;
    command.val.open.initial_data = initial_utf8;
    command.val.open.initial_len = initial_len;
    if (host->dispatch(host->userdata, &command, &reply) != 0
            || !croft_wit_text_expect_handle(&reply, &base)) {
        host->dispose_reply(&reply);
        return -1;
    }
    host->dispose_reply(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_TEXT_COMMAND_CLONE;
    command.val.clone.source = base;
    if (host->dispatch(host->userdata, &command, &reply) != 0
            || !croft_wit_text_expect_handle(&reply, &edited)) {
        host->dispose_reply(&reply);
        croft_wit_text_program_cleanup(host, base);
        return -1;
    }
    host->dispose_reply(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_TEXT_COMMAND_INSERT;
    command.val.insert.text = edited;
    command.val.insert.offset = 0u;
    command.val.insert.utf8_data = prefix_utf8;
    command.val.insert.utf8_len = prefix_len;
    if (host->dispatch(host->userdata, &command, &reply) != 0
            || !croft_wit_text_expect_status_ok(&reply)) {
        host->dispose_reply(&reply);
        croft_wit_text_program_cleanup(host, edited);
        croft_wit_text_program_cleanup(host, base);
        return -1;
    }
    host->dispose_reply(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_TEXT_COMMAND_EXPORT;
    command.val.export.text = edited;
    if (host->dispatch(host->userdata, &command, &reply) != 0
            || !croft_wit_text_expect_export_ok(&reply, &exported, &exported_len)) {
        host->dispose_reply(&reply);
        croft_wit_text_program_cleanup(host, edited);
        croft_wit_text_program_cleanup(host, base);
        return -1;
    }

    if (exported_len > 0u) {
        owned = (uint8_t*)malloc((size_t)exported_len);
        if (!owned) {
            host->dispose_reply(&reply);
            croft_wit_text_program_cleanup(host, edited);
            croft_wit_text_program_cleanup(host, base);
            return -1;
        }
        memcpy(owned, exported, exported_len);
    }
    out_bytes->data = owned;
    out_bytes->len = exported_len;
    host->dispose_reply(&reply);

    croft_wit_text_program_cleanup(host, edited);
    croft_wit_text_program_cleanup(host, base);
    return 0;
}

void croft_wit_owned_bytes_dispose(croft_wit_owned_bytes* bytes)
{
    if (!bytes) {
        return;
    }
    free(bytes->data);
    bytes->data = NULL;
    bytes->len = 0u;
}
