#include "croft/editor_document.h"
#include "croft/editor_document_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_DOCUMENT(cond)                                            \
    do {                                                                 \
        if (!(cond)) {                                                   \
            fprintf(stderr, "    ASSERT failed: %s (%s:%d)\n",           \
                    #cond, __FILE__, __LINE__);                          \
            return 1;                                                    \
        }                                                                \
    } while (0)

static int export_matches(croft_editor_document* document, const char* expected) {
    char* utf8 = NULL;
    size_t utf8_len = 0u;
    size_t expected_len = strlen(expected);
    int32_t rc = croft_editor_document_export_utf8(document, &utf8, &utf8_len);
    int matches = 0;

    if (rc == 0 && utf8 && utf8_len == expected_len && memcmp(utf8, expected, expected_len) == 0) {
        matches = 1;
    }

    free(utf8);
    return matches;
}

int test_editor_document_undo_redo_coalesced_insert(void) {
    croft_editor_document* document = croft_editor_document_create((const uint8_t*)"abc", 3u);

    ASSERT_DOCUMENT(document != NULL);
    ASSERT_DOCUMENT(croft_editor_document_is_dirty(document) == 0);
    ASSERT_DOCUMENT(croft_editor_document_replace_range_with_codepoint(
                        document,
                        3u,
                        3u,
                        'd',
                        CROFT_EDITOR_EDIT_INSERT) == 0);
    ASSERT_DOCUMENT(croft_editor_document_replace_range_with_codepoint(
                        document,
                        4u,
                        4u,
                        'e',
                        CROFT_EDITOR_EDIT_INSERT) == 0);
    ASSERT_DOCUMENT(export_matches(document, "abcde"));
    ASSERT_DOCUMENT(croft_editor_document_can_undo(document));
    ASSERT_DOCUMENT(!croft_editor_document_can_redo(document));
    ASSERT_DOCUMENT(croft_editor_document_is_dirty(document));

    ASSERT_DOCUMENT(croft_editor_document_undo(document) == 0);
    ASSERT_DOCUMENT(export_matches(document, "abc"));
    ASSERT_DOCUMENT(!croft_editor_document_is_dirty(document));
    ASSERT_DOCUMENT(croft_editor_document_can_redo(document));

    ASSERT_DOCUMENT(croft_editor_document_redo(document) == 0);
    ASSERT_DOCUMENT(export_matches(document, "abcde"));
    ASSERT_DOCUMENT(croft_editor_document_is_dirty(document));

    croft_editor_document_destroy(document);
    return 0;
}

int test_editor_document_coalescing_barrier(void) {
    croft_editor_document* document = croft_editor_document_create((const uint8_t*)"abc", 3u);

    ASSERT_DOCUMENT(document != NULL);
    ASSERT_DOCUMENT(croft_editor_document_replace_range_with_codepoint(
                        document,
                        3u,
                        3u,
                        'd',
                        CROFT_EDITOR_EDIT_INSERT) == 0);
    croft_editor_document_break_coalescing(document);
    ASSERT_DOCUMENT(croft_editor_document_replace_range_with_codepoint(
                        document,
                        4u,
                        4u,
                        'e',
                        CROFT_EDITOR_EDIT_INSERT) == 0);
    ASSERT_DOCUMENT(export_matches(document, "abcde"));

    ASSERT_DOCUMENT(croft_editor_document_undo(document) == 0);
    ASSERT_DOCUMENT(export_matches(document, "abcd"));
    ASSERT_DOCUMENT(croft_editor_document_undo(document) == 0);
    ASSERT_DOCUMENT(export_matches(document, "abc"));

    croft_editor_document_destroy(document);
    return 0;
}

int test_editor_document_delete_coalescing_and_redo_invalidation(void) {
    croft_editor_document* document = croft_editor_document_create((const uint8_t*)"abcdef", 6u);

    ASSERT_DOCUMENT(document != NULL);
    ASSERT_DOCUMENT(croft_editor_document_delete_range(
                        document,
                        5u,
                        6u,
                        CROFT_EDITOR_EDIT_DELETE_BACKWARD) == 0);
    ASSERT_DOCUMENT(croft_editor_document_delete_range(
                        document,
                        4u,
                        5u,
                        CROFT_EDITOR_EDIT_DELETE_BACKWARD) == 0);
    ASSERT_DOCUMENT(export_matches(document, "abcd"));

    ASSERT_DOCUMENT(croft_editor_document_undo(document) == 0);
    ASSERT_DOCUMENT(export_matches(document, "abcdef"));
    ASSERT_DOCUMENT(croft_editor_document_redo(document) == 0);
    ASSERT_DOCUMENT(export_matches(document, "abcd"));
    ASSERT_DOCUMENT(croft_editor_document_undo(document) == 0);
    ASSERT_DOCUMENT(export_matches(document, "abcdef"));

    ASSERT_DOCUMENT(croft_editor_document_replace_range_with_codepoint(
                        document,
                        6u,
                        6u,
                        'X',
                        CROFT_EDITOR_EDIT_INSERT) == 0);
    ASSERT_DOCUMENT(export_matches(document, "abcdefX"));
    ASSERT_DOCUMENT(!croft_editor_document_can_redo(document));

    croft_editor_document_destroy(document);
    return 0;
}

int test_editor_document_fs_open_save_roundtrip(void) {
    const char* path = "test_editor_document_fs.txt";
    const char* seed = "seed";
    FILE* file = fopen(path, "wb");
    croft_editor_document* document;

    ASSERT_DOCUMENT(file != NULL);
    ASSERT_DOCUMENT(fwrite(seed, 1u, strlen(seed), file) == strlen(seed));
    fclose(file);

    document = croft_editor_document_open(NULL,
                                          path,
                                          (const uint8_t*)"fallback",
                                          strlen("fallback"));
    ASSERT_DOCUMENT(document != NULL);
    ASSERT_DOCUMENT(export_matches(document, "seed"));
    ASSERT_DOCUMENT(croft_editor_document_path(document) != NULL);
    ASSERT_DOCUMENT(strcmp(croft_editor_document_path(document), path) == 0);

    ASSERT_DOCUMENT(croft_editor_document_replace_range_with_codepoint(
                        document,
                        4u,
                        4u,
                        '!',
                        CROFT_EDITOR_EDIT_INSERT) == 0);
    ASSERT_DOCUMENT(croft_editor_document_save(document) == 0);
    croft_editor_document_destroy(document);

    document = croft_editor_document_open(NULL, path, NULL, 0u);
    ASSERT_DOCUMENT(document != NULL);
    ASSERT_DOCUMENT(export_matches(document, "seed!"));
    croft_editor_document_destroy(document);

    remove(path);
    return 0;
}
