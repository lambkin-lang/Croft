#include "croft/wit_mailbox_runtime.h"

#include <stdint.h>
#include <string.h>

static int wit_mailbox_expect_mailbox_ok(const SapWitCommonCoreMailboxReply* reply,
                                         SapWitCommonCoreMailboxResource* handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_COMMON_CORE_MAILBOX_REPLY_MAILBOX
            || !reply->val.mailbox.is_v_ok) {
        return 0;
    }
    *handle_out = reply->val.mailbox.v_val.ok.v;
    return 1;
}

static int wit_mailbox_expect_status_ok(const SapWitCommonCoreMailboxReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_COMMON_CORE_MAILBOX_REPLY_STATUS
        && reply->val.status.is_v_ok;
}

static int wit_mailbox_expect_error(const uint8_t* data, uint32_t len, const char* expected)
{
    size_t expected_len;

    if (!expected) {
        return 0;
    }
    expected_len = strlen(expected);
    return len == (uint32_t)expected_len && memcmp(data, expected, expected_len) == 0;
}

int test_wit_mailbox_runtime_roundtrip(void)
{
    croft_wit_mailbox_runtime* runtime;
    SapWitCommonCoreMailboxCommand command = {0};
    SapWitCommonCoreMailboxReply reply = {0};
    SapWitCommonCoreMailboxResource mailbox = SAP_WIT_COMMON_CORE_MAILBOX_RESOURCE_INVALID;
    const uint8_t* payload = NULL;
    uint32_t payload_len = 0u;

    runtime = croft_wit_mailbox_runtime_create();
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_COMMON_CORE_MAILBOX_COMMAND_OPEN;
    command.val.open.max_messages = 2u;
    if (croft_wit_mailbox_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_mailbox_expect_mailbox_ok(&reply, &mailbox)) {
        croft_wit_mailbox_reply_dispose(&reply);
        croft_wit_mailbox_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_mailbox_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_MAILBOX_COMMAND_SEND;
    command.val.send.mailbox = mailbox;
    command.val.send.payload_data = (const uint8_t*)"mail";
    command.val.send.payload_len = 4u;
    if (croft_wit_mailbox_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_mailbox_expect_status_ok(&reply)) {
        croft_wit_mailbox_reply_dispose(&reply);
        croft_wit_mailbox_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_mailbox_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_MAILBOX_COMMAND_RECV;
    command.val.recv.mailbox = mailbox;
    if (croft_wit_mailbox_runtime_dispatch(runtime, &command, &reply) != 0
            || reply.case_tag != SAP_WIT_COMMON_CORE_MAILBOX_REPLY_RECV
            || !reply.val.recv.is_v_ok
            || !reply.val.recv.v_val.ok.has_v) {
        croft_wit_mailbox_reply_dispose(&reply);
        croft_wit_mailbox_runtime_destroy(runtime);
        return 1;
    }

    payload = reply.val.recv.v_val.ok.v_data;
    payload_len = reply.val.recv.v_val.ok.v_len;
    if (payload_len != 4u || memcmp(payload, "mail", 4u) != 0) {
        croft_wit_mailbox_reply_dispose(&reply);
        croft_wit_mailbox_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_mailbox_reply_dispose(&reply);

    if (croft_wit_mailbox_runtime_dispatch(runtime, &command, &reply) != 0
            || reply.case_tag != SAP_WIT_COMMON_CORE_MAILBOX_REPLY_RECV
            || !reply.val.recv.is_v_ok
            || reply.val.recv.v_val.ok.has_v) {
        croft_wit_mailbox_reply_dispose(&reply);
        croft_wit_mailbox_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_mailbox_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_MAILBOX_COMMAND_DROP;
    command.val.drop.mailbox = mailbox;
    if (croft_wit_mailbox_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_mailbox_expect_status_ok(&reply)) {
        croft_wit_mailbox_reply_dispose(&reply);
        croft_wit_mailbox_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_mailbox_reply_dispose(&reply);

    croft_wit_mailbox_runtime_destroy(runtime);
    return 0;
}

int test_wit_mailbox_runtime_drop_busy(void)
{
    croft_wit_mailbox_runtime* runtime;
    SapWitCommonCoreMailboxCommand command = {0};
    SapWitCommonCoreMailboxReply reply = {0};
    SapWitCommonCoreMailboxResource mailbox = SAP_WIT_COMMON_CORE_MAILBOX_RESOURCE_INVALID;

    runtime = croft_wit_mailbox_runtime_create();
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_COMMON_CORE_MAILBOX_COMMAND_OPEN;
    command.val.open.max_messages = 1u;
    if (croft_wit_mailbox_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_mailbox_expect_mailbox_ok(&reply, &mailbox)) {
        croft_wit_mailbox_reply_dispose(&reply);
        croft_wit_mailbox_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_mailbox_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_MAILBOX_COMMAND_SEND;
    command.val.send.mailbox = mailbox;
    command.val.send.payload_data = (const uint8_t*)"x";
    command.val.send.payload_len = 1u;
    if (croft_wit_mailbox_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_mailbox_expect_status_ok(&reply)) {
        croft_wit_mailbox_reply_dispose(&reply);
        croft_wit_mailbox_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_mailbox_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_MAILBOX_COMMAND_DROP;
    command.val.drop.mailbox = mailbox;
    if (croft_wit_mailbox_runtime_dispatch(runtime, &command, &reply) != 0
            || reply.case_tag != SAP_WIT_COMMON_CORE_MAILBOX_REPLY_STATUS
            || reply.val.status.is_v_ok
            || !wit_mailbox_expect_error(reply.val.status.v_val.err.v_data,
                                         reply.val.status.v_val.err.v_len,
                                         "busy")) {
        croft_wit_mailbox_reply_dispose(&reply);
        croft_wit_mailbox_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_mailbox_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_MAILBOX_COMMAND_RECV;
    command.val.recv.mailbox = mailbox;
    if (croft_wit_mailbox_runtime_dispatch(runtime, &command, &reply) != 0
            || reply.case_tag != SAP_WIT_COMMON_CORE_MAILBOX_REPLY_RECV
            || !reply.val.recv.is_v_ok
            || !reply.val.recv.v_val.ok.has_v) {
        croft_wit_mailbox_reply_dispose(&reply);
        croft_wit_mailbox_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_mailbox_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_MAILBOX_COMMAND_DROP;
    command.val.drop.mailbox = mailbox;
    if (croft_wit_mailbox_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_mailbox_expect_status_ok(&reply)) {
        croft_wit_mailbox_reply_dispose(&reply);
        croft_wit_mailbox_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_mailbox_reply_dispose(&reply);

    croft_wit_mailbox_runtime_destroy(runtime);
    return 0;
}
