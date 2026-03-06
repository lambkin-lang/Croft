#include "croft/host_render.h"
#include "croft/host_ui.h"

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <CoreGraphics/CoreGraphics.h>
#include <cmath>
#include <cstdio>
#include <vector>

#include <simd/simd.h>

@interface CroftTextTextureEntry : NSObject
@property(nonatomic, strong) id<MTLTexture> texture;
@property(nonatomic, assign) NSUInteger width;
@property(nonatomic, assign) NSUInteger height;
@end

@implementation CroftTextTextureEntry
@end

namespace {

struct RenderState {
    CGAffineTransform transform;
    bool has_clip;
    CGRect clip_device;
};

struct ColorVertex {
    simd::float2 position;
    simd::float4 color;
};

struct TextureVertex {
    simd::float2 position;
    simd::float2 tex_coord;
    simd::float4 color;
};

static __strong NSView* g_view = nil;
static __strong CAMetalLayer* g_layer = nil;
static __strong id<MTLDevice> g_device = nil;
static __strong id<MTLCommandQueue> g_command_queue = nil;
static __strong id<MTLLibrary> g_library = nil;
static __strong id<MTLRenderPipelineState> g_color_pipeline = nil;
static __strong id<MTLRenderPipelineState> g_text_pipeline = nil;
static __strong id<MTLSamplerState> g_text_sampler = nil;
static __strong id<CAMetalDrawable> g_drawable = nil;
static __strong id<MTLCommandBuffer> g_command_buffer = nil;
static __strong id<MTLRenderCommandEncoder> g_encoder = nil;
static __strong NSMutableDictionary<NSString*, CroftTextTextureEntry*>* g_text_cache = nil;

/*
 * The direct-Metal path deliberately keeps rendering small and explicit.
 * Text shaping, caret policy, IME composition, and accessibility remain
 * separate join-points above this renderer rather than being hidden inside one
 * "editor widget" abstraction.
 */
static uint32_t g_frame_width = 0;
static uint32_t g_frame_height = 0;
static MTLClearColor g_clear_color = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
static bool g_encoder_started = false;
static std::vector<RenderState> g_state_stack;

static constexpr const char* kShaderSource = R"msl(
#include <metal_stdlib>
using namespace metal;

struct ColorVertexIn {
    float2 position;
    float4 color;
};

struct TextureVertexIn {
    float2 position;
    float2 texCoord;
    float4 color;
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
    float2 texCoord;
};

vertex VertexOut croftColorVertex(const device ColorVertexIn* vertices [[buffer(0)]],
                                  uint vertexID [[vertex_id]]) {
    VertexOut out;
    ColorVertexIn in = vertices[vertexID];
    out.position = float4(in.position, 0.0, 1.0);
    out.color = in.color;
    out.texCoord = float2(0.0, 0.0);
    return out;
}

vertex VertexOut croftTextVertex(const device TextureVertexIn* vertices [[buffer(0)]],
                                 uint vertexID [[vertex_id]]) {
    VertexOut out;
    TextureVertexIn in = vertices[vertexID];
    out.position = float4(in.position, 0.0, 1.0);
    out.color = in.color;
    out.texCoord = in.texCoord;
    return out;
}

fragment float4 croftColorFragment(VertexOut in [[stage_in]]) {
    return in.color;
}

fragment float4 croftTextFragment(VertexOut in [[stage_in]],
                                  texture2d<float> textTexture [[texture(0)]],
                                  sampler textSampler [[sampler(0)]]) {
    float alpha = textTexture.sample(textSampler, in.texCoord).a;
    return float4(in.color.rgb, in.color.a * alpha);
}
)msl";

static RenderState& current_state() {
    return g_state_stack.back();
}

static simd::float4 rgba_to_color(uint32_t color_rgba) {
    const float r = static_cast<float>((color_rgba >> 24) & 0xFF) / 255.0f;
    const float g = static_cast<float>((color_rgba >> 16) & 0xFF) / 255.0f;
    const float b = static_cast<float>((color_rgba >> 8) & 0xFF) / 255.0f;
    const float a = static_cast<float>(color_rgba & 0xFF) / 255.0f;
    return simd::float4{r, g, b, a};
}

static CGPoint transform_point(CGPoint point, const CGAffineTransform& transform) {
    return CGPointApplyAffineTransform(point, transform);
}

static CGRect transformed_rect(float x, float y, float w, float h, const CGAffineTransform& transform) {
    const CGPoint p0 = transform_point(CGPointMake(x, y), transform);
    const CGPoint p1 = transform_point(CGPointMake(x + w, y), transform);
    const CGPoint p2 = transform_point(CGPointMake(x, y + h), transform);
    const CGPoint p3 = transform_point(CGPointMake(x + w, y + h), transform);

    const CGFloat min_x = std::fmin(std::fmin(p0.x, p1.x), std::fmin(p2.x, p3.x));
    const CGFloat max_x = std::fmax(std::fmax(p0.x, p1.x), std::fmax(p2.x, p3.x));
    const CGFloat min_y = std::fmin(std::fmin(p0.y, p1.y), std::fmin(p2.y, p3.y));
    const CGFloat max_y = std::fmax(std::fmax(p0.y, p1.y), std::fmax(p2.y, p3.y));

    return CGRectMake(min_x, min_y, max_x - min_x, max_y - min_y);
}

static simd::float2 device_to_ndc(CGFloat x, CGFloat y) {
    const float width = g_frame_width == 0 ? 1.0f : static_cast<float>(g_frame_width);
    const float height = g_frame_height == 0 ? 1.0f : static_cast<float>(g_frame_height);
    const float ndc_x = (2.0f * static_cast<float>(x) / width) - 1.0f;
    const float ndc_y = 1.0f - (2.0f * static_cast<float>(y) / height);
    return simd::float2{ndc_x, ndc_y};
}

static MTLScissorRect current_scissor_rect() {
    CGRect clip = CGRectMake(0.0, 0.0, static_cast<CGFloat>(g_frame_width), static_cast<CGFloat>(g_frame_height));
    if (current_state().has_clip) {
        clip = CGRectIntersection(clip, current_state().clip_device);
    }

    if (CGRectIsEmpty(clip) || g_frame_width == 0 || g_frame_height == 0) {
        return MTLScissorRect{0, 0, 0, 0};
    }

    const CGFloat min_x = std::fmax(0.0, std::floor(CGRectGetMinX(clip)));
    const CGFloat min_y = std::fmax(0.0, std::floor(CGRectGetMinY(clip)));
    const CGFloat max_x = std::fmin(static_cast<CGFloat>(g_frame_width), std::ceil(CGRectGetMaxX(clip)));
    const CGFloat max_y = std::fmin(static_cast<CGFloat>(g_frame_height), std::ceil(CGRectGetMaxY(clip)));

    return MTLScissorRect{
        static_cast<NSUInteger>(min_x),
        static_cast<NSUInteger>(min_y),
        static_cast<NSUInteger>(std::fmax(0.0, max_x - min_x)),
        static_cast<NSUInteger>(std::fmax(0.0, max_y - min_y))
    };
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

static bool ensure_encoder_started(bool use_clear_color) {
    if (g_encoder_started) {
        return true;
    }
    if (!g_command_buffer || !g_drawable) {
        return false;
    }

    MTLRenderPassDescriptor* descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    descriptor.colorAttachments[0].texture = g_drawable.texture;
    descriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    descriptor.colorAttachments[0].clearColor = use_clear_color ? g_clear_color
                                                               : MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

    g_encoder = [g_command_buffer renderCommandEncoderWithDescriptor:descriptor];
    if (!g_encoder) {
        return false;
    }

    [g_encoder setCullMode:MTLCullModeNone];
    [g_encoder setFrontFacingWinding:MTLWindingCounterClockwise];
    g_encoder_started = true;
    return true;
}

static int32_t encode_rect(CGRect rect_device, simd::float4 color) {
    if (!ensure_encoder_started(false) || CGRectIsEmpty(rect_device)) {
        return -1;
    }

    const CGFloat x0 = CGRectGetMinX(rect_device);
    const CGFloat y0 = CGRectGetMinY(rect_device);
    const CGFloat x1 = CGRectGetMaxX(rect_device);
    const CGFloat y1 = CGRectGetMaxY(rect_device);

    const simd::float2 p0 = device_to_ndc(x0, y0);
    const simd::float2 p1 = device_to_ndc(x1, y0);
    const simd::float2 p2 = device_to_ndc(x0, y1);
    const simd::float2 p3 = device_to_ndc(x1, y1);

    const ColorVertex vertices[6] = {
        {p0, color}, {p2, color}, {p1, color},
        {p1, color}, {p2, color}, {p3, color},
    };

    [g_encoder setRenderPipelineState:g_color_pipeline];
    [g_encoder setScissorRect:current_scissor_rect()];
    [g_encoder setVertexBytes:vertices length:sizeof(vertices) atIndex:0];
    [g_encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    return 0;
}

static int32_t encode_textured_rect(CGRect rect_device, id<MTLTexture> texture, simd::float4 color) {
    if (!ensure_encoder_started(false) || CGRectIsEmpty(rect_device) || !texture) {
        return -1;
    }

    const CGFloat x0 = CGRectGetMinX(rect_device);
    const CGFloat y0 = CGRectGetMinY(rect_device);
    const CGFloat x1 = CGRectGetMaxX(rect_device);
    const CGFloat y1 = CGRectGetMaxY(rect_device);

    const simd::float2 p0 = device_to_ndc(x0, y0);
    const simd::float2 p1 = device_to_ndc(x1, y0);
    const simd::float2 p2 = device_to_ndc(x0, y1);
    const simd::float2 p3 = device_to_ndc(x1, y1);

    const TextureVertex vertices[6] = {
        {p0, simd::float2{0.0f, 0.0f}, color},
        {p2, simd::float2{0.0f, 1.0f}, color},
        {p1, simd::float2{1.0f, 0.0f}, color},
        {p1, simd::float2{1.0f, 0.0f}, color},
        {p2, simd::float2{0.0f, 1.0f}, color},
        {p3, simd::float2{1.0f, 1.0f}, color},
    };

    [g_encoder setRenderPipelineState:g_text_pipeline];
    [g_encoder setScissorRect:current_scissor_rect()];
    [g_encoder setFragmentTexture:texture atIndex:0];
    [g_encoder setFragmentSamplerState:g_text_sampler atIndex:0];
    [g_encoder setVertexBytes:vertices length:sizeof(vertices) atIndex:0];
    [g_encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    return 0;
}

static NSFont* resolve_font(float font_size) {
    NSFont* font = [NSFont fontWithName:@"Helvetica" size:font_size];
    if (!font) {
        font = [NSFont systemFontOfSize:font_size];
    }
    return font;
}

static NSString* text_cache_key(NSString* string, float font_size) {
    return [NSString stringWithFormat:@"%.2f|%@", font_size, string];
}

static CroftTextTextureEntry* create_text_texture_entry(NSString* string, float font_size) {
    NSFont* font;
    NSDictionary* attrs;
    NSSize measured;
    NSUInteger width;
    NSUInteger height;
    NSBitmapImageRep* bitmap;
    NSGraphicsContext* context;
    MTLTextureDescriptor* descriptor;
    id<MTLTexture> texture;
    CroftTextTextureEntry* entry;

    if (!g_device || !string || string.length == 0) {
        return nil;
    }

    font = resolve_font(font_size);
    attrs = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: [NSColor whiteColor]
    };
    measured = [string sizeWithAttributes:attrs];
    width = static_cast<NSUInteger>(std::max(1.0, std::ceil(measured.width)));
    height = static_cast<NSUInteger>(std::max(1.0, std::ceil(measured.height)));

    bitmap = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:nil
                      pixelsWide:(NSInteger)width
                      pixelsHigh:(NSInteger)height
                   bitsPerSample:8
                 samplesPerPixel:4
                        hasAlpha:YES
                        isPlanar:NO
                  colorSpaceName:NSCalibratedRGBColorSpace
                     bitmapFormat:0
                      bytesPerRow:(NSInteger)(width * 4)
                     bitsPerPixel:32];
    if (!bitmap) {
        return nil;
    }

    context = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:context];
    [[NSColor clearColor] setFill];
    NSRectFill(NSMakeRect(0.0, 0.0, (CGFloat)width, (CGFloat)height));
    [string drawAtPoint:NSMakePoint(0.0, 0.0) withAttributes:attrs];
    [context flushGraphics];
    [NSGraphicsContext restoreGraphicsState];

    descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                    width:width
                                                                   height:height
                                                                mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
#if TARGET_OS_OSX
    descriptor.storageMode = MTLStorageModeShared;
#endif
    texture = [g_device newTextureWithDescriptor:descriptor];
    if (!texture) {
        return nil;
    }

    [texture replaceRegion:MTLRegionMake2D(0, 0, width, height)
               mipmapLevel:0
                 withBytes:bitmap.bitmapData
               bytesPerRow:(NSUInteger)bitmap.bytesPerRow];

    entry = [[CroftTextTextureEntry alloc] init];
    entry.texture = texture;
    entry.width = width;
    entry.height = height;
    return entry;
}

static CroftTextTextureEntry* cached_text_texture_entry(NSString* string, float font_size) {
    NSString* key;
    CroftTextTextureEntry* entry;

    if (!string || string.length == 0) {
        return nil;
    }

    if (!g_text_cache) {
        g_text_cache = [[NSMutableDictionary alloc] init];
    }

    key = text_cache_key(string, font_size);
    entry = [g_text_cache objectForKey:key];
    if (entry) {
        return entry;
    }

    entry = create_text_texture_entry(string, font_size);
    if (entry) {
        [g_text_cache setObject:entry forKey:key];
    }
    return entry;
}

} // namespace

extern "C" {

int32_t host_render_init(void) {
    NSWindow* window = (__bridge NSWindow*)host_ui_get_native_window();
    NSError* error = nil;
    NSString* source = nil;
    id<MTLFunction> color_vertex_fn = nil;
    id<MTLFunction> color_fragment_fn = nil;
    id<MTLFunction> text_vertex_fn = nil;
    id<MTLFunction> text_fragment_fn = nil;
    MTLRenderPipelineDescriptor* pipeline = nil;
    MTLSamplerDescriptor* sampler_descriptor = nil;

    if (!window) {
        std::printf("host_render_init(native metal): missing Cocoa window.\n");
        return -1;
    }

    g_view = window.contentView;
    if (!g_view) {
        std::printf("host_render_init(native metal): missing Cocoa content view.\n");
        return -1;
    }

    g_device = MTLCreateSystemDefaultDevice();
    if (!g_device) {
        std::printf("host_render_init(native metal): failed to create MTLDevice.\n");
        return -1;
    }

    g_command_queue = [g_device newCommandQueue];
    if (!g_command_queue) {
        std::printf("host_render_init(native metal): failed to create command queue.\n");
        g_device = nil;
        return -1;
    }

    source = [NSString stringWithUTF8String:kShaderSource];
    g_library = [g_device newLibraryWithSource:source options:nil error:&error];
    if (!g_library) {
        std::printf("host_render_init(native metal): shader compile failed: %s\n",
                    error ? error.localizedDescription.UTF8String : "unknown");
        g_command_queue = nil;
        g_device = nil;
        return -1;
    }

    color_vertex_fn = [g_library newFunctionWithName:@"croftColorVertex"];
    color_fragment_fn = [g_library newFunctionWithName:@"croftColorFragment"];
    text_vertex_fn = [g_library newFunctionWithName:@"croftTextVertex"];
    text_fragment_fn = [g_library newFunctionWithName:@"croftTextFragment"];
    if (!color_vertex_fn || !color_fragment_fn || !text_vertex_fn || !text_fragment_fn) {
        std::printf("host_render_init(native metal): missing shader entry points.\n");
        g_library = nil;
        g_command_queue = nil;
        g_device = nil;
        return -1;
    }

    pipeline = [[MTLRenderPipelineDescriptor alloc] init];
    pipeline.vertexFunction = color_vertex_fn;
    pipeline.fragmentFunction = color_fragment_fn;
    pipeline.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pipeline.colorAttachments[0].blendingEnabled = YES;
    pipeline.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    pipeline.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    pipeline.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pipeline.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
    pipeline.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipeline.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    g_color_pipeline = [g_device newRenderPipelineStateWithDescriptor:pipeline error:&error];
    if (!g_color_pipeline) {
        std::printf("host_render_init(native metal): color pipeline creation failed: %s\n",
                    error ? error.localizedDescription.UTF8String : "unknown");
        g_library = nil;
        g_command_queue = nil;
        g_device = nil;
        return -1;
    }

    pipeline = [[MTLRenderPipelineDescriptor alloc] init];
    pipeline.vertexFunction = text_vertex_fn;
    pipeline.fragmentFunction = text_fragment_fn;
    pipeline.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pipeline.colorAttachments[0].blendingEnabled = YES;
    pipeline.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    pipeline.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    pipeline.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pipeline.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
    pipeline.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipeline.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    g_text_pipeline = [g_device newRenderPipelineStateWithDescriptor:pipeline error:&error];
    if (!g_text_pipeline) {
        std::printf("host_render_init(native metal): text pipeline creation failed: %s\n",
                    error ? error.localizedDescription.UTF8String : "unknown");
        g_color_pipeline = nil;
        g_library = nil;
        g_command_queue = nil;
        g_device = nil;
        return -1;
    }

    sampler_descriptor = [[MTLSamplerDescriptor alloc] init];
    sampler_descriptor.minFilter = MTLSamplerMinMagFilterLinear;
    sampler_descriptor.magFilter = MTLSamplerMinMagFilterLinear;
    sampler_descriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler_descriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
    g_text_sampler = [g_device newSamplerStateWithDescriptor:sampler_descriptor];

    g_layer = [CAMetalLayer layer];
    g_layer.device = g_device;
    g_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    g_layer.framebufferOnly = YES;
    g_layer.contentsScale = window.backingScaleFactor;
    g_layer.frame = g_view.bounds;
    g_layer.drawableSize = CGSizeMake(g_view.bounds.size.width * g_layer.contentsScale,
                                      g_view.bounds.size.height * g_layer.contentsScale);

    g_view.wantsLayer = YES;
    g_view.layer = g_layer;
    g_text_cache = [[NSMutableDictionary alloc] init];
    return 0;
}

void host_render_terminate(void) {
    if (g_encoder) {
        [g_encoder endEncoding];
    }

    g_encoder = nil;
    g_command_buffer = nil;
    g_drawable = nil;
    g_text_sampler = nil;
    g_text_pipeline = nil;
    g_color_pipeline = nil;
    g_library = nil;
    g_command_queue = nil;
    g_device = nil;
    g_text_cache = nil;
    g_state_stack.clear();
    g_encoder_started = false;
    g_frame_width = 0;
    g_frame_height = 0;

    if (g_view && g_view.layer == g_layer) {
        g_view.layer = nil;
    }

    g_layer = nil;
    g_view = nil;
}

int32_t host_render_begin_frame(uint32_t width, uint32_t height) {
    if (!g_device || !g_command_queue || !g_layer || width == 0 || height == 0) {
        return -1;
    }

    g_frame_width = width;
    g_frame_height = height;
    update_layer_size(width, height);

    g_drawable = [g_layer nextDrawable];
    if (!g_drawable) {
        return -1;
    }

    g_command_buffer = [g_command_queue commandBuffer];
    if (!g_command_buffer) {
        g_drawable = nil;
        return -1;
    }

    g_encoder = nil;
    g_encoder_started = false;
    g_state_stack.clear();
    g_state_stack.push_back(RenderState{CGAffineTransformIdentity, false, CGRectZero});
    return 0;
}

void host_render_save(void) {
    if (!g_state_stack.empty()) {
        g_state_stack.push_back(current_state());
    }
}

void host_render_restore(void) {
    if (g_state_stack.size() > 1) {
        g_state_stack.pop_back();
    }
}

void host_render_translate(float dx, float dy) {
    if (!g_state_stack.empty()) {
        current_state().transform = CGAffineTransformTranslate(current_state().transform, dx, dy);
    }
}

void host_render_scale(float sx, float sy) {
    if (!g_state_stack.empty()) {
        current_state().transform = CGAffineTransformScale(current_state().transform, sx, sy);
    }
}

void host_render_clip_rect(float x, float y, float w, float h) {
    if (g_state_stack.empty()) {
        return;
    }

    CGRect clip = transformed_rect(x, y, w, h, current_state().transform);
    if (current_state().has_clip) {
        clip = CGRectIntersection(current_state().clip_device, clip);
    }

    current_state().has_clip = true;
    current_state().clip_device = clip;
}

int32_t host_render_clear(uint32_t color_rgba) {
    if (!g_command_buffer || !g_drawable) {
        return -1;
    }
    if (g_encoder_started) {
        std::printf("host_render_clear(native metal): clear after draw is not supported in this prototype.\n");
        return -1;
    }

    const simd::float4 color = rgba_to_color(color_rgba);
    g_clear_color = MTLClearColorMake(color.x, color.y, color.z, color.w);
    return ensure_encoder_started(true) ? 0 : -1;
}

int32_t host_render_draw_rect(float x, float y, float w, float h, uint32_t color_rgba) {
    if (g_state_stack.empty()) {
        return -1;
    }

    CGRect rect = transformed_rect(x, y, w, h, current_state().transform);
    return encode_rect(rect, rgba_to_color(color_rgba));
}

int32_t host_render_draw_text(float x,
                              float y,
                              const char* text,
                              uint32_t len,
                              float font_size,
                              uint32_t color_rgba) {
    NSString* string = nil;
    CroftTextTextureEntry* entry = nil;
    CGRect rect;

    if (g_state_stack.empty() || !text || len == 0) {
        return 0;
    }

    string = [[NSString alloc] initWithBytes:text length:len encoding:NSUTF8StringEncoding];
    if (!string) {
        return -1;
    }

    entry = cached_text_texture_entry(string, font_size);
    if (!entry || !entry.texture) {
        return -1;
    }

    rect = transformed_rect(x,
                            y - font_size,
                            static_cast<float>(entry.width),
                            static_cast<float>(entry.height),
                            current_state().transform);
    return encode_textured_rect(rect, entry.texture, rgba_to_color(color_rgba));
}

float host_render_measure_text(const char* text, uint32_t len, float font_size) {
    if (!text || len == 0) {
        return 0.0f;
    }

    NSString* string = [[NSString alloc] initWithBytes:text
                                                length:len
                                              encoding:NSUTF8StringEncoding];
    if (!string) {
        return 0.0f;
    }

    NSDictionary* attrs = @{
        NSFontAttributeName: resolve_font(font_size)
    };
    NSSize size = [string sizeWithAttributes:attrs];
    return static_cast<float>(std::ceil(size.width));
}

int32_t host_render_end_frame(void) {
    if (!g_command_buffer || !g_drawable) {
        return -1;
    }

    if (!g_encoder_started && !ensure_encoder_started(true)) {
        g_command_buffer = nil;
        g_drawable = nil;
        return -1;
    }

    [g_encoder endEncoding];
    [g_command_buffer presentDrawable:g_drawable];
    [g_command_buffer commit];

    g_encoder = nil;
    g_command_buffer = nil;
    g_drawable = nil;
    g_encoder_started = false;
    g_state_stack.clear();
    g_frame_width = 0;
    g_frame_height = 0;
    return 0;
}

} // extern "C"
