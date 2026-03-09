#include "croft/wit_host_gpu2d_runtime.h"

#include "croft/host_render.h"
#include "croft/host_ui.h"

#include <stdlib.h>
#include <string.h>

struct croft_wit_host_gpu2d_runtime {
    uint8_t live;
    uint8_t frame_active;
};

/*
 * The current native host only exposes one implicit render target bound to the
 * current window. We still model it as a `surface` resource because Lambkin's
 * generated side should not observe raw Cocoa/Metal pointers or singleton host
 * state directly. That mismatch is intentional research data, not an accident.
 */
static int croft_wit_host_gpu2d_valid(const croft_wit_host_gpu2d_runtime* runtime,
                                      SapWitHostGpu2dSurfaceResource handle)
{
    return runtime && runtime->live && handle == (SapWitHostGpu2dSurfaceResource)1u;
}

static void croft_wit_host_gpu2d_reply_zero(SapWitHostGpu2dReply* reply)
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

static void croft_wit_host_gpu2d_reply_surface_ok(SapWitHostGpu2dReply* reply,
                                                  SapWitHostGpu2dSurfaceResource handle)
{
    croft_wit_host_gpu2d_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_GPU2D_REPLY_SURFACE;
    reply->val.surface.is_v_ok = 1u;
    reply->val.surface.v_val.ok.v = handle;
}

static void croft_wit_host_gpu2d_reply_surface_err(SapWitHostGpu2dReply* reply, const char* err)
{
    croft_wit_host_gpu2d_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_GPU2D_REPLY_SURFACE;
    reply->val.surface.is_v_ok = 0u;
    croft_wit_set_string_view(err,
                              &reply->val.surface.v_val.err.v_data,
                              &reply->val.surface.v_val.err.v_len);
}

static void croft_wit_host_gpu2d_reply_status_ok(SapWitHostGpu2dReply* reply)
{
    croft_wit_host_gpu2d_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_GPU2D_REPLY_STATUS;
    reply->val.status.is_v_ok = 1u;
}

static void croft_wit_host_gpu2d_reply_status_err(SapWitHostGpu2dReply* reply, const char* err)
{
    croft_wit_host_gpu2d_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_GPU2D_REPLY_STATUS;
    reply->val.status.is_v_ok = 0u;
    croft_wit_set_string_view(err,
                              &reply->val.status.v_val.err.v_data,
                              &reply->val.status.v_val.err.v_len);
}

static void croft_wit_host_gpu2d_reply_capabilities_ok(SapWitHostGpu2dReply* reply,
                                                       uint32_t flags)
{
    croft_wit_host_gpu2d_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_GPU2D_REPLY_CAPABILITIES;
    reply->val.capabilities.is_v_ok = 1u;
    reply->val.capabilities.v_val.ok.v = flags;
}

static void croft_wit_host_gpu2d_reply_capabilities_err(SapWitHostGpu2dReply* reply, const char* err)
{
    croft_wit_host_gpu2d_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_GPU2D_REPLY_CAPABILITIES;
    reply->val.capabilities.is_v_ok = 0u;
    croft_wit_set_string_view(err,
                              &reply->val.capabilities.v_val.err.v_data,
                              &reply->val.capabilities.v_val.err.v_len);
}

static void croft_wit_host_gpu2d_reply_measure_ok(SapWitHostGpu2dReply* reply, float width)
{
    croft_wit_host_gpu2d_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_GPU2D_REPLY_MEASURE;
    reply->val.measure.is_v_ok = 1u;
    reply->val.measure.v_val.ok.v = width;
}

static void croft_wit_host_gpu2d_reply_measure_err(SapWitHostGpu2dReply* reply, const char* err)
{
    croft_wit_host_gpu2d_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_GPU2D_REPLY_MEASURE;
    reply->val.measure.is_v_ok = 0u;
    croft_wit_set_string_view(err,
                              &reply->val.measure.v_val.err.v_data,
                              &reply->val.measure.v_val.err.v_len);
}

static uint32_t croft_wit_host_gpu2d_capabilities(void)
{
    return SAP_WIT_HOST_GPU2D_CAPABILITIES_TEXT
        | SAP_WIT_HOST_GPU2D_CAPABILITIES_TRANSFORM
        | SAP_WIT_HOST_GPU2D_CAPABILITIES_CLIP
        | SAP_WIT_HOST_GPU2D_CAPABILITIES_PRESENT;
}

croft_wit_host_gpu2d_runtime* croft_wit_host_gpu2d_runtime_create(void)
{
    return (croft_wit_host_gpu2d_runtime*)calloc(1u, sizeof(croft_wit_host_gpu2d_runtime));
}

void croft_wit_host_gpu2d_runtime_destroy(croft_wit_host_gpu2d_runtime* runtime)
{
    if (!runtime) {
        return;
    }
    if (runtime->live) {
        host_render_terminate();
    }
    free(runtime);
}

static int32_t croft_wit_host_gpu2d_dispatch_capabilities(
    croft_wit_host_gpu2d_runtime* runtime,
    const SapWitHostGpu2dCapabilitiesQuery* request,
    SapWitHostGpu2dReply* reply_out)
{
    (void)runtime;
    (void)request;
    if (!reply_out) {
        return -1;
    }
    croft_wit_host_gpu2d_reply_capabilities_ok(reply_out, croft_wit_host_gpu2d_capabilities());
    return 0;
}

static int32_t croft_wit_host_gpu2d_dispatch_open(croft_wit_host_gpu2d_runtime* runtime,
                                                  const SapWitHostGpu2dOpen* request,
                                                  SapWitHostGpu2dReply* reply_out)
{
    (void)request;
    if (!runtime || !reply_out) {
        return -1;
    }
    if (runtime->live) {
        croft_wit_host_gpu2d_reply_surface_err(reply_out, "busy");
        return 0;
    }
    if (host_render_init() != 0) {
        croft_wit_host_gpu2d_reply_surface_err(reply_out, "unavailable");
        return 0;
    }
    runtime->live = 1u;
    runtime->frame_active = 0u;
    croft_wit_host_gpu2d_reply_surface_ok(reply_out, (SapWitHostGpu2dSurfaceResource)1u);
    return 0;
}

static int32_t croft_wit_host_gpu2d_dispatch_drop(croft_wit_host_gpu2d_runtime* runtime,
                                                  const SapWitHostGpu2dSurfaceOp* request,
                                                  SapWitHostGpu2dReply* reply_out)
{
    if (!runtime || !request || !reply_out) {
        return -1;
    }
    if (!croft_wit_host_gpu2d_valid(runtime, request->surface)) {
        croft_wit_host_gpu2d_reply_status_err(reply_out, "invalid-handle");
        return 0;
    }
    if (runtime->frame_active) {
        croft_wit_host_gpu2d_reply_status_err(reply_out, "busy");
        return 0;
    }
    host_render_terminate();
    runtime->live = 0u;
    croft_wit_host_gpu2d_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_gpu2d_dispatch_begin_frame(
    croft_wit_host_gpu2d_runtime* runtime,
    const SapWitHostGpu2dBeginFrame* request,
    SapWitHostGpu2dReply* reply_out)
{
    if (!runtime || !request || !reply_out) {
        return -1;
    }
    if (!croft_wit_host_gpu2d_valid(runtime, request->surface)) {
        croft_wit_host_gpu2d_reply_status_err(reply_out, "invalid-handle");
        return 0;
    }
    if (runtime->frame_active) {
        croft_wit_host_gpu2d_reply_status_err(reply_out, "busy");
        return 0;
    }
    if (host_render_begin_frame(request->width, request->height) != 0) {
        croft_wit_host_gpu2d_reply_status_err(reply_out, "internal");
        return 0;
    }
    runtime->frame_active = 1u;
    croft_wit_host_gpu2d_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_gpu2d_dispatch_frame_required(
    croft_wit_host_gpu2d_runtime* runtime,
    SapWitHostGpu2dSurfaceResource surface,
    SapWitHostGpu2dReply* reply_out)
{
    if (!croft_wit_host_gpu2d_valid(runtime, surface)) {
        croft_wit_host_gpu2d_reply_status_err(reply_out, "invalid-handle");
        return 0;
    }
    if (!runtime->frame_active) {
        croft_wit_host_gpu2d_reply_status_err(reply_out, "busy");
        return 0;
    }
    return 1;
}

static int32_t croft_wit_host_gpu2d_dispatch_surface_required(
    croft_wit_host_gpu2d_runtime* runtime,
    SapWitHostGpu2dSurfaceResource surface,
    SapWitHostGpu2dReply* reply_out)
{
    if (!croft_wit_host_gpu2d_valid(runtime, surface)) {
        croft_wit_host_gpu2d_reply_status_err(reply_out, "invalid-handle");
        return 0;
    }
    return 1;
}

int32_t croft_wit_host_gpu2d_runtime_dispatch(
    croft_wit_host_gpu2d_runtime* runtime,
    const SapWitHostGpu2dCommand* command,
    SapWitHostGpu2dReply* reply_out)
{
    if (!runtime || !command || !reply_out) {
        return -1;
    }

    switch (command->case_tag) {
        case SAP_WIT_HOST_GPU2D_COMMAND_CAPABILITIES:
            return croft_wit_host_gpu2d_dispatch_capabilities(runtime, &command->val.capabilities, reply_out);
        case SAP_WIT_HOST_GPU2D_COMMAND_OPEN:
            return croft_wit_host_gpu2d_dispatch_open(runtime, &command->val.open, reply_out);
        case SAP_WIT_HOST_GPU2D_COMMAND_DROP:
            return croft_wit_host_gpu2d_dispatch_drop(runtime, &command->val.drop, reply_out);
        case SAP_WIT_HOST_GPU2D_COMMAND_BEGIN_FRAME:
            return croft_wit_host_gpu2d_dispatch_begin_frame(runtime, &command->val.begin_frame, reply_out);
        case SAP_WIT_HOST_GPU2D_COMMAND_CLEAR:
            if (!croft_wit_host_gpu2d_dispatch_frame_required(runtime, command->val.clear.surface, reply_out)) {
                return 0;
            }
            if (host_render_clear(command->val.clear.color_rgba) != 0) {
                croft_wit_host_gpu2d_reply_status_err(reply_out, "internal");
                return 0;
            }
            croft_wit_host_gpu2d_reply_status_ok(reply_out);
            return 0;
        case SAP_WIT_HOST_GPU2D_COMMAND_DRAW_RECT:
            if (!croft_wit_host_gpu2d_dispatch_frame_required(runtime, command->val.draw_rect.surface, reply_out)) {
                return 0;
            }
            if (host_render_draw_rect(command->val.draw_rect.x,
                                      command->val.draw_rect.y,
                                      command->val.draw_rect.w,
                                      command->val.draw_rect.h,
                                      command->val.draw_rect.color_rgba) != 0) {
                croft_wit_host_gpu2d_reply_status_err(reply_out, "internal");
                return 0;
            }
            croft_wit_host_gpu2d_reply_status_ok(reply_out);
            return 0;
        case SAP_WIT_HOST_GPU2D_COMMAND_DRAW_TEXT:
            if (!croft_wit_host_gpu2d_dispatch_frame_required(runtime, command->val.draw_text.surface, reply_out)) {
                return 0;
            }
            if (host_render_draw_text(command->val.draw_text.x,
                                      command->val.draw_text.y,
                                      (const char*)command->val.draw_text.utf8_data,
                                      command->val.draw_text.utf8_len,
                                      command->val.draw_text.font_size,
                                      command->val.draw_text.color_rgba) != 0) {
                croft_wit_host_gpu2d_reply_status_err(reply_out, "internal");
                return 0;
            }
            croft_wit_host_gpu2d_reply_status_ok(reply_out);
            return 0;
        case SAP_WIT_HOST_GPU2D_COMMAND_MEASURE_TEXT:
            if (!croft_wit_host_gpu2d_dispatch_surface_required(runtime, command->val.measure_text.surface, reply_out)) {
                croft_wit_host_gpu2d_reply_measure_err(reply_out, "invalid-handle");
                return 0;
            }
            croft_wit_host_gpu2d_reply_measure_ok(
                reply_out,
                host_render_measure_text((const char*)command->val.measure_text.utf8_data,
                                         command->val.measure_text.utf8_len,
                                         command->val.measure_text.font_size));
            return 0;
        case SAP_WIT_HOST_GPU2D_COMMAND_SAVE:
            if (!croft_wit_host_gpu2d_dispatch_frame_required(runtime, command->val.save.surface, reply_out)) {
                return 0;
            }
            host_render_save();
            croft_wit_host_gpu2d_reply_status_ok(reply_out);
            return 0;
        case SAP_WIT_HOST_GPU2D_COMMAND_RESTORE:
            if (!croft_wit_host_gpu2d_dispatch_frame_required(runtime, command->val.restore.surface, reply_out)) {
                return 0;
            }
            host_render_restore();
            croft_wit_host_gpu2d_reply_status_ok(reply_out);
            return 0;
        case SAP_WIT_HOST_GPU2D_COMMAND_TRANSLATE:
            if (!croft_wit_host_gpu2d_dispatch_frame_required(runtime, command->val.translate.surface, reply_out)) {
                return 0;
            }
            host_render_translate(command->val.translate.dx, command->val.translate.dy);
            croft_wit_host_gpu2d_reply_status_ok(reply_out);
            return 0;
        case SAP_WIT_HOST_GPU2D_COMMAND_SCALE:
            if (!croft_wit_host_gpu2d_dispatch_frame_required(runtime, command->val.scale.surface, reply_out)) {
                return 0;
            }
            host_render_scale(command->val.scale.sx, command->val.scale.sy);
            croft_wit_host_gpu2d_reply_status_ok(reply_out);
            return 0;
        case SAP_WIT_HOST_GPU2D_COMMAND_CLIP_RECT:
            if (!croft_wit_host_gpu2d_dispatch_frame_required(runtime, command->val.clip_rect.surface, reply_out)) {
                return 0;
            }
            host_render_clip_rect(command->val.clip_rect.x,
                                  command->val.clip_rect.y,
                                  command->val.clip_rect.w,
                                  command->val.clip_rect.h);
            croft_wit_host_gpu2d_reply_status_ok(reply_out);
            return 0;
        case SAP_WIT_HOST_GPU2D_COMMAND_END_FRAME:
            if (!croft_wit_host_gpu2d_dispatch_frame_required(runtime, command->val.end_frame.surface, reply_out)) {
                return 0;
            }
            if (host_render_end_frame() != 0) {
                croft_wit_host_gpu2d_reply_status_err(reply_out, "internal");
                return 0;
            }
            host_ui_swap_buffers();
            runtime->frame_active = 0u;
            croft_wit_host_gpu2d_reply_status_ok(reply_out);
            return 0;
        default:
            croft_wit_host_gpu2d_reply_status_err(reply_out, "internal");
            return 0;
    }
}
