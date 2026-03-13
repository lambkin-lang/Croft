#include "croft/host_render.h"
#include "host_render_tgfx_text_cache.h"
#include <tgfx/gpu/opengl/GLDevice.h>
#include <tgfx/gpu/Context.h>
#include <tgfx/gpu/GPU.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <tgfx/gpu/opengl/GLTypes.h>
#include <tgfx/core/Surface.h>
#include <tgfx/core/Canvas.h>
#include <tgfx/core/Paint.h>
#include <tgfx/core/Color.h>
#include <tgfx/core/Font.h>
#include <tgfx/core/TextBlob.h>
#include <chrono>
#include <cstring>
#include <string>
#include <OpenGL/gl3.h>

struct RenderState {
    std::shared_ptr<tgfx::GLDevice> device;
    tgfx::Context* context = nullptr;
    std::shared_ptr<tgfx::Surface> surface;
    tgfx::Canvas* canvas = nullptr;
    GLuint quadVao = 0;
    GLuint quadProgram = 0;
} state;
static uint32_t g_profile_enabled = 0u;
static croft_host_render_profile_snapshot g_profile_stats = {};
static croft_tgfx_text_cache::Cache g_text_cache = {};

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

static const char* quadVert = 
    "#version 330 core\n"
    "in vec2 pos;\n"
    "out vec2 uv;\n"
    "void main() {\n"
    "    gl_Position = vec4(pos, 0.0, 1.0);\n"
    "    uv = vec2(pos.x * 0.5 + 0.5, 0.5 - pos.y * 0.5);\n"
    "}\n";

static const char* quadFrag =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "uniform sampler2D tex;\n"
    "void main() {\n"
    "    color = texture(tex, uv);\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint success;
    glGetShaderiv(s, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(s, 512, NULL, log);
        printf("Shader compilation error: %s\n", log);
    }
    return s;
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
    // Compile quad shader for final blit to FBO 0
    GLuint vs = compile_shader(GL_VERTEX_SHADER, quadVert);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, quadFrag);
    state.quadProgram = glCreateProgram();
    glAttachShader(state.quadProgram, vs);
    glAttachShader(state.quadProgram, fs);
    glLinkProgram(state.quadProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);
    
    // Core profile requires a VAO to be bound even if we generate vertices in the shader
    // Moreover, Apple macOS drivers throw GL_INVALID_FRAMEBUFFER_OPERATION (0x506) if you draw without a VBO bound!
    glGenVertexArrays(1, &state.quadVao);
    glBindVertexArray(state.quadVao);
    
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    float quadVertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    
    GLint posAttrib = glGetAttribLocation(state.quadProgram, "pos");
    glEnableVertexAttribArray(posAttrib);
    glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 0, 0);
    
    glBindVertexArray(0);

    // Device is now lazily initialized in begin_frame to guarantee context readiness
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
    croft_tgfx_text_cache::reset(&g_text_cache);
}

int32_t host_render_begin_frame(uint32_t width, uint32_t height) {
    uint64_t begin_start_usec = g_profile_enabled ? profile_now_usec() : 0u;

    if (!state.device) {
        state.device = tgfx::GLDevice::Current();
        if (!state.device) {
            printf("host_render_begin_frame: Failed to create device from GLFW context.\n");
            return -1;
        }
    }
    
    {
        uint64_t phase_start_usec = g_profile_enabled ? profile_now_usec() : 0u;
        state.context = state.device->lockContext();
        profile_note(&g_profile_stats.context_lock_total_usec, phase_start_usec);
    }
    if (!state.context) return -1;
    
    // Explicitly set the OpenGL viewport before handing control to tgfx
    {
        uint64_t phase_start_usec = g_profile_enabled ? profile_now_usec() : 0u;
        glViewport(0, 0, width, height);
        profile_note(&g_profile_stats.target_update_total_usec, phase_start_usec);
    }

    GLint drawFboId = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &drawFboId);
    
    static bool printed = false;
    if (!printed) {
        printf("host_render_begin_frame: Active FBO is %d, Dimensions: %dx%d\n", drawFboId, width, height);
        printed = true;
    }

    // Create an offscreen surface. Since tgfx spawns its own shared OpenGL context, 
    // we cannot render directly to FBO 0.
    {
        uint64_t phase_start_usec = g_profile_enabled ? profile_now_usec() : 0u;
        state.surface = tgfx::Surface::Make(state.context, width, height);
        profile_note(&g_profile_stats.surface_create_total_usec, phase_start_usec);
    }
    if (!state.surface) {
        state.device->unlock();
        state.context = nullptr;
        return -1;
    }
    
    state.canvas = state.surface->getCanvas();
    if (begin_start_usec != 0u) {
        g_profile_stats.begin_frame_calls++;
        profile_note(&g_profile_stats.begin_frame_total_usec, begin_start_usec);
    }
    return 0;
}

void host_render_save(void) {
    if (state.canvas) {
        state.canvas->save();
    }
}

void host_render_restore(void) {
    if (state.canvas) {
        state.canvas->restore();
    }
}

void host_render_translate(float dx, float dy) {
    if (state.canvas) {
        state.canvas->translate(dx, dy);
    }
}

void host_render_scale(float sx, float sy) {
    if (state.canvas) {
        state.canvas->scale(sx, sy);
    }
}

void host_render_clip_rect(float x, float y, float w, float h) {
    if (state.canvas) {
        state.canvas->clipRect(tgfx::Rect::MakeXYWH(x, y, w, h));
    }
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

int32_t host_render_draw_text_with_role(float x,
                                        float y,
                                        const char* text,
                                        uint32_t len,
                                        float font_size,
                                        uint8_t font_role,
                                        uint32_t color_rgba) {
    if (!state.canvas) return -1;
    tgfx::Paint paint;
    float r = ((color_rgba >> 24) & 0xFF) / 255.0f;
    float g = ((color_rgba >> 16) & 0xFF) / 255.0f;
    float b = ((color_rgba >> 8)  & 0xFF) / 255.0f;
    float a =  (color_rgba        & 0xFF) / 255.0f;
    paint.setColor(tgfx::Color{r, g, b, a});
    
    auto textBlob =
        croft_tgfx_text_cache::get_text_blob(&g_text_cache, text, len, font_size, font_role);
    if (textBlob) {
        state.canvas->drawTextBlob(textBlob, x, y, paint);
    }
    return 0;
}

int32_t host_render_draw_text(float x,
                              float y,
                              const char* text,
                              uint32_t len,
                              float font_size,
                              uint32_t color_rgba) {
    return host_render_draw_text_with_role(x,
                                           y,
                                           text,
                                           len,
                                           font_size,
                                           CROFT_TEXT_FONT_ROLE_MONOSPACE,
                                           color_rgba);
}

float host_render_measure_text_with_role(const char* text,
                                         uint32_t len,
                                         float font_size,
                                         uint8_t font_role) {
    return croft_tgfx_text_cache::measure_text(&g_text_cache, text, len, font_size, font_role);
}

float host_render_measure_text(const char* text, uint32_t len, float font_size) {
    return host_render_measure_text_with_role(text,
                                              len,
                                              font_size,
                                              CROFT_TEXT_FONT_ROLE_MONOSPACE);
}

int32_t host_render_probe_font(float font_size,
                               const char* sample,
                               uint32_t len,
                               croft_editor_font_probe* out_probe) {
    return croft_tgfx_text_cache::probe_font(&g_text_cache, font_size, sample, len, out_probe);
}

int32_t host_render_end_frame(void) {
    uint64_t end_start_usec = g_profile_enabled ? profile_now_usec() : 0u;

    if (state.context) {
        std::unique_ptr<tgfx::Recording> recording;

        {
            uint64_t phase_start_usec = g_profile_enabled ? profile_now_usec() : 0u;
            recording = state.context->flush();
            profile_note(&g_profile_stats.flush_total_usec, phase_start_usec);
            if (!recording) {
                printf("host_render_end_frame: flushAndSubmit returned false.\n");
            }
        }
        if (recording) {
            uint64_t phase_start_usec = g_profile_enabled ? profile_now_usec() : 0u;
            state.context->submit(std::move(recording), false);
            profile_note(&g_profile_stats.submit_total_usec, phase_start_usec);
        }
        {
            uint64_t phase_start_usec = g_profile_enabled ? profile_now_usec() : 0u;
            state.context->gpu()->queue()->waitUntilCompleted();
            profile_note(&g_profile_stats.wait_total_usec, phase_start_usec);
        }
        
        GLenum tgfxErr = glGetError();
        if (tgfxErr != GL_NO_ERROR) {
            printf("host_render_end_frame: error INSIDE tgfx context: 0x%X\n", tgfxErr);
        }
        
        // Extact texture before unlocking Device/Context
        tgfx::BackendTexture backendTex = state.surface->getBackendTexture();
        tgfx::GLTextureInfo texInfo;
        bool hasTex = backendTex.getGLTextureInfo(&texInfo);
        int width = state.surface->width();
        int height = state.surface->height();
        
        // UNLOCK BEFORE BLITTING! This restores the thread to the GLFW context.
        {
            uint64_t phase_start_usec = g_profile_enabled ? profile_now_usec() : 0u;
            state.device->unlock();
            profile_note(&g_profile_stats.unlock_total_usec, phase_start_usec);
        }
        
        // Force the GLFW context current explicitly, because tgfx's unlock may not be reliable
        // across differing OS context management paradigms like CGL vs NSOpenGL.
        // We MUST set it to NULL first because GLFW maintains a TLS cache of the current context
        // and will NO-OP if it thinks the window is already current!
        extern void* host_ui_get_window(void);
        {
            uint64_t phase_start_usec = g_profile_enabled ? profile_now_usec() : 0u;
            glfwMakeContextCurrent(NULL);
            glfwMakeContextCurrent((GLFWwindow*)host_ui_get_window());
            profile_note(&g_profile_stats.present_total_usec, phase_start_usec);
        }

        if (hasTex) {
            uint64_t phase_start_usec = g_profile_enabled ? profile_now_usec() : 0u;
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            
            // Draw via textured quad mapping our offscreen render texture to FBO 0
            glUseProgram(state.quadProgram);
            
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, (GLuint)texInfo.id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glUniform1i(glGetUniformLocation(state.quadProgram, "tex"), 0);

            // Core profile requires a VAO for drawing
            glBindVertexArray(state.quadVao);

            glDisable(GL_DEPTH_TEST);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);

            // macOS requires explicitly setting the viewport 
            glViewport(0, 0, width, height);

            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);
            profile_note(&g_profile_stats.blit_total_usec, phase_start_usec);
            
        } else {
            // printf("host_render_end_frame: Failed to retrieve GL texture from surface.\n");
        }

        state.canvas = nullptr;
        state.surface = nullptr;
        state.context = nullptr;
    }
    if (end_start_usec != 0u) {
        g_profile_stats.end_frame_calls++;
        profile_note(&g_profile_stats.end_frame_total_usec, end_start_usec);
    }
    return 0;
}

}
