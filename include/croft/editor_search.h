#ifndef CROFT_EDITOR_SEARCH_H
#define CROFT_EDITOR_SEARCH_H

#include "croft/editor_text_model.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_editor_search_match {
    uint32_t start_offset;
    uint32_t end_offset;
} croft_editor_search_match;

int32_t croft_editor_search_next(const croft_editor_text_model* model,
                                 const char* needle_utf8,
                                 size_t needle_utf8_len,
                                 uint32_t start_offset,
                                 croft_editor_search_match* out_match);

int32_t croft_editor_search_previous(const croft_editor_text_model* model,
                                     const char* needle_utf8,
                                     size_t needle_utf8_len,
                                     uint32_t before_offset,
                                     croft_editor_search_match* out_match);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_EDITOR_SEARCH_H */
