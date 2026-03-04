#include "croft/host_render.h"
#include <tgfx/gpu/opengl/GLDevice.h>
#include <tgfx/gpu/opengl/GLTypes.h>
#include <tgfx/core/Surface.h>
#include <tgfx/core/Canvas.h>
#include <tgfx/core/Paint.h>
#include <tgfx/core/Color.h>
#include <tgfx/core/Font.h>
#include <tgfx/core/TextBlob.h>
#include <string>

struct RenderState {
    std::shared_ptr<tgfx::GLDevice> device;
    tgfx::Context* context = nullptr;
    std::shared_ptr<tgfx::Surface> surface;
    tgfx::Canvas* canvas = nullptr;
} state;

extern "C" {

int32_t host_render_init(void) {
    state.device = tgfx::GLDevice::MakeWithFallback();
    if (!state.device) return -1;
    return 0;
}

void host_render_terminate(void) {
    state.surface = nullptr;
    state.canvas = nullptr;
    if (state.device && state.context) {
        state.device->unlock();
        state.context = nullptr;
    }
    state.device = nullptr;
}

int32_t host_render_begin_frame(uint32_t width, uint32_t height) {
    if (!state.device) return -1;
    
    state.context = state.device->lockContext();
    if (!state.context) return -1;
    
    tgfx::GLFrameBufferInfo glInfo;
    glInfo.id = 0;
    glInfo.format = 0x8058; // GL_RGBA8
    
    tgfx::BackendRenderTarget renderTarget(glInfo, width, height);
    state.surface = tgfx::Surface::MakeFrom(state.context, renderTarget, tgfx::ImageOrigin::BottomLeft);
    if (!state.surface) {
        state.device->unlock();
        state.context = nullptr;
        return -1;
    }
    
    state.canvas = state.surface->getCanvas();
    return 0;
}

int32_t host_render_clear(uint32_t color_rgba) {
    if (!state.canvas) return -1;
    float r = ((color_rgba >> 24) & 0xFF) / 255.0f;
    float g = ((color_rgba >> 16) & 0xFF) / 255.0f;
    float b = ((color_rgba >> 8)  & 0xFF) / 255.0f;
    float a =  (color_rgba        & 0xFF) / 255.0f;
    state.canvas->clear(tgfx::Color{r, g, b, a});
    return 0;
}

int32_t host_render_draw_rect(float x, float y, float w, float h, uint32_t color_rgba) {
    if (!state.canvas) return -1;
    tgfx::Paint paint;
    float r = ((color_rgba >> 24) & 0xFF) / 255.0f;
    float g = ((color_rgba >> 16) & 0xFF) / 255.0f;
    float b = ((color_rgba >> 8)  & 0xFF) / 255.0f;
    float a =  (color_rgba        & 0xFF) / 255.0f;
    paint.setColor(tgfx::Color{r, g, b, a});
    state.canvas->drawRect(tgfx::Rect::MakeXYWH(x, y, w, h), paint);
    return 0;
}

int32_t host_render_draw_text(float x, float y, const char* text, uint32_t len, uint32_t color_rgba) {
    if (!state.canvas) return -1;
    tgfx::Paint paint;
    float r = ((color_rgba >> 24) & 0xFF) / 255.0f;
    float g = ((color_rgba >> 16) & 0xFF) / 255.0f;
    float b = ((color_rgba >> 8)  & 0xFF) / 255.0f;
    float a =  (color_rgba        & 0xFF) / 255.0f;
    paint.setColor(tgfx::Color{r, g, b, a});
    
    tgfx::Font font;
    font.setSize(36.0f);
    std::string str(text, static_cast<size_t>(len));
    auto textBlob = tgfx::TextBlob::MakeFrom(str, font);
    if (textBlob) {
        state.canvas->drawTextBlob(textBlob, x, y, paint);
    }
    return 0;
}

int32_t host_render_end_frame(void) {
    if (state.context) {
        state.context->flushAndSubmit(true);
        state.canvas = nullptr;
        state.surface = nullptr;
        state.device->unlock();
        state.context = nullptr;
    }
    return 0;
}

}
