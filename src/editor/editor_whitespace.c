#include "croft/editor_whitespace.h"

static uint32_t croft_editor_whitespace_tab_size(const croft_editor_tab_settings* settings) {
    if (!settings || settings->tab_size == 0u) {
        return 4u;
    }
    return settings->tab_size;
}

static int croft_editor_whitespace_is_marker_codepoint(uint32_t codepoint,
                                                       croft_editor_visible_whitespace_kind* out_kind) {
    if (codepoint == ' ') {
        if (out_kind) {
            *out_kind = CROFT_EDITOR_VISIBLE_WHITESPACE_SPACE;
        }
        return 1;
    }
    if (codepoint == '\t') {
        if (out_kind) {
            *out_kind = CROFT_EDITOR_VISIBLE_WHITESPACE_TAB;
        }
        return 1;
    }
    return 0;
}

static uint32_t croft_editor_whitespace_visual_width_for_codepoint(uint32_t visual_column,
                                                                   uint32_t codepoint,
                                                                   uint32_t tab_size) {
    if (codepoint == '\t') {
        return tab_size - ((visual_column - 1u) % tab_size);
    }
    return 1u;
}

int32_t croft_editor_whitespace_describe_line(const croft_editor_text_model* model,
                                              uint32_t line_number,
                                              const croft_editor_tab_settings* settings,
                                              croft_editor_whitespace_line* out_line) {
    uint32_t tab_size;
    uint32_t offset;
    uint32_t visual_column = 1u;
    uint32_t leading_indent_columns = 0u;
    int leading = 1;

    if (!model || !out_line) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    out_line->line_start_offset = croft_editor_text_model_line_start_offset(model, line_number);
    out_line->line_end_offset = croft_editor_text_model_line_end_offset(model, line_number);
    out_line->leading_indent_columns = 0u;
    out_line->indent_guide_count = 0u;

    tab_size = croft_editor_whitespace_tab_size(settings);
    for (offset = out_line->line_start_offset; offset < out_line->line_end_offset; offset++) {
        uint32_t codepoint = 0u;
        croft_editor_visible_whitespace_kind kind;
        uint32_t visual_width;

        if (croft_editor_text_model_codepoint_at_offset(model, offset, &codepoint) != CROFT_EDITOR_OK) {
            return CROFT_EDITOR_ERR_INVALID;
        }

        visual_width = croft_editor_whitespace_visual_width_for_codepoint(visual_column,
                                                                          codepoint,
                                                                          tab_size);
        if (leading && croft_editor_whitespace_is_marker_codepoint(codepoint, &kind)) {
            leading_indent_columns += visual_width;
        } else {
            leading = 0;
        }
        visual_column += visual_width;
    }

    out_line->leading_indent_columns = leading_indent_columns;
    out_line->indent_guide_count = leading_indent_columns / tab_size;
    return CROFT_EDITOR_OK;
}

int32_t croft_editor_whitespace_find_in_line(const croft_editor_text_model* model,
                                             uint32_t line_number,
                                             const croft_editor_tab_settings* settings,
                                             uint32_t search_offset,
                                             croft_editor_visible_whitespace* out_marker) {
    uint32_t tab_size;
    uint32_t line_start_offset;
    uint32_t line_end_offset;
    uint32_t offset;
    uint32_t visual_column = 1u;

    if (!model || !out_marker) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    line_start_offset = croft_editor_text_model_line_start_offset(model, line_number);
    line_end_offset = croft_editor_text_model_line_end_offset(model, line_number);
    if (search_offset < line_start_offset) {
        search_offset = line_start_offset;
    }
    if (search_offset > line_end_offset) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    tab_size = croft_editor_whitespace_tab_size(settings);
    for (offset = line_start_offset; offset < line_end_offset; offset++) {
        uint32_t codepoint = 0u;
        croft_editor_visible_whitespace_kind kind;
        uint32_t visual_width;

        if (croft_editor_text_model_codepoint_at_offset(model, offset, &codepoint) != CROFT_EDITOR_OK) {
            return CROFT_EDITOR_ERR_INVALID;
        }

        visual_width = croft_editor_whitespace_visual_width_for_codepoint(visual_column,
                                                                          codepoint,
                                                                          tab_size);
        if (offset >= search_offset && croft_editor_whitespace_is_marker_codepoint(codepoint, &kind)) {
            out_marker->offset = offset;
            out_marker->visual_column = visual_column;
            out_marker->visual_width = visual_width;
            out_marker->kind = kind;
            return CROFT_EDITOR_OK;
        }
        visual_column += visual_width;
    }

    return CROFT_EDITOR_ERR_INVALID;
}
