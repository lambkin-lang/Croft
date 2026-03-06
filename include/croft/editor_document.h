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

croft_editor_document* croft_editor_document_create(const char* exe_path,
                                                    const char* file_path,
                                                    const uint8_t* fallback_utf8,
                                                    size_t fallback_len);

void croft_editor_document_destroy(croft_editor_document* document);

int32_t croft_editor_document_replace_utf8(croft_editor_document* document,
                                           const uint8_t* utf8,
                                           size_t utf8_len);

int32_t croft_editor_document_export_utf8(croft_editor_document* document,
                                          char** out_utf8,
                                          size_t* out_len);

int32_t croft_editor_document_save(croft_editor_document* document);

const char* croft_editor_document_path(const croft_editor_document* document);

int croft_editor_document_is_dirty(const croft_editor_document* document);

void croft_editor_document_mark_clean(croft_editor_document* document);

struct SapEnv* croft_editor_document_env(croft_editor_document* document);

struct Text* croft_editor_document_text(croft_editor_document* document);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_EDITOR_DOCUMENT_H */
