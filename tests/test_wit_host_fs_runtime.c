#include "croft/host_fs.h"
#include "croft/wit_host_fs_runtime.h"

#include <stdint.h>
#include <string.h>

#ifndef CROFT_WIT_FS_SAMPLE_PATH
#define CROFT_WIT_FS_SAMPLE_PATH "tests/fixtures/wit_host_fs_sample.txt"
#endif
#define WIT_FS_SAMPLE_TEXT "wit host fs sample\n"

static int wit_host_fs_expect_file_ok(const SapWitHostFsReply* reply, SapWitHostFsFileResource* handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_FS_REPLY_FILE
            || reply->val.file.case_tag != SAP_WIT_HOST_FS_FILE_OP_RESULT_OK) {
        return 0;
    }
    *handle_out = reply->val.file.val.ok;
    return 1;
}

static int wit_host_fs_expect_status_ok(const SapWitHostFsReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_FS_REPLY_STATUS
        && reply->val.status.case_tag == SAP_WIT_HOST_FS_STATUS_OK;
}

int test_wit_host_fs_runtime_read_fixture(void)
{
    croft_wit_host_fs_runtime* runtime;
    SapWitHostFsCommand command = {0};
    SapWitHostFsReply reply = {0};
    SapWitHostFsFileResource file = SAP_WIT_HOST_FS_FILE_RESOURCE_INVALID;

    host_fs_init(NULL);

    runtime = croft_wit_host_fs_runtime_create();
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_FS_COMMAND_OPEN;
    command.val.open.path_data = (const uint8_t*)CROFT_WIT_FS_SAMPLE_PATH;
    command.val.open.path_len = (uint32_t)strlen(CROFT_WIT_FS_SAMPLE_PATH);
    if (croft_wit_host_fs_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_host_fs_expect_file_ok(&reply, &file)) {
        croft_wit_host_fs_reply_dispose(&reply);
        croft_wit_host_fs_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_host_fs_reply_dispose(&reply);

    command.case_tag = SAP_WIT_HOST_FS_COMMAND_READ_ALL;
    command.val.read_all.file = file;
    if (croft_wit_host_fs_runtime_dispatch(runtime, &command, &reply) != 0
            || reply.case_tag != SAP_WIT_HOST_FS_REPLY_READ
            || reply.val.read.case_tag != SAP_WIT_HOST_FS_FILE_READ_RESULT_OK) {
        croft_wit_host_fs_reply_dispose(&reply);
        croft_wit_host_fs_runtime_destroy(runtime);
        return 1;
    }
    if (reply.val.read.val.ok.len != (uint32_t)strlen(WIT_FS_SAMPLE_TEXT)
            || memcmp(reply.val.read.val.ok.data, WIT_FS_SAMPLE_TEXT,
                      strlen(WIT_FS_SAMPLE_TEXT)) != 0) {
        croft_wit_host_fs_reply_dispose(&reply);
        croft_wit_host_fs_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_host_fs_reply_dispose(&reply);

    command.case_tag = SAP_WIT_HOST_FS_COMMAND_CLOSE;
    command.val.close.file = file;
    if (croft_wit_host_fs_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_host_fs_expect_status_ok(&reply)) {
        croft_wit_host_fs_reply_dispose(&reply);
        croft_wit_host_fs_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_host_fs_reply_dispose(&reply);

    croft_wit_host_fs_runtime_destroy(runtime);
    return 0;
}
