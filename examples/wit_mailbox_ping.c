#include "croft/wit_mailbox_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int expect_mailbox_ok(const SapWitCommonCoreMailboxReply* reply, SapWitCommonCoreMailboxResource* handle_out)
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

static int expect_status_ok(const SapWitCommonCoreMailboxReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_COMMON_CORE_MAILBOX_REPLY_STATUS
        && reply->val.status.is_v_ok;
}

static int expect_recv_ok(const SapWitCommonCoreMailboxReply* reply, const uint8_t** data_out, uint32_t* len_out)
{
    if (!reply || !data_out || !len_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_COMMON_CORE_MAILBOX_REPLY_RECV
            || !reply->val.recv.is_v_ok
            || !reply->val.recv.v_val.ok.has_v) {
        return 0;
    }
    *data_out = reply->val.recv.v_val.ok.v_data;
    *len_out = reply->val.recv.v_val.ok.v_len;
    return 1;
}

int main(void)
{
    croft_wit_mailbox_runtime* runtime;
    SapWitCommonCoreMailboxCommand command = {0};
    SapWitCommonCoreMailboxReply reply = {0};
    SapWitCommonCoreMailboxResource mailbox = SAP_WIT_COMMON_CORE_MAILBOX_RESOURCE_INVALID;
    const uint8_t* payload = NULL;
    uint32_t payload_len = 0u;

    runtime = croft_wit_mailbox_runtime_create();
    if (!runtime) {
        fprintf(stderr, "example_wit_mailbox_ping: runtime init failed\n");
        return 1;
    }

    command.case_tag = SAP_WIT_COMMON_CORE_MAILBOX_COMMAND_OPEN;
    command.val.open.max_messages = 4u;
    if (croft_wit_mailbox_runtime_dispatch(runtime, &command, &reply) != 0
            || !expect_mailbox_ok(&reply, &mailbox)) {
        fprintf(stderr, "example_wit_mailbox_ping: open failed\n");
        croft_wit_mailbox_reply_dispose(&reply);
        croft_wit_mailbox_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_mailbox_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_MAILBOX_COMMAND_SEND;
    command.val.send.mailbox = mailbox;
    command.val.send.payload_data = (const uint8_t*)"ping";
    command.val.send.payload_len = 4u;
    if (croft_wit_mailbox_runtime_dispatch(runtime, &command, &reply) != 0
            || !expect_status_ok(&reply)) {
        fprintf(stderr, "example_wit_mailbox_ping: send failed\n");
        croft_wit_mailbox_reply_dispose(&reply);
        croft_wit_mailbox_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_mailbox_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_MAILBOX_COMMAND_RECV;
    command.val.recv.mailbox = mailbox;
    if (croft_wit_mailbox_runtime_dispatch(runtime, &command, &reply) != 0
            || !expect_recv_ok(&reply, &payload, &payload_len)) {
        fprintf(stderr, "example_wit_mailbox_ping: recv failed\n");
        croft_wit_mailbox_reply_dispose(&reply);
        croft_wit_mailbox_runtime_destroy(runtime);
        return 1;
    }

    printf("mailbox=\"%.*s\"\n", (int)payload_len, (const char*)payload);
    if (payload_len != 4u || memcmp(payload, "ping", 4u) != 0) {
        croft_wit_mailbox_reply_dispose(&reply);
        croft_wit_mailbox_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_mailbox_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_MAILBOX_COMMAND_DROP;
    command.val.drop.mailbox = mailbox;
    if (croft_wit_mailbox_runtime_dispatch(runtime, &command, &reply) != 0
            || !expect_status_ok(&reply)) {
        fprintf(stderr, "example_wit_mailbox_ping: drop failed\n");
        croft_wit_mailbox_reply_dispose(&reply);
        croft_wit_mailbox_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_mailbox_reply_dispose(&reply);

    croft_wit_mailbox_runtime_destroy(runtime);
    return 0;
}
