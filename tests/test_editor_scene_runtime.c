#include "croft/editor_scene_runtime.h"

#define ASSERT(cond)            \
    do {                        \
        if (!(cond)) {          \
            return 1;           \
        }                       \
    } while (0)

int test_editor_scene_runtime_bounds_invalidate(void)
{
    croft_editor_scene_runtime_state state;
    viewport_node root = {0};
    text_editor_node editor = {0};

    croft_editor_scene_runtime_state_init(&state);
    croft_editor_scene_runtime_note_frame_rendered(&state);

    croft_editor_scene_runtime_sync_bounds(&state, &root, &editor, 1000u, 800u, 16.0f);
    ASSERT(croft_editor_scene_runtime_needs_redraw(&state));
    ASSERT(root.base.sx == 1000.0f);
    ASSERT(root.base.sy == 800.0f);
    ASSERT(editor.base.sx == 968.0f);
    ASSERT(editor.base.sy == 768.0f);

    croft_editor_scene_runtime_note_frame_rendered(&state);
    croft_editor_scene_runtime_sync_bounds(&state, &root, &editor, 1000u, 800u, 16.0f);
    ASSERT(!croft_editor_scene_runtime_needs_redraw(&state));

    croft_editor_scene_runtime_sync_bounds(&state, &root, &editor, 900u, 700u, 16.0f);
    ASSERT(croft_editor_scene_runtime_needs_redraw(&state));
    return 0;
}

int test_editor_scene_runtime_cursor_blink_invalidate(void)
{
    croft_editor_scene_runtime_state state;
    text_editor_node editor = {0};

    croft_editor_scene_runtime_state_init(&state);
    croft_editor_scene_runtime_note_frame_rendered(&state);
    editor.sel_start = 12u;
    editor.sel_end = 12u;

    croft_editor_scene_runtime_sync_cursor_blink(&state, &editor, 0u);
    ASSERT(croft_editor_scene_runtime_needs_redraw(&state));

    croft_editor_scene_runtime_note_frame_rendered(&state);
    croft_editor_scene_runtime_sync_cursor_blink(&state, &editor, 200u);
    ASSERT(!croft_editor_scene_runtime_needs_redraw(&state));

    croft_editor_scene_runtime_sync_cursor_blink(&state, &editor, 500u);
    ASSERT(croft_editor_scene_runtime_needs_redraw(&state));

    croft_editor_scene_runtime_note_frame_rendered(&state);
    croft_editor_scene_runtime_sync_cursor_blink(&state, &editor, 1000u);
    ASSERT(croft_editor_scene_runtime_needs_redraw(&state));

    croft_editor_scene_runtime_note_frame_rendered(&state);
    editor.sel_end = 16u;
    croft_editor_scene_runtime_sync_cursor_blink(&state, &editor, 1000u);
    ASSERT(croft_editor_scene_runtime_needs_redraw(&state));
    return 0;
}

int test_editor_scene_runtime_auto_close(void)
{
    ASSERT(!croft_editor_scene_runtime_should_auto_close(100u, 450u, 400u));
    ASSERT(croft_editor_scene_runtime_should_auto_close(100u, 500u, 400u));
    ASSERT(!croft_editor_scene_runtime_should_auto_close(500u, 100u, 400u));
    ASSERT(!croft_editor_scene_runtime_should_auto_close(100u, 900u, 0u));
    return 0;
}
