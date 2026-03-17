#include "croft/json_viewer_window_app.h"
#include "croft/wit_runtime_support.h"

#if defined(__has_include) && __has_include("generated/wit_host_clock.h")
#include "generated/wit_host_clock.h"
#include "generated/wit_host_gpu2d.h"
#include "generated/wit_host_window.h"
#else
#include "wit_host_clock.h"
#include "wit_host_gpu2d.h"
#include "wit_host_window.h"
#endif

#include <stdio.h>
#include <string.h>

#define ASSERT_JSON_VIEWER_APP(cond)                                      \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "    ASSERT failed: %s (%s:%d)\n",            \
                    #cond, __FILE__, __LINE__);                           \
            return 1;                                                     \
        }                                                                 \
    } while (0)

enum {
    CROFT_JSON_VIEWER_TEST_MOUSE_PRESS = 1,
    CROFT_JSON_VIEWER_TEST_MOUSE_LEFT = 0
};

typedef struct {
    uint32_t width;
    uint32_t height;
    SapWitHostWindowEvent events[4];
    uint32_t event_count;
    uint32_t next_event_index;
    uint64_t now_ms[4];
    uint32_t now_count;
    uint32_t next_now_index;
    uint32_t last_window_command;
    uint32_t last_gpu_command;
    uint32_t last_clock_command;
} CroftJsonViewerWindowAppMock;

static const uint8_t k_test_json[] =
    "{"
    "\"project\":\"Croft\","
    "\"features\":{\"solver\":true,\"runtime\":\"wasm3-windowed\"},"
    "\"items\":[\"alpha\",{\"nested\":true}],"
    "\"empty\":{},"
    "\"notes\":{\"viewer\":\"read-only\"}"
    "}";

static uint32_t find_path_index(const CroftJsonViewerState *state, const char *path)
{
    uint32_t i;

    if (!state || !path) {
        return UINT32_MAX;
    }
    for (i = 0u; i < state->path_count; i++) {
        if (strcmp(state->paths[i], path) == 0) {
            return i;
        }
    }
    return UINT32_MAX;
}

static void window_reply_status_ok(SapWitHostWindowReply *reply)
{
    sap_wit_zero_host_window_reply(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_REPLY_STATUS;
    reply->val.status.is_v_ok = 1u;
}

static void window_reply_window_ok(SapWitHostWindowReply *reply)
{
    sap_wit_zero_host_window_reply(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_REPLY_WINDOW;
    reply->val.window.is_v_ok = 1u;
    reply->val.window.v_val.ok.v = (SapWitHostWindowResource)1u;
}

static void window_reply_event(SapWitHostWindowReply *reply,
                               const SapWitHostWindowEvent *event)
{
    sap_wit_zero_host_window_reply(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_REPLY_EVENT;
    reply->val.event.is_v_ok = 1u;
    reply->val.event.v_val.ok.has_v = event ? 1u : 0u;
    if (event) {
        reply->val.event.v_val.ok.v = *event;
    }
}

static void window_reply_should_close(SapWitHostWindowReply *reply, uint8_t should_close)
{
    sap_wit_zero_host_window_reply(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_REPLY_SHOULD_CLOSE;
    reply->val.should_close.is_v_ok = 1u;
    reply->val.should_close.v_val.ok.v = should_close;
}

static void window_reply_size(SapWitHostWindowReply *reply, uint32_t width, uint32_t height)
{
    sap_wit_zero_host_window_reply(reply);
    reply->case_tag = SAP_WIT_HOST_WINDOW_REPLY_SIZE;
    reply->val.size.is_v_ok = 1u;
    reply->val.size.v_val.ok.v.width = width;
    reply->val.size.v_val.ok.v.height = height;
}

static int mock_window_dispatch(void *ctx, const void *command, void *reply_out)
{
    CroftJsonViewerWindowAppMock *mock = (CroftJsonViewerWindowAppMock *)ctx;
    const SapWitHostWindowCommand *window_cmd = (const SapWitHostWindowCommand *)command;
    SapWitHostWindowReply *reply = (SapWitHostWindowReply *)reply_out;

    if (!mock || !window_cmd || !reply) {
        return ERR_INVALID;
    }
    mock->last_window_command = window_cmd->case_tag;

    switch (window_cmd->case_tag) {
        case SAP_WIT_HOST_WINDOW_COMMAND_OPEN:
            window_reply_window_ok(reply);
            return ERR_OK;
        case SAP_WIT_HOST_WINDOW_COMMAND_POLL:
        case SAP_WIT_HOST_WINDOW_COMMAND_CLOSE:
            window_reply_status_ok(reply);
            return ERR_OK;
        case SAP_WIT_HOST_WINDOW_COMMAND_NEXT_EVENT:
            if (mock->next_event_index < mock->event_count) {
                window_reply_event(reply, &mock->events[mock->next_event_index++]);
            } else {
                window_reply_event(reply, NULL);
            }
            return ERR_OK;
        case SAP_WIT_HOST_WINDOW_COMMAND_SHOULD_CLOSE:
            window_reply_should_close(reply, 0u);
            return ERR_OK;
        case SAP_WIT_HOST_WINDOW_COMMAND_FRAMEBUFFER_SIZE:
            window_reply_size(reply, mock->width, mock->height);
            return ERR_OK;
        default:
            return ERR_INVALID;
    }
}

static void gpu_reply_capabilities_ok(SapWitHostGpu2dReply *reply)
{
    sap_wit_zero_host_gpu2d_reply(reply);
    reply->case_tag = SAP_WIT_HOST_GPU2D_REPLY_CAPABILITIES;
    reply->val.capabilities.is_v_ok = 1u;
    reply->val.capabilities.v_val.ok.v =
        SAP_WIT_HOST_GPU2D_CAPABILITIES_TEXT | SAP_WIT_HOST_GPU2D_CAPABILITIES_PRESENT;
}

static void gpu_reply_surface_ok(SapWitHostGpu2dReply *reply)
{
    sap_wit_zero_host_gpu2d_reply(reply);
    reply->case_tag = SAP_WIT_HOST_GPU2D_REPLY_SURFACE;
    reply->val.surface.is_v_ok = 1u;
    reply->val.surface.v_val.ok.v = (SapWitHostGpu2dSurfaceResource)1u;
}

static void gpu_reply_status_ok(SapWitHostGpu2dReply *reply)
{
    sap_wit_zero_host_gpu2d_reply(reply);
    reply->case_tag = SAP_WIT_HOST_GPU2D_REPLY_STATUS;
    reply->val.status.is_v_ok = 1u;
}

static int mock_gpu_dispatch(void *ctx, const void *command, void *reply_out)
{
    const SapWitHostGpu2dCommand *gpu_cmd = (const SapWitHostGpu2dCommand *)command;
    SapWitHostGpu2dReply *reply = (SapWitHostGpu2dReply *)reply_out;
    (void)ctx;

    if (!gpu_cmd || !reply) {
        return ERR_INVALID;
    }
    if (ctx) {
        ((CroftJsonViewerWindowAppMock *)ctx)->last_gpu_command = gpu_cmd->case_tag;
    }

    switch (gpu_cmd->case_tag) {
        case SAP_WIT_HOST_GPU2D_COMMAND_CAPABILITIES:
            gpu_reply_capabilities_ok(reply);
            return ERR_OK;
        case SAP_WIT_HOST_GPU2D_COMMAND_OPEN:
            gpu_reply_surface_ok(reply);
            return ERR_OK;
        case SAP_WIT_HOST_GPU2D_COMMAND_DROP:
        case SAP_WIT_HOST_GPU2D_COMMAND_BEGIN_FRAME:
        case SAP_WIT_HOST_GPU2D_COMMAND_CLEAR:
        case SAP_WIT_HOST_GPU2D_COMMAND_END_FRAME:
        case SAP_WIT_HOST_GPU2D_COMMAND_DRAW_RECT:
        case SAP_WIT_HOST_GPU2D_COMMAND_DRAW_TEXT:
            gpu_reply_status_ok(reply);
            return ERR_OK;
        case SAP_WIT_HOST_GPU2D_COMMAND_MEASURE_TEXT:
            sap_wit_zero_host_gpu2d_reply(reply);
            reply->case_tag = SAP_WIT_HOST_GPU2D_REPLY_MEASURE;
            reply->val.measure.is_v_ok = 1u;
            reply->val.measure.v_val.ok.v = 120.0f;
            return ERR_OK;
        default:
            return ERR_INVALID;
    }
}

static int mock_clock_dispatch(void *ctx, const void *command, void *reply_out)
{
    CroftJsonViewerWindowAppMock *mock = (CroftJsonViewerWindowAppMock *)ctx;
    const SapWitHostClockCommand *clock_cmd = (const SapWitHostClockCommand *)command;
    SapWitHostClockReply *reply = (SapWitHostClockReply *)reply_out;
    uint64_t now = 0u;

    if (!mock || !clock_cmd || !reply) {
        return ERR_INVALID;
    }
    mock->last_clock_command = clock_cmd->case_tag;
    if (clock_cmd->case_tag != SAP_WIT_HOST_CLOCK_COMMAND_MONOTONIC_NOW) {
        return ERR_INVALID;
    }

    if (mock->now_count > 0u) {
        uint32_t idx = mock->next_now_index < mock->now_count
            ? mock->next_now_index
            : (mock->now_count - 1u);
        now = mock->now_ms[idx];
        if (mock->next_now_index < mock->now_count) {
            mock->next_now_index++;
        }
    }

    sap_wit_zero_host_clock_reply(reply);
    reply->case_tag = SAP_WIT_HOST_CLOCK_REPLY_NOW;
    reply->val.now.is_v_ok = 1u;
    reply->val.now.v_val.ok.v = now;
    return ERR_OK;
}

static int run_with_click(CroftJsonViewerWindowAppMock *mock,
                          uint32_t auto_close_ms,
                          CroftJsonViewerWindowAppState *state_out,
                          uint32_t *frame_count_out)
{
    CroftJsonViewerWindowAppConfig config = {0};

    config.dispatch_ctx = mock;
    config.window_dispatch = mock_window_dispatch;
    config.gpu_dispatch = mock_gpu_dispatch;
    config.clock_dispatch = mock_clock_dispatch;
    config.title_data = (const uint8_t *)"Croft JSON Viewer Test";
    config.title_len = 22u;
    return croft_json_viewer_window_app_run(state_out,
                                            &config,
                                            k_test_json,
                                            (uint32_t)(sizeof(k_test_json) - 1u),
                                            auto_close_ms,
                                            frame_count_out);
}

int test_json_viewer_window_app_content_click_selects_and_toggles(void)
{
    CroftJsonViewerWindowAppMock mock = {0};
    CroftJsonViewerWindowAppState state = {0};
    uint32_t frame_count = UINT32_MAX;
    uint32_t items_index;
    int rc;

    mock.width = 860u;
    mock.height = 560u;
    mock.events[0].case_tag = SAP_WIT_HOST_WINDOW_EVENT_CURSOR;
    mock.events[0].val.cursor.x_milli = 330000;
    mock.events[0].val.cursor.y_milli = 252000;
    mock.events[1].case_tag = SAP_WIT_HOST_WINDOW_EVENT_MOUSE;
    mock.events[1].val.mouse.button = CROFT_JSON_VIEWER_TEST_MOUSE_LEFT;
    mock.events[1].val.mouse.action = CROFT_JSON_VIEWER_TEST_MOUSE_PRESS;
    mock.event_count = 2u;
    mock.now_ms[0] = 0u;
    mock.now_ms[1] = 1u;
    mock.now_count = 2u;

    rc = run_with_click(&mock, 1u, &state, &frame_count);
    if (rc != ERR_OK) {
        fprintf(stderr,
                "    run_with_click rc=%d window_cmd=%u gpu_cmd=%u clock_cmd=%u\n",
                rc,
                mock.last_window_command,
                mock.last_gpu_command,
                mock.last_clock_command);
    }
    ASSERT_JSON_VIEWER_APP(rc == ERR_OK);
    ASSERT_JSON_VIEWER_APP(frame_count == 0u);
    ASSERT_JSON_VIEWER_APP(strcmp(croft_json_viewer_state_selected_path(&state.viewer), ".items") == 0);
    ASSERT_JSON_VIEWER_APP(croft_json_viewer_state_selected_is_expanded(&state.viewer));

    items_index = find_path_index(&state.viewer, ".items");
    ASSERT_JSON_VIEWER_APP(items_index != UINT32_MAX);
    ASSERT_JSON_VIEWER_APP(croft_json_viewer_state_path_is_expanded(&state.viewer, items_index));
    return 0;
}

int test_json_viewer_window_app_content_click_uses_live_layout(void)
{
    CroftJsonViewerWindowAppMock mock = {0};
    CroftJsonViewerWindowAppState state = {0};
    uint32_t frame_count = UINT32_MAX;
    int rc;

    mock.width = 860u;
    mock.height = 300u;
    mock.events[0].case_tag = SAP_WIT_HOST_WINDOW_EVENT_CURSOR;
    mock.events[0].val.cursor.x_milli = 330000;
    mock.events[0].val.cursor.y_milli = 252000;
    mock.events[1].case_tag = SAP_WIT_HOST_WINDOW_EVENT_MOUSE;
    mock.events[1].val.mouse.button = CROFT_JSON_VIEWER_TEST_MOUSE_LEFT;
    mock.events[1].val.mouse.action = CROFT_JSON_VIEWER_TEST_MOUSE_PRESS;
    mock.event_count = 2u;
    mock.now_ms[0] = 0u;
    mock.now_ms[1] = 1u;
    mock.now_count = 2u;

    rc = run_with_click(&mock, 1u, &state, &frame_count);
    if (rc != ERR_OK) {
        fprintf(stderr,
                "    run_with_click rc=%d window_cmd=%u gpu_cmd=%u clock_cmd=%u\n",
                rc,
                mock.last_window_command,
                mock.last_gpu_command,
                mock.last_clock_command);
    }
    ASSERT_JSON_VIEWER_APP(rc == ERR_OK);
    ASSERT_JSON_VIEWER_APP(frame_count == 0u);
    ASSERT_JSON_VIEWER_APP(strcmp(croft_json_viewer_state_selected_path(&state.viewer), ".features") == 0);
    ASSERT_JSON_VIEWER_APP(!croft_json_viewer_state_selected_is_expanded(&state.viewer));
    return 0;
}
