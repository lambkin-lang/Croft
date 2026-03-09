#include "croft/wit_host_window_runtime.h"

#include "croft/host_ui.h"
#if defined(__APPLE__)
#include "croft/host_gesture.h"
#endif

#include <stdlib.h>
#include <string.h>

#define CROFT_WIT_HOST_WINDOW_EVENT_CAP 128u

struct croft_wit_host_window_runtime {
    uint8_t live;
    SapWitHostWindowEvent events[CROFT_WIT_HOST_WINDOW_EVENT_CAP];
    uint32_t event_head;
    uint32_t event_count;
};

static croft_wit_host_window_runtime* g_window_runtime = NULL;

static void croft_wit_host_window_reply_zero(SapWitHostWindowReply* reply)
{
    if (!reply) {
        return;
    }
    memset(reply, 0, sizeof(*reply));
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

static void croft_wit_host_window_reply_window_ok(SapWitHostWindowReply* reply, SapWitHostWindowResource handle)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_REPLY_WINDOW;
    reply->val.window.is_v_ok = 1u;
    reply->val.window.v_val.ok.v = handle;
}

static void croft_wit_host_window_reply_window_err(SapWitHostWindowReply* reply, const char* err)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_REPLY_WINDOW;
    reply->val.window.is_v_ok = 0u;
    croft_wit_set_string_view(err,
                              &reply->val.window.v_val.err.v_data,
                              &reply->val.window.v_val.err.v_len);
}

static void croft_wit_host_window_reply_status_ok(SapWitHostWindowReply* reply)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_REPLY_STATUS;
    reply->val.status.is_v_ok = 1u;
}

static void croft_wit_host_window_reply_status_err(SapWitHostWindowReply* reply, const char* err)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_REPLY_STATUS;
    reply->val.status.is_v_ok = 0u;
    croft_wit_set_string_view(err,
                              &reply->val.status.v_val.err.v_data,
                              &reply->val.status.v_val.err.v_len);
}

static void croft_wit_host_window_reply_event_ok(SapWitHostWindowReply* reply, const SapWitHostWindowEvent* event)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_REPLY_EVENT;
    reply->val.event.is_v_ok = 1u;
    reply->val.event.v_val.ok.has_v = 1u;
    reply->val.event.v_val.ok.v = *event;
}

static void croft_wit_host_window_reply_event_empty(SapWitHostWindowReply* reply)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_REPLY_EVENT;
    reply->val.event.is_v_ok = 1u;
    reply->val.event.v_val.ok.has_v = 0u;
}

static void croft_wit_host_window_reply_event_err(SapWitHostWindowReply* reply, const char* err)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_REPLY_EVENT;
    reply->val.event.is_v_ok = 0u;
    croft_wit_set_string_view(err,
                              &reply->val.event.v_val.err.v_data,
                              &reply->val.event.v_val.err.v_len);
}

static void croft_wit_host_window_reply_bool_ok(SapWitHostWindowReply* reply, uint8_t value)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_REPLY_SHOULD_CLOSE;
    reply->val.should_close.is_v_ok = 1u;
    reply->val.should_close.v_val.ok.v = value ? 1u : 0u;
}

static void croft_wit_host_window_reply_bool_err(SapWitHostWindowReply* reply, const char* err)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_REPLY_SHOULD_CLOSE;
    reply->val.should_close.is_v_ok = 0u;
    croft_wit_set_string_view(err,
                              &reply->val.should_close.v_val.err.v_data,
                              &reply->val.should_close.v_val.err.v_len);
}

static void croft_wit_host_window_reply_size_ok(SapWitHostWindowReply* reply, uint32_t width, uint32_t height)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_REPLY_SIZE;
    reply->val.size.is_v_ok = 1u;
    reply->val.size.v_val.ok.v.width = width;
    reply->val.size.v_val.ok.v.height = height;
}

static void croft_wit_host_window_reply_size_err(SapWitHostWindowReply* reply, const char* err)
{
    croft_wit_host_window_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_REPLY_SIZE;
    reply->val.size.is_v_ok = 0u;
    croft_wit_set_string_view(err,
                              &reply->val.size.v_val.err.v_data,
                              &reply->val.size.v_val.err.v_len);
}

static void croft_wit_host_window_enqueue(const SapWitHostWindowEvent* event)
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
    SapWitHostWindowEvent event = {0};

    switch (event_type) {
        case CROFT_UI_EVENT_KEY:
            event.case_tag = SAP_WIT_HOST_WINDOW_EVENT_KEY;
            event.val.key.key = arg0;
            event.val.key.action = arg1;
            event.val.key.modifiers = host_ui_get_modifiers();
            croft_wit_host_window_enqueue(&event);
            break;
        case CROFT_UI_EVENT_MOUSE:
            event.case_tag = SAP_WIT_HOST_WINDOW_EVENT_MOUSE;
            event.val.mouse.button = arg0;
            event.val.mouse.action = arg1;
            event.val.mouse.modifiers = host_ui_get_modifiers();
            croft_wit_host_window_enqueue(&event);
            break;
        case CROFT_UI_EVENT_SCROLL:
            event.case_tag = SAP_WIT_HOST_WINDOW_EVENT_SCROLL;
            event.val.scroll.x_milli = arg0;
            event.val.scroll.y_milli = arg1;
            croft_wit_host_window_enqueue(&event);
            break;
        case CROFT_UI_EVENT_CURSOR_POS:
            event.case_tag = SAP_WIT_HOST_WINDOW_EVENT_CURSOR;
            event.val.cursor.x_milli = arg0;
            event.val.cursor.y_milli = arg1;
            croft_wit_host_window_enqueue(&event);
            break;
        case CROFT_UI_EVENT_CHAR:
            event.case_tag = SAP_WIT_HOST_WINDOW_EVENT_CHAR_EVENT;
            event.val.char_event.codepoint = (uint32_t)arg0;
            croft_wit_host_window_enqueue(&event);
            break;
        case CROFT_UI_EVENT_ZOOM_GESTURE:
            event.case_tag = SAP_WIT_HOST_WINDOW_EVENT_ZOOM;
            event.val.zoom.delta_micros = arg0;
            croft_wit_host_window_enqueue(&event);
            break;
        default:
            break;
    }
}

static int croft_wit_host_window_valid(const croft_wit_host_window_runtime* runtime,
                                       SapWitHostWindowResource handle)
{
    return runtime && runtime->live && handle == (SapWitHostWindowResource)1u;
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

void* croft_wit_host_window_runtime_native_window(croft_wit_host_window_runtime* runtime,
                                                  SapWitHostWindowResource window)
{
    if (!croft_wit_host_window_valid(runtime, window)) {
        return NULL;
    }

    return host_ui_get_native_window();
}

static int32_t croft_wit_host_window_dispatch_open(croft_wit_host_window_runtime* runtime,
                                                   const SapWitHostWindowOpen* request,
                                                   SapWitHostWindowReply* reply_out)
{
    char* title = NULL;

    if (!runtime || !request || !reply_out) {
        return -1;
    }
    if (runtime->live || g_window_runtime) {
        croft_wit_host_window_reply_window_err(reply_out, "busy");
        return 0;
    }

    title = (char*)malloc((size_t)request->title_len + 1u);
    if (!title) {
        croft_wit_host_window_reply_window_err(reply_out, "internal");
        return 0;
    }
    if (request->title_len > 0u) {
        memcpy(title, request->title_data, request->title_len);
    }
    title[request->title_len] = '\0';

    if (host_ui_init() != 0 || host_ui_create_window(request->width, request->height, title) != 0) {
        free(title);
        host_ui_terminate();
        croft_wit_host_window_reply_window_err(reply_out, "unavailable");
        return 0;
    }
    free(title);

    runtime->live = 1u;
    runtime->event_head = 0u;
    runtime->event_count = 0u;
    g_window_runtime = runtime;
    host_ui_set_event_callback(croft_wit_host_window_event_cb);
#if defined(__APPLE__)
    host_gesture_mac_init(host_ui_get_native_window(), (void*)croft_wit_host_window_event_cb);
#endif
    croft_wit_host_window_reply_window_ok(reply_out, (SapWitHostWindowResource)1u);
    return 0;
}

static int32_t croft_wit_host_window_dispatch_close(croft_wit_host_window_runtime* runtime,
                                                    const SapWitHostWindowClose* request,
                                                    SapWitHostWindowReply* reply_out)
{
    if (!croft_wit_host_window_valid(runtime, request ? request->window : 0u)) {
        croft_wit_host_window_reply_status_err(reply_out, "invalid-handle");
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
                                                   const SapWitHostWindowPoll* request,
                                                   SapWitHostWindowReply* reply_out)
{
    if (!croft_wit_host_window_valid(runtime, request ? request->window : 0u)) {
        croft_wit_host_window_reply_status_err(reply_out, "invalid-handle");
        return 0;
    }

    host_ui_poll_events();
    croft_wit_host_window_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_window_dispatch_next_event(croft_wit_host_window_runtime* runtime,
                                                         const SapWitHostWindowNextEvent* request,
                                                         SapWitHostWindowReply* reply_out)
{
    SapWitHostWindowEvent event;

    if (!croft_wit_host_window_valid(runtime, request ? request->window : 0u)) {
        croft_wit_host_window_reply_event_err(reply_out, "invalid-handle");
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
                                                           const SapWitHostWindowShouldClose* request,
                                                           SapWitHostWindowReply* reply_out)
{
    if (!croft_wit_host_window_valid(runtime, request ? request->window : 0u)) {
        croft_wit_host_window_reply_bool_err(reply_out, "invalid-handle");
        return 0;
    }

    croft_wit_host_window_reply_bool_ok(reply_out, (uint8_t)host_ui_should_close());
    return 0;
}

static int32_t croft_wit_host_window_dispatch_framebuffer_size(
    croft_wit_host_window_runtime* runtime,
    const SapWitHostWindowFramebufferSize* request,
    SapWitHostWindowReply* reply_out)
{
    uint32_t width = 0u;
    uint32_t height = 0u;

    if (!croft_wit_host_window_valid(runtime, request ? request->window : 0u)) {
        croft_wit_host_window_reply_size_err(reply_out, "invalid-handle");
        return 0;
    }

    host_ui_get_framebuffer_size(&width, &height);
    croft_wit_host_window_reply_size_ok(reply_out, width, height);
    return 0;
}

int32_t croft_wit_host_window_runtime_dispatch(croft_wit_host_window_runtime* runtime,
                                               const SapWitHostWindowCommand* command,
                                               SapWitHostWindowReply* reply_out)
{
    if (!runtime || !command || !reply_out) {
        return -1;
    }

    switch (command->case_tag) {
        case SAP_WIT_HOST_WINDOW_COMMAND_OPEN:
            return croft_wit_host_window_dispatch_open(runtime, &command->val.open, reply_out);
        case SAP_WIT_HOST_WINDOW_COMMAND_CLOSE:
            return croft_wit_host_window_dispatch_close(runtime, &command->val.close, reply_out);
        case SAP_WIT_HOST_WINDOW_COMMAND_POLL:
            return croft_wit_host_window_dispatch_poll(runtime, &command->val.poll, reply_out);
        case SAP_WIT_HOST_WINDOW_COMMAND_NEXT_EVENT:
            return croft_wit_host_window_dispatch_next_event(runtime, &command->val.next_event,
                                                             reply_out);
        case SAP_WIT_HOST_WINDOW_COMMAND_SHOULD_CLOSE:
            return croft_wit_host_window_dispatch_should_close(
                runtime, &command->val.should_close, reply_out);
        case SAP_WIT_HOST_WINDOW_COMMAND_FRAMEBUFFER_SIZE:
            return croft_wit_host_window_dispatch_framebuffer_size(
                runtime, &command->val.framebuffer_size, reply_out);
        default:
            return -1;
    }
}
