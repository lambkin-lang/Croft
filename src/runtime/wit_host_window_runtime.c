#include "croft/wit_host_window_runtime.h"

#include "croft/host_ui.h"

#include <stdlib.h>
#include <string.h>

#define CROFT_WIT_HOST_WINDOW_EVENT_CAP 128u

struct croft_wit_host_window_runtime {
    uint8_t live;
    SapWitHostWindowWindowEvent events[CROFT_WIT_HOST_WINDOW_EVENT_CAP];
    uint32_t event_head;
    uint32_t event_count;
};

static croft_wit_host_window_runtime* g_window_runtime = NULL;

static void croft_wit_host_window_reply_zero(SapWitHostWindowWindowReply* reply)
{
    if (!reply) {
        return;
    }
    memset(reply, 0, sizeof(*reply));
}

static void croft_wit_host_window_reply_window_ok(SapWitHostWindowWindowReply* reply, SapWitHostWindowWindowResource handle)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_WINDOW_REPLY_WINDOW;
    reply->val.window.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_OP_RESULT_OK;
    reply->val.window.val.ok = handle;
}

static void croft_wit_host_window_reply_window_err(SapWitHostWindowWindowReply* reply, uint8_t err)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_WINDOW_REPLY_WINDOW;
    reply->val.window.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_OP_RESULT_ERR;
    reply->val.window.val.err = err;
}

static void croft_wit_host_window_reply_status_ok(SapWitHostWindowWindowReply* reply)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_WINDOW_REPLY_STATUS;
    reply->val.status.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_STATUS_OK;
}

static void croft_wit_host_window_reply_status_err(SapWitHostWindowWindowReply* reply, uint8_t err)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_WINDOW_REPLY_STATUS;
    reply->val.status.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_STATUS_ERR;
    reply->val.status.val.err = err;
}

static void croft_wit_host_window_reply_event_ok(SapWitHostWindowWindowReply* reply, const SapWitHostWindowWindowEvent* event)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_WINDOW_REPLY_EVENT;
    reply->val.event.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_EVENT_RESULT_OK;
    reply->val.event.val.ok = *event;
}

static void croft_wit_host_window_reply_event_empty(SapWitHostWindowWindowReply* reply)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_WINDOW_REPLY_EVENT;
    reply->val.event.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_EVENT_RESULT_EMPTY;
}

static void croft_wit_host_window_reply_event_err(SapWitHostWindowWindowReply* reply, uint8_t err)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_WINDOW_REPLY_EVENT;
    reply->val.event.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_EVENT_RESULT_ERR;
    reply->val.event.val.err = err;
}

static void croft_wit_host_window_reply_bool_ok(SapWitHostWindowWindowReply* reply, uint8_t value)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_WINDOW_REPLY_SHOULD_CLOSE;
    reply->val.should_close.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_BOOL_RESULT_OK;
    reply->val.should_close.val.ok = value ? 1u : 0u;
}

static void croft_wit_host_window_reply_bool_err(SapWitHostWindowWindowReply* reply, uint8_t err)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_WINDOW_REPLY_SHOULD_CLOSE;
    reply->val.should_close.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_BOOL_RESULT_ERR;
    reply->val.should_close.val.err = err;
}

static void croft_wit_host_window_reply_size_ok(SapWitHostWindowWindowReply* reply, uint32_t width, uint32_t height)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_WINDOW_REPLY_SIZE;
    reply->val.size.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_SIZE_RESULT_OK;
    reply->val.size.val.ok.width = width;
    reply->val.size.val.ok.height = height;
}

static void croft_wit_host_window_reply_size_err(SapWitHostWindowWindowReply* reply, uint8_t err)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_WINDOW_REPLY_SIZE;
    reply->val.size.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_SIZE_RESULT_ERR;
    reply->val.size.val.err = err;
}

static void croft_wit_host_window_enqueue(const SapWitHostWindowWindowEvent* event)
{
    uint32_t slot;

    if (!g_window_runtime || !event) {
        return;
    }
    if (g_window_runtime->event_count >= CROFT_WIT_HOST_WINDOW_EVENT_CAP) {
        return;
    }

    slot = (g_window_runtime->event_head + g_window_runtime->event_count) % CROFT_WIT_HOST_WINDOW_EVENT_CAP;
    g_window_runtime->events[slot] = *event;
    g_window_runtime->event_count++;
}

static void croft_wit_host_window_event_cb(int32_t event_type, int32_t arg0, int32_t arg1)
{
    SapWitHostWindowWindowEvent event = {0};

    switch (event_type) {
        case CROFT_UI_EVENT_KEY:
            event.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_EVENT_KEY;
            event.val.key.key = arg0;
            event.val.key.action = arg1;
            event.val.key.modifiers = host_ui_get_modifiers();
            croft_wit_host_window_enqueue(&event);
            break;
        case CROFT_UI_EVENT_MOUSE:
            event.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_EVENT_MOUSE;
            event.val.mouse.button = arg0;
            event.val.mouse.action = arg1;
            event.val.mouse.modifiers = host_ui_get_modifiers();
            croft_wit_host_window_enqueue(&event);
            break;
        case CROFT_UI_EVENT_SCROLL:
            event.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_EVENT_SCROLL;
            event.val.scroll.x_milli = arg0;
            event.val.scroll.y_milli = arg1;
            croft_wit_host_window_enqueue(&event);
            break;
        case CROFT_UI_EVENT_CURSOR_POS:
            event.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_EVENT_CURSOR;
            event.val.cursor.x_milli = arg0;
            event.val.cursor.y_milli = arg1;
            croft_wit_host_window_enqueue(&event);
            break;
        case CROFT_UI_EVENT_CHAR:
            event.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_EVENT_CHAR_EVENT;
            event.val.char_event.codepoint = (uint32_t)arg0;
            croft_wit_host_window_enqueue(&event);
            break;
        default:
            break;
    }
}

static int croft_wit_host_window_valid(const croft_wit_host_window_runtime* runtime,
                                       SapWitHostWindowWindowResource handle)
{
    return runtime && runtime->live && handle == (SapWitHostWindowWindowResource)1u;
}

croft_wit_host_window_runtime* croft_wit_host_window_runtime_create(void)
{
    return (croft_wit_host_window_runtime*)calloc(1u, sizeof(croft_wit_host_window_runtime));
}

void croft_wit_host_window_runtime_destroy(croft_wit_host_window_runtime* runtime)
{
    if (!runtime) {
        return;
    }

    if (runtime->live) {
        host_ui_set_event_callback(NULL);
        host_ui_terminate();
    }
    if (g_window_runtime == runtime) {
        g_window_runtime = NULL;
    }

    free(runtime);
}

static int32_t croft_wit_host_window_dispatch_open(croft_wit_host_window_runtime* runtime,
                                                   const SapWitHostWindowWindowOpen* request,
                                                   SapWitHostWindowWindowReply* reply_out)
{
    char* title = NULL;

    if (!runtime || !request || !reply_out) {
        return -1;
    }
    if (runtime->live || g_window_runtime) {
        croft_wit_host_window_reply_window_err(reply_out, SAP_WIT_HOST_WINDOW_WINDOW_ERROR_BUSY);
        return 0;
    }

    title = (char*)malloc((size_t)request->title_len + 1u);
    if (!title) {
        croft_wit_host_window_reply_window_err(reply_out, SAP_WIT_HOST_WINDOW_WINDOW_ERROR_INTERNAL);
        return 0;
    }
    if (request->title_len > 0u) {
        memcpy(title, request->title_data, request->title_len);
    }
    title[request->title_len] = '\0';

    if (host_ui_init() != 0 || host_ui_create_window(request->width, request->height, title) != 0) {
        free(title);
        host_ui_terminate();
        croft_wit_host_window_reply_window_err(reply_out, SAP_WIT_HOST_WINDOW_WINDOW_ERROR_UNAVAILABLE);
        return 0;
    }
    free(title);

    runtime->live = 1u;
    runtime->event_head = 0u;
    runtime->event_count = 0u;
    g_window_runtime = runtime;
    host_ui_set_event_callback(croft_wit_host_window_event_cb);
    croft_wit_host_window_reply_window_ok(reply_out, (SapWitHostWindowWindowResource)1u);
    return 0;
}

static int32_t croft_wit_host_window_dispatch_close(croft_wit_host_window_runtime* runtime,
                                                    const SapWitHostWindowWindowClose* request,
                                                    SapWitHostWindowWindowReply* reply_out)
{
    if (!croft_wit_host_window_valid(runtime, request ? request->window : 0u)) {
        croft_wit_host_window_reply_status_err(reply_out, SAP_WIT_HOST_WINDOW_WINDOW_ERROR_INVALID_HANDLE);
        return 0;
    }

    host_ui_set_event_callback(NULL);
    host_ui_terminate();
    runtime->live = 0u;
    runtime->event_head = 0u;
    runtime->event_count = 0u;
    if (g_window_runtime == runtime) {
        g_window_runtime = NULL;
    }

    croft_wit_host_window_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_window_dispatch_poll(croft_wit_host_window_runtime* runtime,
                                                   const SapWitHostWindowWindowPoll* request,
                                                   SapWitHostWindowWindowReply* reply_out)
{
    if (!croft_wit_host_window_valid(runtime, request ? request->window : 0u)) {
        croft_wit_host_window_reply_status_err(reply_out, SAP_WIT_HOST_WINDOW_WINDOW_ERROR_INVALID_HANDLE);
        return 0;
    }

    host_ui_poll_events();
    croft_wit_host_window_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_window_dispatch_next_event(croft_wit_host_window_runtime* runtime,
                                                         const SapWitHostWindowWindowNextEvent* request,
                                                         SapWitHostWindowWindowReply* reply_out)
{
    SapWitHostWindowWindowEvent event;

    if (!croft_wit_host_window_valid(runtime, request ? request->window : 0u)) {
        croft_wit_host_window_reply_event_err(reply_out, SAP_WIT_HOST_WINDOW_WINDOW_ERROR_INVALID_HANDLE);
        return 0;
    }
    if (runtime->event_count == 0u) {
        croft_wit_host_window_reply_event_empty(reply_out);
        return 0;
    }

    event = runtime->events[runtime->event_head];
    runtime->event_head = (runtime->event_head + 1u) % CROFT_WIT_HOST_WINDOW_EVENT_CAP;
    runtime->event_count--;
    croft_wit_host_window_reply_event_ok(reply_out, &event);
    return 0;
}

static int32_t croft_wit_host_window_dispatch_should_close(croft_wit_host_window_runtime* runtime,
                                                           const SapWitHostWindowWindowShouldClose* request,
                                                           SapWitHostWindowWindowReply* reply_out)
{
    if (!croft_wit_host_window_valid(runtime, request ? request->window : 0u)) {
        croft_wit_host_window_reply_bool_err(reply_out, SAP_WIT_HOST_WINDOW_WINDOW_ERROR_INVALID_HANDLE);
        return 0;
    }

    croft_wit_host_window_reply_bool_ok(reply_out, (uint8_t)host_ui_should_close());
    return 0;
}

static int32_t croft_wit_host_window_dispatch_framebuffer_size(
    croft_wit_host_window_runtime* runtime,
    const SapWitHostWindowWindowFramebufferSize* request,
    SapWitHostWindowWindowReply* reply_out)
{
    uint32_t width = 0u;
    uint32_t height = 0u;

    if (!croft_wit_host_window_valid(runtime, request ? request->window : 0u)) {
        croft_wit_host_window_reply_size_err(reply_out, SAP_WIT_HOST_WINDOW_WINDOW_ERROR_INVALID_HANDLE);
        return 0;
    }

    host_ui_get_framebuffer_size(&width, &height);
    croft_wit_host_window_reply_size_ok(reply_out, width, height);
    return 0;
}

int32_t croft_wit_host_window_runtime_dispatch(croft_wit_host_window_runtime* runtime,
                                               const SapWitHostWindowWindowCommand* command,
                                               SapWitHostWindowWindowReply* reply_out)
{
    if (!runtime || !command || !reply_out) {
        return -1;
    }

    switch (command->case_tag) {
        case SAP_WIT_HOST_WINDOW_WINDOW_COMMAND_OPEN:
            return croft_wit_host_window_dispatch_open(runtime, &command->val.open, reply_out);
        case SAP_WIT_HOST_WINDOW_WINDOW_COMMAND_CLOSE:
            return croft_wit_host_window_dispatch_close(runtime, &command->val.close, reply_out);
        case SAP_WIT_HOST_WINDOW_WINDOW_COMMAND_POLL:
            return croft_wit_host_window_dispatch_poll(runtime, &command->val.poll, reply_out);
        case SAP_WIT_HOST_WINDOW_WINDOW_COMMAND_NEXT_EVENT:
            return croft_wit_host_window_dispatch_next_event(runtime, &command->val.next_event,
                                                             reply_out);
        case SAP_WIT_HOST_WINDOW_WINDOW_COMMAND_SHOULD_CLOSE:
            return croft_wit_host_window_dispatch_should_close(
                runtime, &command->val.should_close, reply_out);
        case SAP_WIT_HOST_WINDOW_WINDOW_COMMAND_FRAMEBUFFER_SIZE:
            return croft_wit_host_window_dispatch_framebuffer_size(
                runtime, &command->val.framebuffer_size, reply_out);
        default:
            return -1;
    }
}
