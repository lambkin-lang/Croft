#include "croft/host_render.h"
#include <tgfx/gpu/opengl/GLDevice.h>
#include <GLFW/glfw3.h>
#include <tgfx/gpu/opengl/GLTypes.h>
#include <tgfx/core/Surface.h>
#include <tgfx/core/Canvas.h>
#include <tgfx/core/Paint.h>
#include <tgfx/core/Color.h>
#include <tgfx/core/Font.h>
#include <tgfx/core/TextBlob.h>
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
}

int32_t host_render_begin_frame(uint32_t width, uint32_t height) {
    if (!state.device) {
        state.device = tgfx::GLDevice::Current();
        if (!state.device) {
            printf("host_render_begin_frame: Failed to create device from GLFW context.\n");
            return -1;
        }
    }
    
    state.context = state.device->lockContext();
    if (!state.context) return -1;
    
    // Explicitly set the OpenGL viewport before handing control to tgfx
    glViewport(0, 0, width, height);

    GLint drawFboId = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &drawFboId);
    
    static bool printed = false;
    if (!printed) {
        printf("host_render_begin_frame: Active FBO is %d, Dimensions: %dx%d\n", drawFboId, width, height);
        printed = true;
    }

    // Create an offscreen surface. Since tgfx spawns its own shared OpenGL context, 
    // we cannot render directly to FBO 0.
    state.surface = tgfx::Surface::Make(state.context, width, height);
    if (!state.surface) {
        state.device->unlock();
        state.context = nullptr;
        return -1;
    }
    
    state.canvas = state.surface->getCanvas();
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
    
    auto typeface = tgfx::Typeface::MakeFromName("Helvetica", "");
    if (!typeface) {
        typeface = tgfx::Typeface::MakeFromName("", "");
    }
    
    tgfx::Font font(typeface, 36.0f);
    std::string str(text, static_cast<size_t>(len));
    auto textBlob = tgfx::TextBlob::MakeFrom(str, font);
    if (textBlob) {
        state.canvas->drawTextBlob(textBlob, x, y, paint);
    }
    return 0;
}

int32_t host_render_end_frame(void) {
    if (state.context) {
        bool result = state.context->flushAndSubmit(true);
        if (!result) {
            printf("host_render_end_frame: flushAndSubmit returned false.\n");
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
        state.device->unlock();
        
        // Force the GLFW context current explicitly, because tgfx's unlock may not be reliable
        // across differing OS context management paradigms like CGL vs NSOpenGL.
        // We MUST set it to NULL first because GLFW maintains a TLS cache of the current context
        // and will NO-OP if it thinks the window is already current!
        extern void* host_ui_get_window(void);
        glfwMakeContextCurrent(NULL);
        glfwMakeContextCurrent((GLFWwindow*)host_ui_get_window());

        if (hasTex) {
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
            
        } else {
            // printf("host_render_end_frame: Failed to retrieve GL texture from surface.\n");
        }

        state.canvas = nullptr;
        state.surface = nullptr;
        state.context = nullptr;
    }
    return 0;
}

}
