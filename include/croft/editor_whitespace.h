#ifndef CROFT_EDITOR_WHITESPACE_H
#define CROFT_EDITOR_WHITESPACE_H

#include "croft/editor_commands.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CROFT_EDITOR_VISIBLE_WHITESPACE_SPACE = 1,
    CROFT_EDITOR_VISIBLE_WHITESPACE_TAB = 2
} croft_editor_visible_whitespace_kind;

typedef struct croft_editor_visible_whitespace {
    uint32_t offset;
    uint32_t visual_column;
    uint32_t visual_width;
    croft_editor_visible_whitespace_kind kind;
} croft_editor_visible_whitespace;

typedef struct croft_editor_whitespace_line {
    uint32_t line_start_offset;
    uint32_t line_end_offset;
    uint32_t leading_indent_columns;
    uint32_t indent_guide_count;
} croft_editor_whitespace_line;

int32_t croft_editor_whitespace_describe_line(const croft_editor_text_model* model,
                                              uint32_t line_number,
                                              const croft_editor_tab_settings* settings,
                                              croft_editor_whitespace_line* out_line);

int32_t croft_editor_whitespace_find_in_line(const croft_editor_text_model* model,
                                             uint32_t line_number,
                                             const croft_editor_tab_settings* settings,
                                             uint32_t search_offset,
                                             croft_editor_visible_whitespace* out_marker);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_EDITOR_WHITESPACE_H */
