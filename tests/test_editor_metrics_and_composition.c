#include "croft/editor_document.h"
#include "croft/scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_EDITOR(cond)                                            \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "    ASSERT failed: %s (%s:%d)\n",         \
                    #cond, __FILE__, __LINE__);                        \
            return 1;                                                  \
        }                                                              \
    } while (0)

int test_editor_metrics_snapshot(void)
{
    const char* initial = "hello world\nwrapped line";
    croft_editor_document* document =
        croft_editor_document_create((const uint8_t*)initial, strlen(initial));
    text_editor_node editor = {0};
    croft_text_editor_metrics_snapshot metrics = {0};
    float x = 0.0f;
    float y = 0.0f;

    ASSERT_EDITOR(document != NULL);
    text_editor_node_init(&editor, NULL, 0.0f, 0.0f, 200.0f, 160.0f, NULL);
    text_editor_node_bind_document(&editor, document);
    text_editor_node_get_metrics(&editor, &metrics);

    ASSERT_EDITOR(metrics.font_size == editor.font_size);
    ASSERT_EDITOR(metrics.line_height >= metrics.font_line_height);
    ASSERT_EDITOR(metrics.baseline_offset > 0.0f);
    ASSERT_EDITOR(metrics.baseline_offset < metrics.line_height);
    ASSERT_EDITOR(metrics.text_inset_x == 12.0f);
    ASSERT_EDITOR(metrics.text_inset_y == 8.0f);
    ASSERT_EDITOR(text_editor_node_offset_to_local_position(&editor,
                                                            200.0f,
                                                            160.0f,
                                                            0u,
                                                            &x,
                                                            &y) == 0);
    ASSERT_EDITOR(y == metrics.text_inset_y);
    ASSERT_EDITOR(x >= metrics.text_inset_x);

    text_editor_node_dispose(&editor);
    croft_editor_document_destroy(document);
    return 0;
}

int test_editor_composition_state(void)
{
    const char* initial = "hello";
    croft_editor_document* document =
        croft_editor_document_create((const uint8_t*)initial, strlen(initial));
    text_editor_node editor = {0};
    char* composition = NULL;
    size_t composition_len = 0u;

    ASSERT_EDITOR(document != NULL);
    text_editor_node_init(&editor, NULL, 0.0f, 0.0f, 200.0f, 120.0f, NULL);
    text_editor_node_bind_document(&editor, document);

    ASSERT_EDITOR(text_editor_node_set_composition_utf8(&editor,
                                                        (const uint8_t*)"ni",
                                                        2u,
                                                        1u,
                                                        2u) == 0);
    ASSERT_EDITOR(text_editor_node_has_composition(&editor));
    ASSERT_EDITOR(text_editor_node_copy_composition_utf8(&editor,
                                                         &composition,
                                                         &composition_len) == 0);
    ASSERT_EDITOR(composition_len == 2u);
    ASSERT_EDITOR(strcmp(composition, "ni") == 0);
    free(composition);

    editor.base.vtbl->on_char_event(&editor.base, (uint32_t)'x');
    ASSERT_EDITOR(!text_editor_node_has_composition(&editor));

    ASSERT_EDITOR(text_editor_node_set_composition_utf8(&editor,
                                                        (const uint8_t*)"ko",
                                                        2u,
                                                        0u,
                                                        0u) == 0);
    text_editor_node_find_activate(&editor);
    ASSERT_EDITOR(!text_editor_node_has_composition(&editor));

    text_editor_node_dispose(&editor);
    croft_editor_document_destroy(document);
    return 0;
}
