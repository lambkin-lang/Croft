#ifndef CROFT_EDITOR_SCENE_RUNTIME_H
#define CROFT_EDITOR_SCENE_RUNTIME_H

#include "croft/scene.h"

typedef struct croft_editor_scene_runtime_state {
    int needs_redraw;
    int last_cursor_blink_visible;
    uint32_t last_framebuffer_width;
    uint32_t last_framebuffer_height;
} croft_editor_scene_runtime_state;

void croft_editor_scene_runtime_state_init(croft_editor_scene_runtime_state* state);
void croft_editor_scene_runtime_request_redraw(croft_editor_scene_runtime_state* state);
int croft_editor_scene_runtime_should_auto_close(uint64_t start_ms,
                                                 uint64_t now_ms,
                                                 uint32_t auto_close_ms);
void croft_editor_scene_runtime_sync_bounds(croft_editor_scene_runtime_state* state,
                                            viewport_node* root,
                                            text_editor_node* editor,
                                            uint32_t framebuffer_width,
                                            uint32_t framebuffer_height,
                                            float window_padding);
void croft_editor_scene_runtime_sync_cursor_blink(croft_editor_scene_runtime_state* state,
                                                  const text_editor_node* editor,
                                                  uint64_t now_ms);
int croft_editor_scene_runtime_needs_redraw(const croft_editor_scene_runtime_state* state);
void croft_editor_scene_runtime_note_frame_rendered(croft_editor_scene_runtime_state* state);

#endif
