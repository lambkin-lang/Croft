#include "croft/editor_scene_runtime.h"

enum {
    CROFT_EDITOR_CURSOR_BLINK_INTERVAL_MS = 500
};

void croft_editor_scene_runtime_state_init(croft_editor_scene_runtime_state* state)
{
    if (!state) {
        return;
    }

    state->needs_redraw = 1;
    state->last_cursor_blink_visible = -1;
    state->last_framebuffer_width = 0u;
    state->last_framebuffer_height = 0u;
}

void croft_editor_scene_runtime_request_redraw(croft_editor_scene_runtime_state* state)
{
    if (!state) {
        return;
    }

    state->needs_redraw = 1;
}

int croft_editor_scene_runtime_should_auto_close(uint64_t start_ms,
                                                 uint64_t now_ms,
                                                 uint32_t auto_close_ms)
{
    if (auto_close_ms == 0u || now_ms < start_ms) {
        return 0;
    }

    return (now_ms - start_ms) >= (uint64_t)auto_close_ms;
}

void croft_editor_scene_runtime_sync_bounds(croft_editor_scene_runtime_state* state,
                                            viewport_node* root,
                                            text_editor_node* editor,
                                            uint32_t framebuffer_width,
                                            uint32_t framebuffer_height,
                                            float window_padding)
{
    float content_width = (float)framebuffer_width - (window_padding * 2.0f);
    float content_height = (float)framebuffer_height - (window_padding * 2.0f);

    if (content_width < 0.0f) {
        content_width = 0.0f;
    }
    if (content_height < 0.0f) {
        content_height = 0.0f;
    }

    if (root) {
        root->base.sx = (float)framebuffer_width;
        root->base.sy = (float)framebuffer_height;
    }
    if (editor) {
        editor->base.sx = content_width;
        editor->base.sy = content_height;
    }

    if (!state) {
        return;
    }

    if (state->last_framebuffer_width != framebuffer_width
            || state->last_framebuffer_height != framebuffer_height) {
        state->last_framebuffer_width = framebuffer_width;
        state->last_framebuffer_height = framebuffer_height;
        state->needs_redraw = 1;
    }
}

void croft_editor_scene_runtime_sync_cursor_blink(croft_editor_scene_runtime_state* state,
                                                  const text_editor_node* editor,
                                                  uint64_t now_ms)
{
    int cursor_blink_visible = 1;

    if (!state || !editor) {
        return;
    }

    if (editor->sel_start == editor->sel_end) {
        cursor_blink_visible =
            (int)((now_ms / CROFT_EDITOR_CURSOR_BLINK_INTERVAL_MS) % 2u);
    }

    if (cursor_blink_visible != state->last_cursor_blink_visible) {
        state->last_cursor_blink_visible = cursor_blink_visible;
        state->needs_redraw = 1;
    }
}

int croft_editor_scene_runtime_needs_redraw(const croft_editor_scene_runtime_state* state)
{
    return state && state->needs_redraw;
}

void croft_editor_scene_runtime_note_frame_rendered(croft_editor_scene_runtime_state* state)
{
    if (!state) {
        return;
    }

    state->needs_redraw = 0;
}
