#include "croft/editor_document.h"
#include "croft/scene.h"

#include <stdlib.h>
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

int test_editor_wrap_layout_affinity(void)
{
    const char* initial = "abcdefghij\nsecond line";
    croft_editor_document* document =
        croft_editor_document_create((const uint8_t*)initial, strlen(initial));
    text_editor_node editor = {0};
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x_leading = 0.0f;
    float y_leading = 0.0f;
    float x_trailing = 0.0f;
    float y_trailing = 0.0f;
    uint32_t wrap_offset = 0u;
    uint32_t hit_offset = 0u;
    uint32_t offset;
    croft_text_editor_caret_affinity hit_affinity = CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING;

    ASSERT_WRAP(document != NULL);
    text_editor_node_init(&editor, NULL, 0.0f, 0.0f, 100.0f, 220.0f, NULL);
    text_editor_node_bind_document(&editor, document);
    text_editor_node_set_wrap_enabled(&editor, 1);

    ASSERT_WRAP(text_editor_node_offset_to_local_position(&editor,
                                                          100.0f,
                                                          220.0f,
                                                          0u,
                                                          &x0,
                                                          &y0) == 0);
    for (offset = 1u; offset <= croft_editor_text_model_codepoint_length(&editor.text_model); offset++) {
        ASSERT_WRAP(text_editor_node_offset_to_local_position(&editor,
                                                              100.0f,
                                                              220.0f,
                                                              offset,
                                                              &x_leading,
                                                              &y_leading) == 0);
        if (y_leading > y0 + (editor.line_height * 0.5f)) {
            wrap_offset = offset;
            break;
        }
    }

    ASSERT_WRAP(wrap_offset > 0u);
    ASSERT_WRAP(text_editor_node_offset_to_local_position_with_affinity(
                    &editor,
                    100.0f,
                    220.0f,
                    wrap_offset,
                    CROFT_TEXT_EDITOR_CARET_AFFINITY_TRAILING,
                    &x_trailing,
                    &y_trailing) == 0);
    ASSERT_WRAP(y_leading > y_trailing + (editor.line_height * 0.5f));

    ASSERT_WRAP(text_editor_node_hit_test_offset_with_affinity(
                    &editor,
                    100.0f,
                    220.0f,
                    x_trailing - 1.0f,
                    y_trailing + (editor.line_height * 0.2f),
                    &hit_offset,
                    &hit_affinity) == 0);
    ASSERT_WRAP(hit_offset == wrap_offset);
    ASSERT_WRAP(hit_affinity == CROFT_TEXT_EDITOR_CARET_AFFINITY_TRAILING);

    ASSERT_WRAP(text_editor_node_hit_test_offset_with_affinity(
                    &editor,
                    100.0f,
                    220.0f,
                    x_leading + 1.0f,
                    y_leading + (editor.line_height * 0.2f),
                    &hit_offset,
                    &hit_affinity) == 0);
    ASSERT_WRAP(hit_offset == wrap_offset);
    ASSERT_WRAP(hit_affinity == CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING);

    text_editor_node_set_caret_affinity(&editor, CROFT_TEXT_EDITOR_CARET_AFFINITY_TRAILING);
    ASSERT_WRAP(text_editor_node_get_caret_affinity(&editor) == CROFT_TEXT_EDITOR_CARET_AFFINITY_TRAILING);

    text_editor_node_dispose(&editor);
    croft_editor_document_destroy(document);
    return 0;
}

int test_editor_double_click_word_selection(void)
{
    const char* initial = "alpha beta.gamma";
    croft_editor_document* document =
        croft_editor_document_create((const uint8_t*)initial, strlen(initial));
    text_editor_node editor = {0};
    char* selected = NULL;
    size_t selected_len = 0u;

    ASSERT_WRAP(document != NULL);
    text_editor_node_init(&editor, NULL, 0.0f, 0.0f, 240.0f, 220.0f, NULL);
    text_editor_node_bind_document(&editor, document);

    ASSERT_WRAP(text_editor_node_select_word_at_offset(&editor, 8u) == 0);
    ASSERT_WRAP(editor.sel_start == 6u);
    ASSERT_WRAP(editor.sel_end == 10u);
    ASSERT_WRAP(text_editor_node_copy_selection_utf8(&editor, &selected, &selected_len) == 0);
    ASSERT_WRAP(selected != NULL);
    ASSERT_WRAP(selected_len == 4u);
    ASSERT_WRAP(memcmp(selected, "beta", 4u) == 0);
    free(selected);
    selected = NULL;

    ASSERT_WRAP(text_editor_node_select_word_at_offset(&editor, 10u) == 0);
    ASSERT_WRAP(text_editor_node_copy_selection_utf8(&editor, &selected, &selected_len) == 0);
    ASSERT_WRAP(selected != NULL);
    ASSERT_WRAP(selected_len == 4u);
    ASSERT_WRAP(memcmp(selected, "beta", 4u) == 0);

    free(selected);
    text_editor_node_dispose(&editor);
    croft_editor_document_destroy(document);
    return 0;
}
