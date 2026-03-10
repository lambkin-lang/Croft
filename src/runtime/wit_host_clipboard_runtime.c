#include "croft/wit_host_clipboard_runtime.h"

#include "croft/host_ui.h"

#include <stdlib.h>
#include <string.h>

struct croft_wit_host_clipboard_runtime {
    uint8_t unused;
};

static void croft_wit_host_clipboard_reply_zero(SapWitHostClipboardReply* reply)
{
    sap_wit_zero_host_clipboard_reply(reply);
}

static void croft_wit_set_string_view(const char* text,
                                      const uint8_t** data_out,
                                      uint32_t* len_out)
{
    if (!data_out || !len_out) {
        return;
    }
    if (!text) {
        text = "";
    }
    *data_out = (const uint8_t*)text;
    *len_out = (uint32_t)strlen(text);
}

static void croft_wit_host_clipboard_reply_status_ok(SapWitHostClipboardReply* reply)
{
    croft_wit_host_clipboard_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_CLIPBOARD_REPLY_STATUS;
    reply->val.status.is_v_ok = 1u;
}

static void croft_wit_host_clipboard_reply_status_err(SapWitHostClipboardReply* reply, const char* err)
{
    croft_wit_host_clipboard_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_CLIPBOARD_REPLY_STATUS;
    reply->val.status.is_v_ok = 0u;
    croft_wit_set_string_view(err,
                              &reply->val.status.v_val.err.v_data,
                              &reply->val.status.v_val.err.v_len);
}

static void croft_wit_host_clipboard_reply_text_ok(SapWitHostClipboardReply* reply,
                                                   uint8_t* data,
                                                   uint32_t len)
{
    croft_wit_host_clipboard_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_CLIPBOARD_REPLY_TEXT;
    reply->val.text.is_v_ok = 1u;
    reply->val.text.v_val.ok.has_v = 1u;
    reply->val.text.v_val.ok.v_data = data;
    reply->val.text.v_val.ok.v_len = len;
}

static void croft_wit_host_clipboard_reply_text_empty(SapWitHostClipboardReply* reply)
{
    croft_wit_host_clipboard_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_CLIPBOARD_REPLY_TEXT;
    reply->val.text.is_v_ok = 1u;
    reply->val.text.v_val.ok.has_v = 0u;
}

static void croft_wit_host_clipboard_reply_text_err(SapWitHostClipboardReply* reply, const char* err)
{
    croft_wit_host_clipboard_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_CLIPBOARD_REPLY_TEXT;
    reply->val.text.is_v_ok = 0u;
    croft_wit_set_string_view(err, &reply->val.text.v_val.err.v_data, &reply->val.text.v_val.err.v_len);
}

croft_wit_host_clipboard_runtime* croft_wit_host_clipboard_runtime_create(void)
{
    return (croft_wit_host_clipboard_runtime*)calloc(1u, sizeof(croft_wit_host_clipboard_runtime));
}

void croft_wit_host_clipboard_runtime_destroy(croft_wit_host_clipboard_runtime* runtime)
{
    free(runtime);
}

void croft_wit_host_clipboard_reply_dispose(SapWitHostClipboardReply* reply)
{
    sap_wit_dispose_host_clipboard_reply(reply);
}

static int32_t croft_wit_host_clipboard_dispatch_set_text(void* ctx,
                                                          const SapWitHostClipboardSetText* request,
                                                          SapWitHostClipboardReply* reply_out)
{
    (void)ctx;
    if (!request || !reply_out) {
        return -1;
    }
    if (host_ui_set_clipboard_text((const char*)request->utf8_data, request->utf8_len) != 0) {
        croft_wit_host_clipboard_reply_status_err(reply_out, "unavailable");
        return 0;
    }
    croft_wit_host_clipboard_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_clipboard_dispatch_get_text(void* ctx,
                                                          SapWitHostClipboardReply* reply_out)
{
    char* text = NULL;
    size_t len = 0u;

    (void)ctx;
    if (!reply_out) {
        return -1;
    }
    if (host_ui_get_clipboard_text(&text, &len) != 0) {
        croft_wit_host_clipboard_reply_text_err(reply_out, "unavailable");
        return 0;
    }
    if (!text || len == 0u) {
        free(text);
        croft_wit_host_clipboard_reply_text_empty(reply_out);
        return 0;
    }
    croft_wit_host_clipboard_reply_text_ok(reply_out, (uint8_t*)text, (uint32_t)len);
    return 0;
}

static const SapWitHostClipboardDispatchOps g_croft_wit_host_clipboard_dispatch_ops = {
    .get_text = croft_wit_host_clipboard_dispatch_get_text,
    .set_text = croft_wit_host_clipboard_dispatch_set_text,
};

int32_t croft_wit_host_clipboard_runtime_dispatch(croft_wit_host_clipboard_runtime* runtime,
                                                  const SapWitHostClipboardCommand* command,
                                                  SapWitHostClipboardReply* reply_out)
{
    int32_t rc;

    if (!runtime || !command || !reply_out) {
        return -1;
    }

    rc = sap_wit_dispatch_host_clipboard(runtime,
                                         &g_croft_wit_host_clipboard_dispatch_ops,
                                         command,
                                         reply_out);
    if (rc == -1) {
        croft_wit_host_clipboard_reply_status_err(reply_out, "internal");
        return 0;
    }
    return rc;
}
