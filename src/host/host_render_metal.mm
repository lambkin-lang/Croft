#include "croft/host_render.h"
#include "croft/host_ui.h"
#include "host_render_tgfx_text_cache.h"

#include <tgfx/core/Canvas.h>
#include <tgfx/core/Color.h>
#include <tgfx/core/Font.h>
#include <tgfx/core/Paint.h>
#include <tgfx/core/Surface.h>
#include <tgfx/core/TextBlob.h>
#include <tgfx/gpu/Backend.h>
#include <tgfx/gpu/Context.h>
#include <tgfx/gpu/GPU.h>
#include <tgfx/gpu/metal/MetalDevice.h>

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <chrono>
#include <cstring>

static std::shared_ptr<tgfx::MetalDevice> g_device;
static tgfx::Context* g_context = nullptr;
static std::shared_ptr<tgfx::Surface> g_surface;
static tgfx::Canvas* g_canvas = nullptr;
static __strong NSView* g_view = nil;
static __strong CAMetalLayer* g_layer = nil;
static __strong id<CAMetalDrawable> g_drawable = nil;
static croft_tgfx_text_cache::Cache g_text_cache = {};
static uint32_t g_profile_enabled = 0u;
static croft_host_render_profile_snapshot g_profile_stats = {};

static uint64_t profile_now_usec() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

static void profile_note(uint64_t* total_usec, uint64_t start_usec) {
    uint64_t end_usec;

    if (!total_usec || start_usec == 0u) {
        return;
    }
    end_usec = profile_now_usec();
    if (end_usec >= start_usec) {
        *total_usec += end_usec - start_usec;
    }
}

static void update_layer_size(uint32_t width, uint32_t height) {
    if (!g_layer || !g_view) {
        return;
    }

    g_layer.frame = g_view.bounds;
    if (g_view.window) {
        g_layer.contentsScale = g_view.window.backingScaleFactor;
    }
    g_layer.drawableSize = CGSizeMake(static_cast<CGFloat>(width), static_cast<CGFloat>(height));
}

extern "C" {

void host_render_set_profiling(int enabled) {
    g_profile_enabled = enabled ? 1u : 0u;
    host_render_reset_profile();
}

void host_render_reset_profile(void) {
    uint32_t enabled = g_profile_enabled;

    std::memset(&g_profile_stats, 0, sizeof(g_profile_stats));
    g_profile_stats.enabled = enabled;
}

void host_render_get_profile(croft_host_render_profile_snapshot* out_snapshot) {
    if (!out_snapshot) {
        return;
    }

    std::memset(out_snapshot, 0, sizeof(*out_snapshot));
    *out_snapshot = g_profile_stats;
    out_snapshot->enabled = g_profile_enabled;
}

int32_t host_render_init(void) {
    NSWindow* window = (__bridge NSWindow*)host_ui_get_native_window();
    if (!window) {
        printf("host_render_init: missing Cocoa window.\n");
        return -1;
    }

    g_view = window.contentView;
    if (!g_view) {
        printf("host_render_init: missing Cocoa content view.\n");
        return -1;
    }

    g_device = tgfx::MetalDevice::Make();
    if (!g_device) {
        printf("host_render_init: failed to create tgfx Metal device.\n");
        return -1;
    }

    id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)g_device->metalDevice();
    if (!metalDevice) {
        printf("host_render_init: tgfx Metal device did not expose MTLDevice.\n");
        g_device = nullptr;
        return -1;
    }

    g_layer = [CAMetalLayer layer];
    g_layer.device = metalDevice;
    g_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    g_layer.framebufferOnly = YES;
    g_layer.contentsScale = window.backingScaleFactor;
    g_layer.frame = g_view.bounds;
    g_layer.drawableSize = CGSizeMake(g_view.bounds.size.width * g_layer.contentsScale,
                                      g_view.bounds.size.height * g_layer.contentsScale);

    g_view.wantsLayer = YES;
    g_view.layer = g_layer;
    return 0;
}

void host_render_terminate(void) {
    g_surface = nullptr;
    g_canvas = nullptr;
    g_drawable = nil;

    if (g_device && g_context) {
        g_device->unlock();
        g_context = nullptr;
    }

    if (g_view && g_view.layer == g_layer) {
        g_view.layer = nil;
    }

    g_layer = nil;
    g_view = nil;
    g_device = nullptr;
    croft_tgfx_text_cache::reset(&g_text_cache);
}

int32_t host_render_begin_frame(uint32_t width, uint32_t height) {
    uint64_t begin_start_usec = g_profile_enabled ? profile_now_usec() : 0u;

    if (!g_device || !g_layer || width == 0 || height == 0) {
        return -1;
    }

    {
        uint64_t phase_start_usec = g_profile_enabled ? profile_now_usec() : 0u;
        g_context = g_device->lockContext();
        profile_note(&g_profile_stats.context_lock_total_usec, phase_start_usec);
    }
    if (!g_context) {
        return -1;
    }

    {
        uint64_t phase_start_usec = g_profile_enabled ? profile_now_usec() : 0u;
        update_layer_size(width, height);
        profile_note(&g_profile_stats.target_update_total_usec, phase_start_usec);
    }

    {
        uint64_t phase_start_usec = g_profile_enabled ? profile_now_usec() : 0u;
        g_drawable = [g_layer nextDrawable];
        profile_note(&g_profile_stats.acquire_drawable_total_usec, phase_start_usec);
    }
    if (!g_drawable) {
        g_device->unlock();
        g_context = nullptr;
        return -1;
    }

    tgfx::MetalTextureInfo metalInfo;
    metalInfo.texture = (__bridge const void*)g_drawable.texture;
    metalInfo.format = static_cast<unsigned>(g_drawable.texture.pixelFormat);

    {
        uint64_t phase_start_usec = g_profile_enabled ? profile_now_usec() : 0u;
        tgfx::BackendRenderTarget renderTarget(metalInfo,
                                               static_cast<int>(width),
                                               static_cast<int>(height));
        g_surface = tgfx::Surface::MakeFrom(g_context, renderTarget, tgfx::ImageOrigin::TopLeft, 0);
        profile_note(&g_profile_stats.surface_create_total_usec, phase_start_usec);
    }
    if (!g_surface) {
        g_drawable = nil;
        g_device->unlock();
        g_context = nullptr;
        return -1;
    }

    g_canvas = g_surface->getCanvas();
    if (begin_start_usec != 0u) {
        g_profile_stats.begin_frame_calls++;
        profile_note(&g_profile_stats.begin_frame_total_usec, begin_start_usec);
    }
    return 0;
}

void host_render_save(void) {
    if (g_canvas) {
        g_canvas->save();
    }
}

void host_render_restore(void) {
    if (g_canvas) {
        g_canvas->restore();
    }
}

void host_render_translate(float dx, float dy) {
    if (g_canvas) {
        g_canvas->translate(dx, dy);
    }
}

void host_render_scale(float sx, float sy) {
    if (g_canvas) {
        g_canvas->scale(sx, sy);
    }
}

void host_render_clip_rect(float x, float y, float w, float h) {
    if (g_canvas) {
        g_canvas->clipRect(tgfx::Rect::MakeXYWH(x, y, w, h));
    }
}

int32_t host_render_clear(uint32_t color_rgba) {
    if (!g_canvas) {
        return -1;
    }
    float r = ((color_rgba >> 24) & 0xFF) / 255.0f;
    float g = ((color_rgba >> 16) & 0xFF) / 255.0f;
    float b = ((color_rgba >> 8) & 0xFF) / 255.0f;
    float a = (color_rgba & 0xFF) / 255.0f;
    g_canvas->clear(tgfx::Color{r, g, b, a});
    return 0;
}

int32_t host_render_draw_rect(float x, float y, float w, float h, uint32_t color_rgba) {
    if (!g_canvas) {
        return -1;
    }

    tgfx::Paint paint;
    float r = ((color_rgba >> 24) & 0xFF) / 255.0f;
    float g = ((color_rgba >> 16) & 0xFF) / 255.0f;
    float b = ((color_rgba >> 8) & 0xFF) / 255.0f;
    float a = (color_rgba & 0xFF) / 255.0f;
    paint.setColor(tgfx::Color{r, g, b, a});
    g_canvas->drawRect(tgfx::Rect::MakeXYWH(x, y, w, h), paint);
    return 0;
}

int32_t host_render_draw_text(float x,
                              float y,
                              const char* text,
                              uint32_t len,
                              float font_size,
                              uint32_t color_rgba) {
    if (!g_canvas) {
        return -1;
    }

    tgfx::Paint paint;
    float r = ((color_rgba >> 24) & 0xFF) / 255.0f;
    float g = ((color_rgba >> 16) & 0xFF) / 255.0f;
    float b = ((color_rgba >> 8) & 0xFF) / 255.0f;
    float a = (color_rgba & 0xFF) / 255.0f;
    paint.setColor(tgfx::Color{r, g, b, a});

    auto textBlob = croft_tgfx_text_cache::get_text_blob(&g_text_cache, text, len, font_size);
    if (textBlob) {
        g_canvas->drawTextBlob(textBlob, x, y, paint);
    }
    return 0;
}

float host_render_measure_text(const char* text, uint32_t len, float font_size) {
    return croft_tgfx_text_cache::measure_text(&g_text_cache, text, len, font_size);
}

int32_t host_render_probe_font(float font_size,
                               const char* sample,
                               uint32_t len,
                               croft_editor_font_probe* out_probe) {
    return croft_tgfx_text_cache::probe_font(&g_text_cache, font_size, sample, len, out_probe);
}

int32_t host_render_end_frame(void) {
    uint64_t end_start_usec = g_profile_enabled ? profile_now_usec() : 0u;

    if (!g_context) {
        return 0;
    }

    {
        std::unique_ptr<tgfx::Recording> recording;

        {
            uint64_t phase_start_usec = g_profile_enabled ? profile_now_usec() : 0u;
            recording = g_context->flush();
            profile_note(&g_profile_stats.flush_total_usec, phase_start_usec);
        }
        if (recording) {
            uint64_t phase_start_usec = g_profile_enabled ? profile_now_usec() : 0u;
            g_context->submit(std::move(recording), false);
            profile_note(&g_profile_stats.submit_total_usec, phase_start_usec);
        }
        {
            uint64_t phase_start_usec = g_profile_enabled ? profile_now_usec() : 0u;
            g_context->gpu()->queue()->waitUntilCompleted();
            profile_note(&g_profile_stats.wait_total_usec, phase_start_usec);
        }
    }
    if (g_drawable) {
        uint64_t phase_start_usec = g_profile_enabled ? profile_now_usec() : 0u;
        [g_drawable present];
        profile_note(&g_profile_stats.present_total_usec, phase_start_usec);
    }

    g_canvas = nullptr;
    g_surface = nullptr;
    g_drawable = nil;
    {
        uint64_t phase_start_usec = g_profile_enabled ? profile_now_usec() : 0u;
        g_device->unlock();
        profile_note(&g_profile_stats.unlock_total_usec, phase_start_usec);
    }
    g_context = nullptr;
    if (end_start_usec != 0u) {
        g_profile_stats.end_frame_calls++;
        profile_note(&g_profile_stats.end_frame_total_usec, end_start_usec);
    }
    return 0;
}

}
