#include "croft/editor_document.h"
#include "croft/scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_FIND_REPLACE(cond)                                        \
    do {                                                                 \
        if (!(cond)) {                                                   \
            fprintf(stderr, "    ASSERT failed: %s (%s:%d)\n",           \
                    #cond, __FILE__, __LINE__);                          \
            return 1;                                                    \
        }                                                                \
    } while (0)

static int document_matches(croft_editor_document* document, const char* expected)
{
    char* utf8 = NULL;
    size_t utf8_len = 0u;
    int matches = 0;

    if (croft_editor_document_export_utf8(document, &utf8, &utf8_len) == 0
            && utf8
            && utf8_len == strlen(expected)
            && memcmp(utf8, expected, utf8_len) == 0) {
        matches = 1;
    }

    free(utf8);
    return matches;
}

int test_editor_find_replace_next(void)
{
    const char* initial_text = "alpha beta alpha gamma alpha";
    croft_editor_document* document =
        croft_editor_document_create((const uint8_t*)initial_text, strlen(initial_text));
    text_editor_node editor = {0};

    ASSERT_FIND_REPLACE(document != NULL);
    text_editor_node_init(&editor, NULL, 0.0f, 0.0f, 800.0f, 600.0f, NULL);
    text_editor_node_bind_document(&editor, document);

    ASSERT_FIND_REPLACE(text_editor_node_set_find_query_utf8(&editor, "alpha", 5u) == 0);
    ASSERT_FIND_REPLACE(text_editor_node_set_replace_query_utf8(&editor, "omega", 5u) == 0);
    text_editor_node_replace_activate(&editor);
    ASSERT_FIND_REPLACE(text_editor_node_is_replace_active(&editor));
    ASSERT_FIND_REPLACE(text_editor_node_replace_next(&editor) == 0);
    ASSERT_FIND_REPLACE(document_matches(document, "omega beta alpha gamma alpha"));
    ASSERT_FIND_REPLACE(editor.sel_start == 11u);
    ASSERT_FIND_REPLACE(editor.sel_end == 16u);
    ASSERT_FIND_REPLACE(text_editor_node_undo(&editor) == 0);
    ASSERT_FIND_REPLACE(document_matches(document, initial_text));

    text_editor_node_dispose(&editor);
    croft_editor_document_destroy(document);
    return 0;
}

int test_editor_find_replace_all_undo(void)
{
    const char* initial_text = "one two one";
    croft_editor_document* document =
        croft_editor_document_create((const uint8_t*)initial_text, strlen(initial_text));
    text_editor_node editor = {0};

    ASSERT_FIND_REPLACE(document != NULL);
    text_editor_node_init(&editor, NULL, 0.0f, 0.0f, 800.0f, 600.0f, NULL);
    text_editor_node_bind_document(&editor, document);

    ASSERT_FIND_REPLACE(text_editor_node_set_find_query_utf8(&editor, "one", 3u) == 0);
    ASSERT_FIND_REPLACE(text_editor_node_set_replace_query_utf8(&editor, "three", 5u) == 0);
    ASSERT_FIND_REPLACE(text_editor_node_replace_all(&editor) == 0);
    ASSERT_FIND_REPLACE(document_matches(document, "three two three"));
    ASSERT_FIND_REPLACE(croft_editor_document_can_undo(document));
    ASSERT_FIND_REPLACE(editor.sel_start == 0u);
    ASSERT_FIND_REPLACE(editor.sel_end == 5u);
    ASSERT_FIND_REPLACE(text_editor_node_undo(&editor) == 0);
    ASSERT_FIND_REPLACE(document_matches(document, initial_text));

    text_editor_node_dispose(&editor);
    croft_editor_document_destroy(document);
    return 0;
}
