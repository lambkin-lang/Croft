#include "croft/host_render.h"
#include "croft/host_ui.h"

#include <tgfx/core/Canvas.h>
#include <tgfx/core/Color.h>
#include <tgfx/core/Font.h>
#include <tgfx/core/Paint.h>
#include <tgfx/core/Surface.h>
#include <tgfx/core/TextBlob.h>
#include <tgfx/gpu/Backend.h>
#include <tgfx/gpu/metal/MetalDevice.h>

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <string>

static std::shared_ptr<tgfx::MetalDevice> g_device;
static tgfx::Context* g_context = nullptr;
static std::shared_ptr<tgfx::Surface> g_surface;
static tgfx::Canvas* g_canvas = nullptr;
static __strong NSView* g_view = nil;
static __strong CAMetalLayer* g_layer = nil;
static __strong id<CAMetalDrawable> g_drawable = nil;

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
}

int32_t host_render_begin_frame(uint32_t width, uint32_t height) {
    if (!g_device || !g_layer || width == 0 || height == 0) {
        return -1;
    }

    g_context = g_device->lockContext();
    if (!g_context) {
        return -1;
    }

    update_layer_size(width, height);

    g_drawable = [g_layer nextDrawable];
    if (!g_drawable) {
        g_device->unlock();
        g_context = nullptr;
        return -1;
    }

    tgfx::MetalTextureInfo metalInfo;
    metalInfo.texture = (__bridge const void*)g_drawable.texture;
    metalInfo.format = static_cast<unsigned>(g_drawable.texture.pixelFormat);

    tgfx::BackendRenderTarget renderTarget(metalInfo,
                                           static_cast<int>(width),
                                           static_cast<int>(height));
    g_surface = tgfx::Surface::MakeFrom(g_context, renderTarget, tgfx::ImageOrigin::TopLeft, 0);
    if (!g_surface) {
        g_drawable = nil;
        g_device->unlock();
        g_context = nullptr;
        return -1;
    }

    g_canvas = g_surface->getCanvas();
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

    auto typeface = tgfx::Typeface::MakeFromName("Helvetica", "");
    if (!typeface) {
        typeface = tgfx::Typeface::MakeFromName("", "");
    }

    tgfx::Font font(typeface, font_size);
    std::string str(text, static_cast<size_t>(len));
    auto textBlob = tgfx::TextBlob::MakeFrom(str, font);
    if (textBlob) {
        g_canvas->drawTextBlob(textBlob, x, y, paint);
    }
    return 0;
}

float host_render_measure_text(const char* text, uint32_t len, float font_size) {
    if (len == 0) {
        return 0.0f;
    }

    auto typeface = tgfx::Typeface::MakeFromName("Helvetica", "");
    if (!typeface) {
        typeface = tgfx::Typeface::MakeFromName("", "");
    }

    tgfx::Font font(typeface, font_size);
    float totalAdvance = 0.0f;
    uint32_t i = 0;
    while (i < len) {
        uint8_t c = static_cast<uint8_t>(text[i]);
        uint32_t codepoint = 0;
        int bytes = 1;
        if ((c & 0x80) == 0) {
            codepoint = c;
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 < len) {
                codepoint = ((c & 0x1F) << 6) | (text[i + 1] & 0x3F);
                bytes = 2;
            } else {
                break;
            }
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 < len) {
                codepoint = ((c & 0x0F) << 12) |
                            ((text[i + 1] & 0x3F) << 6) |
                            (text[i + 2] & 0x3F);
                bytes = 3;
            } else {
                break;
            }
        } else if ((c & 0xF8) == 0xF0) {
            if (i + 3 < len) {
                codepoint = ((c & 0x07) << 18) |
                            ((text[i + 1] & 0x3F) << 12) |
                            ((text[i + 2] & 0x3F) << 6) |
                            (text[i + 3] & 0x3F);
                bytes = 4;
            } else {
                break;
            }
        }

        tgfx::GlyphID glyph_id = font.getGlyphID(codepoint);
        totalAdvance += font.getAdvance(glyph_id);
        i += bytes;
    }

    return totalAdvance;
}

int32_t host_render_end_frame(void) {
    if (!g_context) {
        return 0;
    }

    g_context->flushAndSubmit(true);
    if (g_drawable) {
        [g_drawable present];
    }

    g_canvas = nullptr;
    g_surface = nullptr;
    g_drawable = nil;
    g_device->unlock();
    g_context = nullptr;
    return 0;
}

}
