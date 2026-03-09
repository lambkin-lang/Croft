#include "croft/host_fs.h"
#include "croft/wit_host_fs_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef CROFT_WIT_FS_SAMPLE_PATH
#define CROFT_WIT_FS_SAMPLE_PATH "tests/fixtures/wit_host_fs_sample.txt"
#endif

static int expect_file_ok(const SapWitHostFsReply* reply, SapWitHostFsFileResource* handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_FS_REPLY_FILE
            || !reply->val.file.is_v_ok) {
        return 0;
    }
    *handle_out = reply->val.file.v_val.ok.v;
    return 1;
}

static int expect_status_ok(const SapWitHostFsReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_FS_REPLY_STATUS
        && reply->val.status.is_v_ok;
}

int main(int argc, char** argv)
{
    croft_wit_host_fs_runtime* runtime;
    SapWitHostFsCommand command = {0};
    SapWitHostFsReply reply = {0};
    SapWitHostFsFileResource file = SAP_WIT_HOST_FS_FILE_RESOURCE_INVALID;

    host_fs_init(argc > 0 ? argv[0] : NULL);

    runtime = croft_wit_host_fs_runtime_create();
    if (!runtime) {
        fprintf(stderr, "example_wit_fs_read: runtime init failed\n");
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_FS_COMMAND_OPEN;
    command.val.open.path_data = (const uint8_t*)CROFT_WIT_FS_SAMPLE_PATH;
    command.val.open.path_len = (uint32_t)strlen(CROFT_WIT_FS_SAMPLE_PATH);
    if (croft_wit_host_fs_runtime_dispatch(runtime, &command, &reply) != 0
            || !expect_file_ok(&reply, &file)) {
        fprintf(stderr, "example_wit_fs_read: open failed\n");
        croft_wit_host_fs_reply_dispose(&reply);
        croft_wit_host_fs_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_host_fs_reply_dispose(&reply);

    command.case_tag = SAP_WIT_HOST_FS_COMMAND_READ_ALL;
    command.val.read_all.file = file;
    if (croft_wit_host_fs_runtime_dispatch(runtime, &command, &reply) != 0
            || reply.case_tag != SAP_WIT_HOST_FS_REPLY_READ
            || !reply.val.read.is_v_ok) {
        fprintf(stderr, "example_wit_fs_read: read failed\n");
        croft_wit_host_fs_reply_dispose(&reply);
        croft_wit_host_fs_runtime_destroy(runtime);
        return 1;
    }

    printf("fs=\"%.*s\"\n", (int)reply.val.read.v_val.ok.v_len, (const char*)reply.val.read.v_val.ok.v_data);
    croft_wit_host_fs_reply_dispose(&reply);

    command.case_tag = SAP_WIT_HOST_FS_COMMAND_CLOSE;
    command.val.close.file = file;
    if (croft_wit_host_fs_runtime_dispatch(runtime, &command, &reply) != 0
            || !expect_status_ok(&reply)) {
        fprintf(stderr, "example_wit_fs_read: close failed\n");
        croft_wit_host_fs_reply_dispose(&reply);
        croft_wit_host_fs_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_host_fs_reply_dispose(&reply);

    croft_wit_host_fs_runtime_destroy(runtime);
    return 0;
}
