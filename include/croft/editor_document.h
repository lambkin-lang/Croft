#ifndef CROFT_EDITOR_DOCUMENT_H
#define CROFT_EDITOR_DOCUMENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_editor_document croft_editor_document;
struct SapEnv;
struct Text;

typedef enum croft_editor_document_edit_kind {
    CROFT_EDITOR_EDIT_INSERT = 1,
    CROFT_EDITOR_EDIT_DELETE_BACKWARD = 2,
    CROFT_EDITOR_EDIT_DELETE_FORWARD = 3,
    CROFT_EDITOR_EDIT_REPLACE_ALL = 4
} croft_editor_document_edit_kind;

croft_editor_document* croft_editor_document_create(const uint8_t* initial_utf8,
                                                    size_t initial_len);

void croft_editor_document_destroy(croft_editor_document* document);

int32_t croft_editor_document_set_path(croft_editor_document* document,
                                       const char* path);

int32_t croft_editor_document_replace_utf8(croft_editor_document* document,
                                           const uint8_t* utf8,
                                           size_t utf8_len);

int32_t croft_editor_document_replace_range_with_codepoint(
    croft_editor_document* document,
    size_t start_offset,
    size_t end_offset,
    uint32_t codepoint,
    croft_editor_document_edit_kind edit_kind);

int32_t croft_editor_document_delete_range(croft_editor_document* document,
                                           size_t start_offset,
                                           size_t end_offset,
                                           croft_editor_document_edit_kind edit_kind);

int32_t croft_editor_document_export_utf8(croft_editor_document* document,
                                          char** out_utf8,
                                          size_t* out_len);

const char* croft_editor_document_path(const croft_editor_document* document);

int croft_editor_document_is_dirty(const croft_editor_document* document);
int croft_editor_document_can_undo(const croft_editor_document* document);
int croft_editor_document_can_redo(const croft_editor_document* document);

void croft_editor_document_mark_clean(croft_editor_document* document);
void croft_editor_document_break_coalescing(croft_editor_document* document);

int32_t croft_editor_document_undo(croft_editor_document* document);
int32_t croft_editor_document_redo(croft_editor_document* document);

struct SapEnv* croft_editor_document_env(croft_editor_document* document);

struct Text* croft_editor_document_text(croft_editor_document* document);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_EDITOR_DOCUMENT_H */
