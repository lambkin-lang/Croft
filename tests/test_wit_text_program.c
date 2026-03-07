#include "croft/wit_text_program.h"
#include "croft/wit_text_runtime.h"

#include <string.h>

int test_wit_text_program_prepend(void)
{
    croft_wit_text_runtime* runtime;
    croft_wit_text_program_host host = {0};
    croft_wit_owned_bytes bytes = {0};
    int rc = 1;

    runtime = croft_wit_text_runtime_create(NULL);
    if (!runtime) {
        return 1;
    }

    host.userdata = runtime;
    host.dispatch = (croft_wit_text_program_dispatch_fn)croft_wit_text_runtime_dispatch;
    host.dispose_reply = croft_wit_text_reply_dispose;

    if (croft_wit_text_program_prepend(&host,
                                       (const uint8_t*)"small binaries",
                                       14u,
                                       (const uint8_t*)"Big analysis, ",
                                       14u,
                                       &bytes) != 0) {
        goto cleanup;
    }
    if (bytes.len != 28u || memcmp(bytes.data, "Big analysis, small binaries", 28u) != 0) {
        goto cleanup;
    }

    rc = 0;

cleanup:
    croft_wit_owned_bytes_dispose(&bytes);
    croft_wit_text_runtime_destroy(runtime);
    return rc;
}
