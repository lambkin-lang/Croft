#ifndef CROFT_EDITOR_DOCUMENT_FS_H
#define CROFT_EDITOR_DOCUMENT_FS_H

#include "croft/editor_document.h"

#ifdef __cplusplus
extern "C" {
#endif

croft_editor_document* croft_editor_document_open(const char* exe_path,
                                                  const char* file_path,
                                                  const uint8_t* fallback_utf8,
                                                  size_t fallback_len);

int32_t croft_editor_document_save(croft_editor_document* document);

int32_t croft_editor_document_save_as(croft_editor_document* document,
                                      const char* path);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_EDITOR_DOCUMENT_FS_H */
