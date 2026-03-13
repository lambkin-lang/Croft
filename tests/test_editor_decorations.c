#include "croft/editor_document.h"
#include "croft/scene.h"

#include <stdio.h>
#include <string.h>

#define ASSERT_DECORATION(cond)                                        \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "    ASSERT failed: %s (%s:%d)\n",         \
                    #cond, __FILE__, __LINE__);                        \
            return 1;                                                  \
        }                                                              \
    } while (0)

int test_editor_decorations_normalize(void)
{
    const char* initial = "hello world";
    croft_editor_document* document =
        croft_editor_document_create((const uint8_t*)initial, strlen(initial));
    text_editor_node editor = {0};
    croft_text_editor_decoration decorations[3] = {
        {.start_offset = 5u,
         .end_offset = 2u,
         .color_rgba = 0x11223344u,
         .style = CROFT_TEXT_EDITOR_DECORATION_STYLE_BACKGROUND},
        {.start_offset = 6u,
         .end_offset = 6u,
         .color_rgba = 0x55667788u,
         .style = CROFT_TEXT_EDITOR_DECORATION_STYLE_UNDERLINE},
        {.start_offset = 8u,
         .end_offset = 32u,
         .color_rgba = 0x99AABBCCu,
         .style = CROFT_TEXT_EDITOR_DECORATION_STYLE_UNDERLINE}
    };
    croft_text_editor_decoration decoration = {0};

    ASSERT_DECORATION(document != NULL);
    text_editor_node_init(&editor, NULL, 0.0f, 0.0f, 200.0f, 120.0f, NULL);
    text_editor_node_bind_document(&editor, document);

    ASSERT_DECORATION(text_editor_node_set_decorations(&editor, decorations, 3u) == 0);
    ASSERT_DECORATION(text_editor_node_decoration_count(&editor) == 2u);
    ASSERT_DECORATION(text_editor_node_get_decoration(&editor, 0u, &decoration) == 0);
    ASSERT_DECORATION(decoration.start_offset == 2u);
    ASSERT_DECORATION(decoration.end_offset == 5u);
    ASSERT_DECORATION(decoration.style == CROFT_TEXT_EDITOR_DECORATION_STYLE_BACKGROUND);
    ASSERT_DECORATION(decoration.color_rgba == 0x11223344u);
    ASSERT_DECORATION(text_editor_node_get_decoration(&editor, 1u, &decoration) == 0);
    ASSERT_DECORATION(decoration.start_offset == 8u);
    ASSERT_DECORATION(decoration.end_offset == 11u);
    ASSERT_DECORATION(decoration.style == CROFT_TEXT_EDITOR_DECORATION_STYLE_UNDERLINE);
    ASSERT_DECORATION(decoration.color_rgba == 0x99AABBCCu);

    text_editor_node_clear_decorations(&editor);
    ASSERT_DECORATION(text_editor_node_decoration_count(&editor) == 0u);

    text_editor_node_dispose(&editor);
    croft_editor_document_destroy(document);
    return 0;
}

int test_editor_decorations_clamp_after_edit(void)
{
    const char* initial = "hello world";
    croft_editor_document* document =
        croft_editor_document_create((const uint8_t*)initial, strlen(initial));
    text_editor_node editor = {0};
    croft_text_editor_decoration decorations[2] = {
        {.start_offset = 0u,
         .end_offset = 5u,
         .color_rgba = 0x22446688u,
         .style = CROFT_TEXT_EDITOR_DECORATION_STYLE_BACKGROUND},
        {.start_offset = 8u,
         .end_offset = 11u,
         .color_rgba = 0xCC4422FFu,
         .style = CROFT_TEXT_EDITOR_DECORATION_STYLE_UNDERLINE}
    };
    croft_text_editor_decoration decoration = {0};

    ASSERT_DECORATION(document != NULL);
    text_editor_node_init(&editor, NULL, 0.0f, 0.0f, 200.0f, 120.0f, NULL);
    text_editor_node_bind_document(&editor, document);

    ASSERT_DECORATION(text_editor_node_set_decorations(&editor, decorations, 2u) == 0);
    text_editor_node_select_all(&editor);
    ASSERT_DECORATION(text_editor_node_replace_selection_utf8(&editor,
                                                              (const uint8_t*)"hi",
                                                              2u) == 0);
    ASSERT_DECORATION(text_editor_node_decoration_count(&editor) == 1u);
    ASSERT_DECORATION(text_editor_node_get_decoration(&editor, 0u, &decoration) == 0);
    ASSERT_DECORATION(decoration.start_offset == 0u);
    ASSERT_DECORATION(decoration.end_offset == 2u);
    ASSERT_DECORATION(decoration.style == CROFT_TEXT_EDITOR_DECORATION_STYLE_BACKGROUND);

    text_editor_node_dispose(&editor);
    croft_editor_document_destroy(document);
    return 0;
}
