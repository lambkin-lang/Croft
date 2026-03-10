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
    sap_wit_zero_host_gpu2d_reply(reply);
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
    void* ctx,
    SapWitHostGpu2dReply* reply_out)
{
    croft_wit_host_gpu2d_runtime* runtime = (croft_wit_host_gpu2d_runtime*)ctx;
    (void)runtime;
    if (!reply_out) {
        return -1;
    }
    croft_wit_host_gpu2d_reply_capabilities_ok(reply_out, croft_wit_host_gpu2d_capabilities());
    return 0;
}

static int32_t croft_wit_host_gpu2d_dispatch_open(void* ctx,
                                                  SapWitHostGpu2dReply* reply_out)
{
    croft_wit_host_gpu2d_runtime* runtime = (croft_wit_host_gpu2d_runtime*)ctx;
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

static int32_t croft_wit_host_gpu2d_dispatch_drop(void* ctx,
                                                  const SapWitHostGpu2dSurfaceDrop* request,
                                                  SapWitHostGpu2dReply* reply_out)
{
    croft_wit_host_gpu2d_runtime* runtime = (croft_wit_host_gpu2d_runtime*)ctx;
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
    void* ctx,
    const SapWitHostGpu2dSurfaceBeginFrame* request,
    SapWitHostGpu2dReply* reply_out)
{
    croft_wit_host_gpu2d_runtime* runtime = (croft_wit_host_gpu2d_runtime*)ctx;
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

static int32_t croft_wit_host_gpu2d_dispatch_clear(void* ctx,
                                                   const SapWitHostGpu2dSurfaceClear* request,
                                                   SapWitHostGpu2dReply* reply_out)
{
    croft_wit_host_gpu2d_runtime* runtime = (croft_wit_host_gpu2d_runtime*)ctx;

    if (!runtime || !request || !reply_out) {
        return -1;
    }
    if (!croft_wit_host_gpu2d_dispatch_frame_required(runtime, request->surface, reply_out)) {
        return 0;
    }
    if (host_render_clear(request->color_rgba) != 0) {
        croft_wit_host_gpu2d_reply_status_err(reply_out, "internal");
        return 0;
    }
    croft_wit_host_gpu2d_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_gpu2d_dispatch_draw_rect(void* ctx,
                                                       const SapWitHostGpu2dSurfaceDrawRect* request,
                                                       SapWitHostGpu2dReply* reply_out)
{
    croft_wit_host_gpu2d_runtime* runtime = (croft_wit_host_gpu2d_runtime*)ctx;

    if (!runtime || !request || !reply_out) {
        return -1;
    }
    if (!croft_wit_host_gpu2d_dispatch_frame_required(runtime, request->surface, reply_out)) {
        return 0;
    }
    if (host_render_draw_rect(request->x,
                              request->y,
                              request->w,
                              request->h,
                              request->color_rgba) != 0) {
        croft_wit_host_gpu2d_reply_status_err(reply_out, "internal");
        return 0;
    }
    croft_wit_host_gpu2d_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_gpu2d_dispatch_draw_text(void* ctx,
                                                       const SapWitHostGpu2dSurfaceDrawText* request,
                                                       SapWitHostGpu2dReply* reply_out)
{
    croft_wit_host_gpu2d_runtime* runtime = (croft_wit_host_gpu2d_runtime*)ctx;

    if (!runtime || !request || !reply_out) {
        return -1;
    }
    if (!croft_wit_host_gpu2d_dispatch_frame_required(runtime, request->surface, reply_out)) {
        return 0;
    }
    if (host_render_draw_text(request->x,
                              request->y,
                              (const char*)request->utf8_data,
                              request->utf8_len,
                              request->font_size,
                              request->color_rgba) != 0) {
        croft_wit_host_gpu2d_reply_status_err(reply_out, "internal");
        return 0;
    }
    croft_wit_host_gpu2d_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_gpu2d_dispatch_measure_text(
    void* ctx,
    const SapWitHostGpu2dSurfaceMeasureText* request,
    SapWitHostGpu2dReply* reply_out)
{
    croft_wit_host_gpu2d_runtime* runtime = (croft_wit_host_gpu2d_runtime*)ctx;

    if (!runtime || !request || !reply_out) {
        return -1;
    }
    if (!croft_wit_host_gpu2d_dispatch_surface_required(runtime, request->surface, reply_out)) {
        croft_wit_host_gpu2d_reply_measure_err(reply_out, "invalid-handle");
        return 0;
    }
    croft_wit_host_gpu2d_reply_measure_ok(
        reply_out,
        host_render_measure_text((const char*)request->utf8_data,
                                 request->utf8_len,
                                 request->font_size));
    return 0;
}

static int32_t croft_wit_host_gpu2d_dispatch_save(void* ctx,
                                                  const SapWitHostGpu2dSurfaceSave* request,
                                                  SapWitHostGpu2dReply* reply_out)
{
    croft_wit_host_gpu2d_runtime* runtime = (croft_wit_host_gpu2d_runtime*)ctx;

    if (!runtime || !request || !reply_out) {
        return -1;
    }
    if (!croft_wit_host_gpu2d_dispatch_frame_required(runtime, request->surface, reply_out)) {
        return 0;
    }
    host_render_save();
    croft_wit_host_gpu2d_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_gpu2d_dispatch_restore(void* ctx,
                                                     const SapWitHostGpu2dSurfaceRestore* request,
                                                     SapWitHostGpu2dReply* reply_out)
{
    croft_wit_host_gpu2d_runtime* runtime = (croft_wit_host_gpu2d_runtime*)ctx;

    if (!runtime || !request || !reply_out) {
        return -1;
    }
    if (!croft_wit_host_gpu2d_dispatch_frame_required(runtime, request->surface, reply_out)) {
        return 0;
    }
    host_render_restore();
    croft_wit_host_gpu2d_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_gpu2d_dispatch_translate(void* ctx,
                                                       const SapWitHostGpu2dSurfaceTranslate* request,
                                                       SapWitHostGpu2dReply* reply_out)
{
    croft_wit_host_gpu2d_runtime* runtime = (croft_wit_host_gpu2d_runtime*)ctx;

    if (!runtime || !request || !reply_out) {
        return -1;
    }
    if (!croft_wit_host_gpu2d_dispatch_frame_required(runtime, request->surface, reply_out)) {
        return 0;
    }
    host_render_translate(request->dx, request->dy);
    croft_wit_host_gpu2d_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_gpu2d_dispatch_scale(void* ctx,
                                                   const SapWitHostGpu2dSurfaceScale* request,
                                                   SapWitHostGpu2dReply* reply_out)
{
    croft_wit_host_gpu2d_runtime* runtime = (croft_wit_host_gpu2d_runtime*)ctx;

    if (!runtime || !request || !reply_out) {
        return -1;
    }
    if (!croft_wit_host_gpu2d_dispatch_frame_required(runtime, request->surface, reply_out)) {
        return 0;
    }
    host_render_scale(request->sx, request->sy);
    croft_wit_host_gpu2d_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_gpu2d_dispatch_clip_rect(void* ctx,
                                                       const SapWitHostGpu2dSurfaceClipRect* request,
                                                       SapWitHostGpu2dReply* reply_out)
{
    croft_wit_host_gpu2d_runtime* runtime = (croft_wit_host_gpu2d_runtime*)ctx;

    if (!runtime || !request || !reply_out) {
        return -1;
    }
    if (!croft_wit_host_gpu2d_dispatch_frame_required(runtime, request->surface, reply_out)) {
        return 0;
    }
    host_render_clip_rect(request->x, request->y, request->w, request->h);
    croft_wit_host_gpu2d_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_gpu2d_dispatch_end_frame(void* ctx,
                                                       const SapWitHostGpu2dSurfaceEndFrame* request,
                                                       SapWitHostGpu2dReply* reply_out)
{
    croft_wit_host_gpu2d_runtime* runtime = (croft_wit_host_gpu2d_runtime*)ctx;

    if (!runtime || !request || !reply_out) {
        return -1;
    }
    if (!croft_wit_host_gpu2d_dispatch_frame_required(runtime, request->surface, reply_out)) {
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
}

static const SapWitHostGpu2dDispatchOps g_croft_wit_host_gpu2d_dispatch_ops = {
    .capabilities = croft_wit_host_gpu2d_dispatch_capabilities,
    .open = croft_wit_host_gpu2d_dispatch_open,
    .drop = croft_wit_host_gpu2d_dispatch_drop,
    .begin_frame = croft_wit_host_gpu2d_dispatch_begin_frame,
    .clear = croft_wit_host_gpu2d_dispatch_clear,
    .draw_rect = croft_wit_host_gpu2d_dispatch_draw_rect,
    .draw_text = croft_wit_host_gpu2d_dispatch_draw_text,
    .measure_text = croft_wit_host_gpu2d_dispatch_measure_text,
    .save = croft_wit_host_gpu2d_dispatch_save,
    .restore = croft_wit_host_gpu2d_dispatch_restore,
    .translate = croft_wit_host_gpu2d_dispatch_translate,
    .scale = croft_wit_host_gpu2d_dispatch_scale,
    .clip_rect = croft_wit_host_gpu2d_dispatch_clip_rect,
    .end_frame = croft_wit_host_gpu2d_dispatch_end_frame,
};

int32_t croft_wit_host_gpu2d_runtime_dispatch(
    croft_wit_host_gpu2d_runtime* runtime,
    const SapWitHostGpu2dCommand* command,
    SapWitHostGpu2dReply* reply_out)
{
    int32_t rc;

    if (!runtime || !command || !reply_out) {
        return -1;
    }

    rc = sap_wit_dispatch_host_gpu2d(runtime, &g_croft_wit_host_gpu2d_dispatch_ops, command, reply_out);
    if (rc == -1) {
        croft_wit_host_gpu2d_reply_status_err(reply_out, "internal");
        return 0;
    }
    return rc;
}
