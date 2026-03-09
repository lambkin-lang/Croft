#include "croft/host_fs.h"
#include "croft/wit_host_fs_runtime.h"
#include "croft/wit_text_program.h"
#include "croft/wit_text_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int expect_fs_status_ok(const SapWitHostFsReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_FS_REPLY_STATUS
        && reply->val.status.is_v_ok;
}

static int load_input_bytes(croft_wit_host_fs_runtime* fs_runtime,
                            const char* path,
                            croft_wit_owned_bytes* out_bytes)
{
    SapWitHostFsCommand command = {0};
    SapWitHostFsReply reply = {0};
    SapWitHostFsFileResource file = SAP_WIT_HOST_FS_FILE_RESOURCE_INVALID;
    uint8_t* owned = NULL;

    if (!fs_runtime || !path || !out_bytes) {
        return -1;
    }
    memset(out_bytes, 0, sizeof(*out_bytes));

    command.case_tag = SAP_WIT_HOST_FS_COMMAND_OPEN;
    command.val.open.path_data = (const uint8_t*)path;
    command.val.open.path_len = (uint32_t)strlen(path);
    if (croft_wit_host_fs_runtime_dispatch(fs_runtime, &command, &reply) != 0
            || !expect_file_ok(&reply, &file)) {
        croft_wit_host_fs_reply_dispose(&reply);
        return -1;
    }
    croft_wit_host_fs_reply_dispose(&reply);

    command.case_tag = SAP_WIT_HOST_FS_COMMAND_READ_ALL;
    command.val.read_all.file = file;
    if (croft_wit_host_fs_runtime_dispatch(fs_runtime, &command, &reply) != 0
            || reply.case_tag != SAP_WIT_HOST_FS_REPLY_READ
            || !reply.val.read.is_v_ok) {
        croft_wit_host_fs_reply_dispose(&reply);
        command.case_tag = SAP_WIT_HOST_FS_COMMAND_CLOSE;
        command.val.close.file = file;
        if (croft_wit_host_fs_runtime_dispatch(fs_runtime, &command, &reply) == 0) {
            croft_wit_host_fs_reply_dispose(&reply);
        }
        return -1;
    }

    if (reply.val.read.v_val.ok.v_len > 0u) {
        owned = (uint8_t*)malloc((size_t)reply.val.read.v_val.ok.v_len);
        if (!owned) {
            croft_wit_host_fs_reply_dispose(&reply);
            command.case_tag = SAP_WIT_HOST_FS_COMMAND_CLOSE;
            command.val.close.file = file;
            if (croft_wit_host_fs_runtime_dispatch(fs_runtime, &command, &reply) == 0) {
                croft_wit_host_fs_reply_dispose(&reply);
            }
            return -1;
        }
        memcpy(owned, reply.val.read.v_val.ok.v_data, reply.val.read.v_val.ok.v_len);
    }
    out_bytes->data = owned;
    out_bytes->len = reply.val.read.v_val.ok.v_len;
    croft_wit_host_fs_reply_dispose(&reply);

    command.case_tag = SAP_WIT_HOST_FS_COMMAND_CLOSE;
    command.val.close.file = file;
    if (croft_wit_host_fs_runtime_dispatch(fs_runtime, &command, &reply) != 0
            || !expect_fs_status_ok(&reply)) {
        croft_wit_host_fs_reply_dispose(&reply);
        croft_wit_owned_bytes_dispose(out_bytes);
        return -1;
    }
    croft_wit_host_fs_reply_dispose(&reply);
    return 0;
}

int main(int argc, char** argv)
{
    const char* fallback = "small binaries";
    const char* prefix = "Big analysis, ";
    croft_wit_host_fs_runtime* fs_runtime = NULL;
    croft_wit_text_runtime* text_runtime = NULL;
    croft_wit_text_program_host host = {0};
    croft_wit_owned_bytes source = {0};
    croft_wit_owned_bytes result = {0};

    host_fs_init(argc > 0 ? argv[0] : NULL);

    text_runtime = croft_wit_text_runtime_create(NULL);
    fs_runtime = croft_wit_host_fs_runtime_create();
    if (!text_runtime || !fs_runtime) {
        croft_wit_host_fs_runtime_destroy(fs_runtime);
        croft_wit_text_runtime_destroy(text_runtime);
        return 1;
    }

    if (argc > 1) {
        if (load_input_bytes(fs_runtime, argv[1], &source) != 0) {
            fprintf(stderr, "example_wit_text_cli: unable to read %s\n", argv[1]);
            croft_wit_host_fs_runtime_destroy(fs_runtime);
            croft_wit_text_runtime_destroy(text_runtime);
            return 1;
        }
    } else {
        source.data = (uint8_t*)malloc(strlen(fallback));
        if (!source.data) {
            croft_wit_host_fs_runtime_destroy(fs_runtime);
            croft_wit_text_runtime_destroy(text_runtime);
            return 1;
        }
        memcpy(source.data, fallback, strlen(fallback));
        source.len = (uint32_t)strlen(fallback);
    }

    host.userdata = text_runtime;
    host.dispatch = (croft_wit_text_program_dispatch_fn)croft_wit_text_runtime_dispatch;
    host.dispose_reply = croft_wit_text_reply_dispose;

    if (croft_wit_text_program_prepend(&host,
                                       source.data,
                                       source.len,
                                       (const uint8_t*)prefix,
                                       (uint32_t)strlen(prefix),
                                       &result) != 0) {
        fprintf(stderr, "example_wit_text_cli: text pipeline failed\n");
        croft_wit_owned_bytes_dispose(&source);
        croft_wit_host_fs_runtime_destroy(fs_runtime);
        croft_wit_text_runtime_destroy(text_runtime);
        return 1;
    }

    printf("cli=\"%.*s\"\n", (int)result.len, (const char*)result.data);

    croft_wit_owned_bytes_dispose(&result);
    croft_wit_owned_bytes_dispose(&source);
    croft_wit_host_fs_runtime_destroy(fs_runtime);
    croft_wit_text_runtime_destroy(text_runtime);
    return 0;
}
