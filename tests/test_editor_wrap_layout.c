#include "croft/editor_document.h"
#include "croft/scene.h"

#include <stdio.h>
#include <string.h>

#define ASSERT_WRAP(cond)                                               \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "    ASSERT failed: %s (%s:%d)\n",          \
                    #cond, __FILE__, __LINE__);                         \
            return 1;                                                   \
        }                                                               \
    } while (0)

enum {
    TEST_CROFT_KEY_PRESS = 1,
    TEST_CROFT_KEY_DOWN = 264
};

int test_editor_wrap_layout_geometry(void)
{
    const char* initial = "abcdefghij\nsecond line";
    croft_editor_document* document =
        croft_editor_document_create((const uint8_t*)initial, strlen(initial));
    text_editor_node editor = {0};
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x8 = 0.0f;
    float y8 = 0.0f;
    uint32_t hit_offset = 0u;
    uint32_t wrapped_count;
    uint32_t unwrapped_count;
    croft_editor_position moved_position;

    ASSERT_WRAP(document != NULL);
    text_editor_node_init(&editor, NULL, 0.0f, 0.0f, 100.0f, 220.0f, NULL);
    text_editor_node_bind_document(&editor, document);
    text_editor_node_set_wrap_enabled(&editor, 1);

    wrapped_count = text_editor_node_visible_line_count_for_bounds(&editor, 100.0f, 220.0f);
    ASSERT_WRAP(wrapped_count > 2u);
    ASSERT_WRAP(text_editor_node_offset_to_local_position(&editor, 100.0f, 220.0f, 0u, &x0, &y0) == 0);
    ASSERT_WRAP(text_editor_node_offset_to_local_position(&editor, 100.0f, 220.0f, 8u, &x8, &y8) == 0);
    ASSERT_WRAP(y8 > y0 + (editor.line_height * 0.5f));
    ASSERT_WRAP(text_editor_node_hit_test_offset(&editor,
                                                 100.0f,
                                                 220.0f,
                                                 x8 + 1.0f,
                                                 y8 + (editor.line_height * 0.2f),
                                                 &hit_offset) == 0);
    ASSERT_WRAP(hit_offset == 8u);

    editor.sel_start = 2u;
    editor.sel_end = 2u;
    editor.selection = croft_editor_selection_from_offsets(&editor.text_model, 2u, 2u);
    editor.preferred_column = 0u;
    editor.preferred_visual_x = 0.0f;
    editor.base.vtbl->on_key_event(&editor.base, TEST_CROFT_KEY_DOWN, TEST_CROFT_KEY_PRESS);
    moved_position = croft_editor_text_model_get_position_at(&editor.text_model, editor.sel_end);
    ASSERT_WRAP(moved_position.line_number == 1u);
    ASSERT_WRAP(editor.sel_end > 2u);

    text_editor_node_set_wrap_enabled(&editor, 0);
    unwrapped_count = text_editor_node_visible_line_count_for_bounds(&editor, 100.0f, 220.0f);
    ASSERT_WRAP(unwrapped_count == 2u);

    text_editor_node_dispose(&editor);
    croft_editor_document_destroy(document);
    return 0;
}
