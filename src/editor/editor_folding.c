#include "croft/editor_folding.h"

typedef struct {
    uint32_t indent_columns;
    int is_blank;
} croft_editor_fold_line_info;

static uint32_t croft_editor_folding_tab_size(const croft_editor_tab_settings* settings) {
    if (!settings || settings->tab_size == 0u) {
        return 4u;
    }
    return settings->tab_size;
}

static int32_t croft_editor_folding_describe_line(const croft_editor_text_model* model,
                                                  uint32_t line_number,
                                                  uint32_t tab_size,
                                                  croft_editor_fold_line_info* out_info) {
    uint32_t start_offset;
    uint32_t end_offset;
    uint32_t offset;
    uint32_t indent_columns = 0u;
    int saw_non_whitespace = 0;

    if (!model || !out_info) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    start_offset = croft_editor_text_model_line_start_offset(model, line_number);
    end_offset = croft_editor_text_model_line_end_offset(model, line_number);
    for (offset = start_offset; offset < end_offset; offset++) {
        uint32_t codepoint = 0u;

        if (croft_editor_text_model_codepoint_at_offset(model, offset, &codepoint) != CROFT_EDITOR_OK) {
            return CROFT_EDITOR_ERR_INVALID;
        }
        if (!saw_non_whitespace && codepoint == ' ') {
            indent_columns += 1u;
            continue;
        }
        if (!saw_non_whitespace && codepoint == '\t') {
            indent_columns += tab_size - (indent_columns % tab_size);
            continue;
        }
        if (codepoint != ' ' && codepoint != '\t') {
            saw_non_whitespace = 1;
            break;
        }
    }

    out_info->indent_columns = indent_columns;
    out_info->is_blank = saw_non_whitespace ? 0 : (start_offset == end_offset || offset == end_offset);
    return CROFT_EDITOR_OK;
}

int32_t croft_editor_fold_region_for_line(const croft_editor_text_model* model,
                                          uint32_t line_number,
                                          const croft_editor_tab_settings* settings,
                                          croft_editor_fold_region* out_region) {
    uint32_t line_count;
    uint32_t tab_size;
    croft_editor_fold_line_info header = {0};
    croft_editor_fold_line_info body = {0};
    uint32_t body_line_number = 0u;
    uint32_t end_line_number = 0u;
    uint32_t probe_line;

    if (!model || !out_region) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    line_count = croft_editor_text_model_line_count(model);
    if (line_count == 0u || line_number == 0u || line_number > line_count) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    tab_size = croft_editor_folding_tab_size(settings);
    if (croft_editor_folding_describe_line(model, line_number, tab_size, &header) != CROFT_EDITOR_OK
            || header.is_blank) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    for (probe_line = line_number + 1u; probe_line <= line_count; probe_line++) {
        if (croft_editor_folding_describe_line(model, probe_line, tab_size, &body) != CROFT_EDITOR_OK) {
            return CROFT_EDITOR_ERR_INVALID;
        }
        if (body.is_blank) {
            continue;
        }
        if (body.indent_columns <= header.indent_columns) {
            return CROFT_EDITOR_ERR_INVALID;
        }

        body_line_number = probe_line;
        end_line_number = probe_line;
        break;
    }

    if (body_line_number == 0u) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    for (probe_line = body_line_number + 1u; probe_line <= line_count; probe_line++) {
        croft_editor_fold_line_info probe_info = {0};

        if (croft_editor_folding_describe_line(model, probe_line, tab_size, &probe_info) != CROFT_EDITOR_OK) {
            return CROFT_EDITOR_ERR_INVALID;
        }
        if (probe_info.is_blank) {
            end_line_number = probe_line;
            continue;
        }
        if (probe_info.indent_columns <= header.indent_columns) {
            break;
        }
        end_line_number = probe_line;
    }

    out_region->start_line_number = line_number;
    out_region->end_line_number = end_line_number;
    out_region->start_offset = croft_editor_text_model_line_start_offset(model, line_number);
    out_region->body_start_offset = croft_editor_text_model_line_start_offset(model, body_line_number);
    out_region->end_offset = croft_editor_text_model_line_end_offset(model, end_line_number);
    out_region->header_indent_columns = header.indent_columns;
    out_region->body_indent_columns = body.indent_columns;
    return CROFT_EDITOR_OK;
}
