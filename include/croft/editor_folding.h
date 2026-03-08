#ifndef CROFT_EDITOR_FOLDING_H
#define CROFT_EDITOR_FOLDING_H

#include "croft/editor_commands.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_editor_fold_region {
    uint32_t start_line_number;
    uint32_t end_line_number;
    uint32_t start_offset;
    uint32_t body_start_offset;
    uint32_t end_offset;
    uint32_t header_indent_columns;
    uint32_t body_indent_columns;
} croft_editor_fold_region;

int32_t croft_editor_fold_region_for_line(const croft_editor_text_model* model,
                                          uint32_t line_number,
                                          const croft_editor_tab_settings* settings,
                                          croft_editor_fold_region* out_region);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_EDITOR_FOLDING_H */
