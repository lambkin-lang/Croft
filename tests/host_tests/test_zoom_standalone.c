#include "croft/host_ui.h"
#include "croft/host_render.h"
#include "croft/scene.h"
#include "croft/host_gesture.h"
#include <stdio.h>

static int g_running = 1;
static viewport_node g_root_vp;
static code_block_node g_blocks[3];
static double g_mouse_x = 0;
static double g_mouse_y = 0;

void on_ui_event(int32_t type, int32_t arg0, int32_t arg1) {
    if (type == CROFT_UI_EVENT_SCROLL) {
        // arg0 = xoffset * 1000, arg1 = yoffset * 1000
        float delta = (float)arg1 / 1000.0f;
        
        float old_scale = g_root_vp.scale;
        float new_scale = old_scale * (1.0f + delta * 0.1f);
        
        // Clamp scale to reasonable bounds to prevent flipping or vanishing
        if (new_scale < 0.1f) new_scale = 0.1f;
        if (new_scale > 10.0f) new_scale = 10.0f;
        
        // Zoom around the center of the screen
        uint32_t fw, fh;
        host_ui_get_framebuffer_size(&fw, &fh);
        float cx = (float)fw / 2.0f;
        float cy = (float)fh / 2.0f;
        
        float scale_ratio = new_scale / old_scale;
        
        g_root_vp.scroll_x = cx - (cx - g_root_vp.scroll_x) * scale_ratio;
        g_root_vp.scroll_y = cy - (cy - g_root_vp.scroll_y) * scale_ratio;
        g_root_vp.scale = new_scale;
        
        printf("Zooming (Scroll)! Scale: %.2f | Scroll Offset: (%.1f, %.1f)\n", 
               g_root_vp.scale, g_root_vp.scroll_x, g_root_vp.scroll_y);
    }
    else if (type == CROFT_UI_EVENT_ZOOM_GESTURE) {
        // arg0 = magnification * 1000000
        float delta = (float)arg0 / 1000000.0f;
        
        float old_scale = g_root_vp.scale;
        float new_scale = old_scale * (1.0f + delta); // Native swipe gives true scale ratios
        
        if (new_scale < 0.1f) new_scale = 0.1f;
        if (new_scale > 10.0f) new_scale = 10.0f;
        
        uint32_t fw, fh;
        host_ui_get_framebuffer_size(&fw, &fh);
        float cx = (float)fw / 2.0f;
        float cy = (float)fh / 2.0f;
        
        float scale_ratio = new_scale / old_scale;
        
        g_root_vp.scroll_x = cx - (cx - g_root_vp.scroll_x) * scale_ratio;
        g_root_vp.scroll_y = cy - (cy - g_root_vp.scroll_y) * scale_ratio;
        g_root_vp.scale = new_scale;
        
        printf("Zooming (Pinch)!  Scale: %.2f | Scroll Offset: (%.1f, %.1f)\n", 
               g_root_vp.scale, g_root_vp.scroll_x, g_root_vp.scroll_y);
    } 
    else if (type == CROFT_UI_EVENT_MOUSE && arg0 == 0 && arg1 == 1) { // Left click press
        host_ui_get_mouse_pos(&g_mouse_x, &g_mouse_y);
        hit_result hit;
        scene_node_hit_test_tree((scene_node*)&g_root_vp, (float)g_mouse_x, (float)g_mouse_y, &hit);
        
        if (hit.node) {
            // N.B. In a real application we'd use robust downcasting/RTTI tags to identify the node payload
            printf("CLICKED: Hit tested at local coords (%.1f, %.1f).\n", hit.local_x, hit.local_y);
            
            // Just for logging intuition, see if it's one of our code blocks
            for (int i = 0; i < 3; i++) {
                if (hit.node == (scene_node*)&g_blocks[i]) {
                    printf("         -> Hit Code Block %d ('%s')!\n", i, g_blocks[i].text);
                }
            }
        } else {
            printf("CLICKED: Background miss.\n");
        }
    }
}

int main(void) {
    if (host_ui_init() != 0) return 1;
    if (host_ui_create_window(1000, 800, "Infinite Canvas MVP") != 0) return 1;
    if (host_render_init() != 0) return 1;
    
    // Wire macOS native gestures to our callback!
#ifdef __APPLE__
    host_gesture_mac_init(host_ui_get_native_window(), (void *)on_ui_event);
#endif

    host_ui_set_event_callback(on_ui_event);
    
    // Setup scene
    viewport_node_init(&g_root_vp, 0, 0, 1000, 800);
    g_root_vp.scroll_x = 0;
    g_root_vp.scroll_y = 0;
    g_root_vp.scale = 1.0f;
    
    // Setup Code Bubbles spatially
    code_block_node_init(&g_blocks[0], 100, 100, 300, 150, "func main() {\n    return 0;\n}");
    code_block_node_init(&g_blocks[1], 500, 200, 300, 150, "struct Data { int x; }");
    code_block_node_init(&g_blocks[2], 250, 400, 400, 80, "print(\"Spatial!\");");
    
    scene_node_add_child((scene_node*)&g_root_vp, (scene_node*)&g_blocks[0]);
    scene_node_add_child((scene_node*)&g_root_vp, (scene_node*)&g_blocks[1]);
    scene_node_add_child((scene_node*)&g_root_vp, (scene_node*)&g_blocks[2]);
    
    render_ctx rc = { .bg_color = 0xFFFFFFFF, .fg_color = 0x000000FF };
    
    printf("Infinite Canvas Running.\n- Scroll to Zoom.\n- Click to hit-test bubbles!\n");
    
    while (g_running && !host_ui_should_close()) {
        host_ui_poll_events();
        
        uint32_t fw, fh;
        host_ui_get_framebuffer_size(&fw, &fh);
        g_root_vp.base.sx = (float)fw;
        g_root_vp.base.sy = (float)fh;
        
        if (host_render_begin_frame(fw, fh) == 0) {
            host_render_clear(0xF0F0F0FF); // Light grey outer canvas
            
            // Render the scene graph
            scene_node_draw_tree((scene_node*)&g_root_vp, &rc);
            
            host_render_end_frame();
            host_ui_swap_buffers();
        }
    }
    
    host_render_terminate();
    host_ui_terminate();
    return 0;
}
