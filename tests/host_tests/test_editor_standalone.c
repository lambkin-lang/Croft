#include "croft/host_ui.h"
#include "croft/host_render.h"
#include "croft/scene.h"
#include "croft/host_fs.h"
#include <sapling/sapling.h>
#include <sapling/txn.h>
#include <sapling/text.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_running = 1;
static viewport_node g_root_vp;
static text_editor_node g_editor;
static scene_node *g_focused_node = NULL;
static double g_mouse_x = 0;
static double g_mouse_y = 0;

static SapMemArena *g_arena = NULL;
static SapEnv *g_env = NULL;

extern int sap_seq_subsystem_init(SapEnv *env);

static void on_ui_event(int32_t type, int32_t arg0, int32_t arg1) {
    if (type == CROFT_UI_EVENT_KEY && arg0 == 256 /* Escape */ && arg1 == 1 /* Press */) {
        g_running = 0;
    }
    else if (type == CROFT_UI_EVENT_KEY && arg0 == 81 /* Q */ && arg1 == 1) { // Cmd+Q
        g_running = 0;
    }
    else if (type == CROFT_UI_EVENT_KEY) {
        if (g_focused_node && g_focused_node->vtbl && g_focused_node->vtbl->on_key_event) {
            g_focused_node->vtbl->on_key_event(g_focused_node, arg0, arg1);
        }
    }
    else if (type == CROFT_UI_EVENT_CHAR) {
        if (g_focused_node && g_focused_node->vtbl && g_focused_node->vtbl->on_char_event) {
            g_focused_node->vtbl->on_char_event(g_focused_node, (uint32_t)arg0);
        }
    }
    else if (type == CROFT_UI_EVENT_SCROLL) {
        // Simple vertical scrolling for MVP (natural scrolling)
        float dy = (float)arg1 / 1000.0f;
        g_editor.scroll_y += dy * 20.0f; // changed to +=
        if (g_editor.scroll_y > 0) g_editor.scroll_y = 0; // Don't scroll above top
        
        // Horizontal scroll
        float dx = (float)arg0 / 1000.0f;
        g_editor.scroll_x += dx * 20.0f; // changed to +=
        if (g_editor.scroll_x > 0) g_editor.scroll_x = 0;

        printf("Editor Scroll Offset: (%.1f, %.1f)\n", g_editor.scroll_x, g_editor.scroll_y);
    }
    else if (type == CROFT_UI_EVENT_MOUSE) {
        if (arg1 == 1) { // press
            host_ui_get_mouse_pos(&g_mouse_x, &g_mouse_y);
            hit_result hit;
            scene_node_hit_test_tree(&g_root_vp.base, (float)g_mouse_x, (float)g_mouse_y, &hit);
            if (hit.node) {
                g_focused_node = hit.node;
                printf("Focus acquired on node. Start focus index: %u\n", g_editor.sel_start);
                if (hit.node->vtbl && hit.node->vtbl->on_mouse_event) {
                    hit.node->vtbl->on_mouse_event(hit.node, 1, hit.local_x, hit.local_y);
                }
            }
        } else if (arg1 == 0) { // release
            if (g_focused_node && g_focused_node->vtbl && g_focused_node->vtbl->on_mouse_event) {
                g_focused_node->vtbl->on_mouse_event(g_focused_node, 0, 0, 0); 
            }
            // Keep g_focused_node set so that it can receive keyboard events!
        }
    }
    else if (type == CROFT_UI_EVENT_CURSOR_POS) {
        if (g_focused_node && g_focused_node->vtbl && g_focused_node->vtbl->on_mouse_event) {
            host_ui_get_mouse_pos(&g_mouse_x, &g_mouse_y);
            float lx = (float)g_mouse_x, ly = (float)g_mouse_y;
            // Hacky MVP path finding since we know the tree is root -> editor
            if (g_root_vp.base.vtbl && g_root_vp.base.vtbl->transform_coords) {
                g_root_vp.base.vtbl->transform_coords(&g_root_vp.base, &lx, &ly);
            }
            lx -= g_editor.base.x;
            ly -= g_editor.base.y;
            g_focused_node->vtbl->on_mouse_event(g_focused_node, 3, lx, ly);
        }
    }
}

int main(int argc, char **argv) {
    host_fs_init(argc > 0 ? argv[0] : NULL);
    
    // Initialize Sapling tree for text storage
    SapArenaOptions opts = {0};
    opts.type = SAP_ARENA_BACKING_MALLOC;
    opts.page_size = 4096;
    if (sap_arena_init(&g_arena, &opts) != ERR_OK) return 1;
    
    g_env = sap_env_create(g_arena, 4096);
    if (!g_env) return 1;
    sap_seq_subsystem_init(g_env);
    
    Text *text = text_new(g_env);
    
    // Read a source file into the text editor
    const char *target_file = argc > 1 ? argv[1] : "CMakeLists.txt";
    uint64_t fd;
    if (host_fs_open(target_file, strlen(target_file), HOST_FS_O_RDONLY, &fd) == HOST_FS_OK) {
        uint64_t fsize = 0;
        host_fs_file_size(fd, &fsize);
        if (fsize > 0) {
            uint8_t *buf = (uint8_t*)malloc(fsize);
            uint32_t read_bytes = 0;
            host_fs_read(fd, buf, fsize, &read_bytes);
            
            SapTxnCtx *txn = sap_txn_begin(g_env, NULL, 0);
            text_from_utf8(txn, text, buf, read_bytes);
            sap_txn_commit(txn);
            
            free(buf);
        }
        host_fs_close(fd);
    }
    
    // UI Initialization
    if (host_ui_init() != 0) return 1;
    if (host_ui_create_window(1000, 800, "Text Editor Core MVP") != 0) return 1;
    if (host_render_init() != 0) return 1;
    
    host_ui_set_event_callback(on_ui_event);
    
    // Setup Scene Graph
    uint32_t fw, fh;
    host_ui_get_framebuffer_size(&fw, &fh);
    
    viewport_node_init(&g_root_vp, 0, 0, (float)fw, (float)fh);
    text_editor_node_init(&g_editor, g_env, 50, 50, (float)fw - 100, (float)fh - 100, text);
    
    scene_node_add_child(&g_root_vp.base, &g_editor.base);
    
    // Main loop
    while (g_running && !host_ui_should_close()) {
        host_ui_poll_events();
        
        host_ui_get_framebuffer_size(&fw, &fh);
        g_root_vp.base.sx = (float)fw;
        g_root_vp.base.sy = (float)fh;
        g_editor.base.sx = (float)fw - 100;
        g_editor.base.sy = (float)fh - 100;
        
        if (host_render_begin_frame(fw, fh) == 0) {
            host_render_clear(0x0000FFFF); // Solid blue background outside the editor code
            
            render_ctx rc = {
                .fg_color = 0x111111FF, // Very dark text (RGBA)
                .bg_color = 0xE0E0E0FF, // Light gray background (RGBA)
                .time = host_ui_get_time()
            };
            
            scene_node_draw_tree(&g_root_vp.base, &rc);
            
            host_render_end_frame();
            host_ui_swap_buffers();
        }
    }
    
    host_render_terminate();
    host_ui_terminate();
    text_editor_node_dispose(&g_editor);
    
    // Cleanup sapling
    text_free(g_env, text);
    sap_env_destroy(g_env);
    sap_arena_destroy(g_arena);
    
    return 0;
}
