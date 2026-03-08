#ifndef CROFT_EDITOR_BRACKETS_H
#define CROFT_EDITOR_BRACKETS_H

#include "croft/editor_text_model.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_editor_bracket_match {
    uint32_t open_offset;
    uint32_t close_offset;
} croft_editor_bracket_match;

int32_t croft_editor_bracket_match_at_offset(const croft_editor_text_model* model,
                                             uint32_t bracket_offset,
                                             croft_editor_bracket_match* out_match);

int32_t croft_editor_bracket_match_near_offset(const croft_editor_text_model* model,
                                               uint32_t cursor_offset,
                                               croft_editor_bracket_match* out_match);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_EDITOR_BRACKETS_H */
