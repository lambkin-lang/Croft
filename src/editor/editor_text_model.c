#include "croft/editor_text_model.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static uint32_t croft_editor_clamp_u32(uint32_t value, uint32_t min_value, uint32_t max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int croft_editor_utf8_decode_one(const char* utf8,
                                        size_t remaining,
                                        uint32_t* codepoint_out,
                                        uint32_t* consumed_out) {
    uint32_t codepoint = 0;
    unsigned char b0;
    unsigned char b1;
    unsigned char b2;
    unsigned char b3;

    if (!utf8 || remaining == 0 || !codepoint_out || !consumed_out) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    b0 = (unsigned char)utf8[0];
    if (b0 < 0x80u) {
        *codepoint_out = (uint32_t)b0;
        *consumed_out = 1u;
        return CROFT_EDITOR_OK;
    }

    if ((b0 & 0xE0u) == 0xC0u) {
        if (remaining < 2u) {
            return CROFT_EDITOR_ERR_INVALID;
        }
        b1 = (unsigned char)utf8[1];
        if ((b1 & 0xC0u) != 0x80u) {
            return CROFT_EDITOR_ERR_INVALID;
        }
        codepoint = ((uint32_t)(b0 & 0x1Fu) << 6) | (uint32_t)(b1 & 0x3Fu);
        if (codepoint < 0x80u) {
            return CROFT_EDITOR_ERR_INVALID;
        }
        *codepoint_out = codepoint;
        *consumed_out = 2u;
        return CROFT_EDITOR_OK;
    }

    if ((b0 & 0xF0u) == 0xE0u) {
        if (remaining < 3u) {
            return CROFT_EDITOR_ERR_INVALID;
        }
        b1 = (unsigned char)utf8[1];
        b2 = (unsigned char)utf8[2];
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u) {
            return CROFT_EDITOR_ERR_INVALID;
        }
        codepoint = ((uint32_t)(b0 & 0x0Fu) << 12)
                  | ((uint32_t)(b1 & 0x3Fu) << 6)
                  | (uint32_t)(b2 & 0x3Fu);
        if (codepoint < 0x800u) {
            return CROFT_EDITOR_ERR_INVALID;
        }
        if (codepoint >= 0xD800u && codepoint <= 0xDFFFu) {
            return CROFT_EDITOR_ERR_INVALID;
        }
        *codepoint_out = codepoint;
        *consumed_out = 3u;
        return CROFT_EDITOR_OK;
    }

    if ((b0 & 0xF8u) == 0xF0u) {
        if (remaining < 4u) {
            return CROFT_EDITOR_ERR_INVALID;
        }
        b1 = (unsigned char)utf8[1];
        b2 = (unsigned char)utf8[2];
        b3 = (unsigned char)utf8[3];
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u || (b3 & 0xC0u) != 0x80u) {
            return CROFT_EDITOR_ERR_INVALID;
        }
        codepoint = ((uint32_t)(b0 & 0x07u) << 18)
                  | ((uint32_t)(b1 & 0x3Fu) << 12)
                  | ((uint32_t)(b2 & 0x3Fu) << 6)
                  | (uint32_t)(b3 & 0x3Fu);
        if (codepoint < 0x10000u || codepoint > 0x10FFFFu) {
            return CROFT_EDITOR_ERR_INVALID;
        }
        *codepoint_out = codepoint;
        *consumed_out = 4u;
        return CROFT_EDITOR_OK;
    }

    return CROFT_EDITOR_ERR_INVALID;
}

static uint32_t croft_editor_line_end_offset_0(const croft_editor_text_model* model,
                                               uint32_t line_index) {
    if (!model || model->line_count == 0) {
        return 0;
    }
    if (line_index + 1u < model->line_count) {
        return model->line_start_offsets[line_index + 1u] - 1u;
    }
    return model->codepoint_count;
}

static uint32_t croft_editor_find_line_index_for_offset(const croft_editor_text_model* model,
                                                        uint32_t offset) {
    uint32_t low = 0;
    uint32_t high;

    if (!model || model->line_count == 0) {
        return 0;
    }

    high = model->line_count;
    while (low + 1u < high) {
        uint32_t mid = low + ((high - low) / 2u);
        if (model->line_start_offsets[mid] <= offset) {
            low = mid;
        } else {
            high = mid;
        }
    }

    return low;
}

static int croft_editor_is_word_char(uint32_t codepoint, const char* extra_word_chars) {
    const char* separators = CROFT_EDITOR_WORD_SEPARATORS;

    if (codepoint <= 0x7Fu) {
        unsigned char ch = (unsigned char)codepoint;
        if (isspace(ch)) {
            return 0;
        }
        if (extra_word_chars && strchr(extra_word_chars, (int)ch)) {
            return 1;
        }
        if (strchr(separators, (int)ch)) {
            return 0;
        }
    }

    return 1;
}

static int croft_editor_codepoint_at_offset(const croft_editor_text_model* model,
                                            uint32_t offset,
                                            uint32_t* codepoint_out) {
    uint32_t byte_offset;
    uint32_t next_byte_offset;
    uint32_t consumed = 0;

    if (!model || !codepoint_out || offset >= model->codepoint_count) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    byte_offset = model->codepoint_to_byte_offsets[offset];
    next_byte_offset = model->codepoint_to_byte_offsets[offset + 1u];
    return croft_editor_utf8_decode_one(model->utf8 + byte_offset,
                                        (size_t)(next_byte_offset - byte_offset),
                                        codepoint_out,
                                        &consumed);
}

int32_t croft_editor_text_model_codepoint_at_offset(const croft_editor_text_model* model,
                                                    uint32_t offset,
                                                    uint32_t* out_codepoint) {
    return croft_editor_codepoint_at_offset(model, offset, out_codepoint);
}

void croft_editor_text_model_init(croft_editor_text_model* model) {
    if (!model) {
        return;
    }
    memset(model, 0, sizeof(*model));
}

void croft_editor_text_model_dispose(croft_editor_text_model* model) {
    if (!model) {
        return;
    }

    free(model->utf8);
    free(model->codepoint_to_byte_offsets);
    free(model->line_start_offsets);
    memset(model, 0, sizeof(*model));
}

int32_t croft_editor_text_model_set_text(croft_editor_text_model* model,
                                         const char* utf8,
                                         size_t utf8_len) {
    croft_editor_text_model next_model;
    size_t byte_offset = 0;
    uint32_t codepoint_count = 0;
    uint32_t line_count = 1;
    uint32_t line_index = 1;

    if (!model || (!utf8 && utf8_len > 0u)) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    croft_editor_text_model_init(&next_model);
    next_model.utf8 = (char*)malloc(utf8_len + 1u);
    if (!next_model.utf8) {
        return CROFT_EDITOR_ERR_OOM;
    }

    if (utf8_len > 0u) {
        memcpy(next_model.utf8, utf8, utf8_len);
    }
    next_model.utf8[utf8_len] = '\0';
    next_model.utf8_len = (uint32_t)utf8_len;

    while (byte_offset < utf8_len) {
        uint32_t codepoint = 0;
        uint32_t consumed = 0;
        if (croft_editor_utf8_decode_one(next_model.utf8 + byte_offset,
                                         utf8_len - byte_offset,
                                         &codepoint,
                                         &consumed) != CROFT_EDITOR_OK) {
            croft_editor_text_model_dispose(&next_model);
            return CROFT_EDITOR_ERR_INVALID;
        }
        if (codepoint == '\n') {
            line_count++;
        }
        codepoint_count++;
        byte_offset += consumed;
    }

    next_model.codepoint_to_byte_offsets =
        (uint32_t*)malloc(sizeof(uint32_t) * (codepoint_count + 1u));
    next_model.line_start_offsets = (uint32_t*)malloc(sizeof(uint32_t) * line_count);
    if (!next_model.codepoint_to_byte_offsets || !next_model.line_start_offsets) {
        croft_editor_text_model_dispose(&next_model);
        return CROFT_EDITOR_ERR_OOM;
    }

    next_model.codepoint_count = codepoint_count;
    next_model.line_count = line_count;
    next_model.line_start_offsets[0] = 0u;

    byte_offset = 0u;
    codepoint_count = 0u;
    while (byte_offset < utf8_len) {
        uint32_t codepoint = 0;
        uint32_t consumed = 0;
        next_model.codepoint_to_byte_offsets[codepoint_count] = (uint32_t)byte_offset;
        croft_editor_utf8_decode_one(next_model.utf8 + byte_offset,
                                     utf8_len - byte_offset,
                                     &codepoint,
                                     &consumed);
        byte_offset += consumed;
        if (codepoint == '\n') {
            next_model.line_start_offsets[line_index++] = codepoint_count + 1u;
        }
        codepoint_count++;
    }
    next_model.codepoint_to_byte_offsets[codepoint_count] = (uint32_t)utf8_len;

    croft_editor_text_model_dispose(model);
    *model = next_model;
    return CROFT_EDITOR_OK;
}

const char* croft_editor_text_model_text(const croft_editor_text_model* model) {
    if (!model || !model->utf8) {
        return "";
    }
    return model->utf8;
}

uint32_t croft_editor_text_model_length(const croft_editor_text_model* model) {
    if (!model) {
        return 0;
    }
    return model->utf8_len;
}

uint32_t croft_editor_text_model_codepoint_length(const croft_editor_text_model* model) {
    if (!model) {
        return 0;
    }
    return model->codepoint_count;
}

uint32_t croft_editor_text_model_line_count(const croft_editor_text_model* model) {
    if (!model || model->line_count == 0) {
        return 1;
    }
    return model->line_count;
}

uint32_t croft_editor_text_model_line_start_offset(const croft_editor_text_model* model,
                                                   uint32_t line_number) {
    uint32_t line_index;

    if (!model || model->line_count == 0) {
        return 0;
    }

    line_number = croft_editor_clamp_u32(line_number, 1u, model->line_count);
    line_index = line_number - 1u;
    return model->line_start_offsets[line_index];
}

uint32_t croft_editor_text_model_line_end_offset(const croft_editor_text_model* model,
                                                 uint32_t line_number) {
    uint32_t line_index;

    if (!model || model->line_count == 0) {
        return 0;
    }

    line_number = croft_editor_clamp_u32(line_number, 1u, model->line_count);
    line_index = line_number - 1u;
    return croft_editor_line_end_offset_0(model, line_index);
}

uint32_t croft_editor_text_model_line_length(const croft_editor_text_model* model,
                                             uint32_t line_number) {
    uint32_t line_start = croft_editor_text_model_line_start_offset(model, line_number);
    uint32_t line_end = croft_editor_text_model_line_end_offset(model, line_number);
    return line_end - line_start;
}

const char* croft_editor_text_model_line_utf8(const croft_editor_text_model* model,
                                              uint32_t line_number,
                                              uint32_t* out_len_bytes) {
    uint32_t line_start = croft_editor_text_model_line_start_offset(model, line_number);
    uint32_t line_end = croft_editor_text_model_line_end_offset(model, line_number);
    uint32_t byte_start = croft_editor_text_model_byte_offset_at(model, line_start);
    uint32_t byte_end = croft_editor_text_model_byte_offset_at(model, line_end);

    if (out_len_bytes) {
        *out_len_bytes = byte_end - byte_start;
    }
    if (!model || !model->utf8) {
        return "";
    }
    return model->utf8 + byte_start;
}

uint32_t croft_editor_text_model_get_offset_at(const croft_editor_text_model* model,
                                               uint32_t line_number,
                                               uint32_t column) {
    uint32_t line_start;
    uint32_t line_end;
    uint32_t line_length;
    uint32_t max_column;

    if (!model || model->line_count == 0) {
        return 0;
    }

    line_number = croft_editor_clamp_u32(line_number, 1u, model->line_count);
    line_start = croft_editor_text_model_line_start_offset(model, line_number);
    line_end = croft_editor_text_model_line_end_offset(model, line_number);
    line_length = line_end - line_start;
    max_column = line_length + 1u;
    column = croft_editor_clamp_u32(column, 1u, max_column);
    return line_start + (column - 1u);
}

uint32_t croft_editor_text_model_get_offset_for_position(const croft_editor_text_model* model,
                                                         croft_editor_position position) {
    return croft_editor_text_model_get_offset_at(model, position.line_number, position.column);
}

croft_editor_position croft_editor_text_model_get_position_at(const croft_editor_text_model* model,
                                                              uint32_t offset) {
    croft_editor_position position = {1u, 1u};
    uint32_t line_index;
    uint32_t line_start;

    if (!model || model->line_count == 0) {
        return position;
    }

    offset = croft_editor_clamp_u32(offset, 0u, model->codepoint_count);
    line_index = croft_editor_find_line_index_for_offset(model, offset);
    line_start = model->line_start_offsets[line_index];
    position.line_number = line_index + 1u;
    position.column = (offset - line_start) + 1u;
    return position;
}

uint32_t croft_editor_text_model_byte_offset_at(const croft_editor_text_model* model,
                                                uint32_t offset) {
    if (!model || !model->codepoint_to_byte_offsets) {
        return 0;
    }
    offset = croft_editor_clamp_u32(offset, 0u, model->codepoint_count);
    return model->codepoint_to_byte_offsets[offset];
}

int croft_editor_text_model_get_word_range_at(const croft_editor_text_model* model,
                                              croft_editor_position position,
                                              const char* extra_word_chars,
                                              croft_editor_range* out_range) {
    uint32_t line_start;
    uint32_t line_end;
    uint32_t offset;
    uint32_t probe;
    uint32_t codepoint = 0;

    if (!model || !out_range) {
        return 0;
    }

    offset = croft_editor_text_model_get_offset_for_position(model, position);
    line_start = croft_editor_text_model_line_start_offset(model, position.line_number);
    line_end = croft_editor_text_model_line_end_offset(model, position.line_number);

    if (offset < line_end
            && croft_editor_codepoint_at_offset(model, offset, &codepoint) == CROFT_EDITOR_OK
            && croft_editor_is_word_char(codepoint, extra_word_chars)) {
        probe = offset;
    } else if (offset > line_start
            && croft_editor_codepoint_at_offset(model, offset - 1u, &codepoint) == CROFT_EDITOR_OK
            && croft_editor_is_word_char(codepoint, extra_word_chars)) {
        probe = offset - 1u;
    } else {
        return 0;
    }

    while (probe > line_start) {
        if (croft_editor_codepoint_at_offset(model, probe - 1u, &codepoint) != CROFT_EDITOR_OK
                || !croft_editor_is_word_char(codepoint, extra_word_chars)) {
            break;
        }
        probe--;
    }
    line_start = probe;

    probe++;
    while (probe < line_end) {
        if (croft_editor_codepoint_at_offset(model, probe, &codepoint) != CROFT_EDITOR_OK
                || !croft_editor_is_word_char(codepoint, extra_word_chars)) {
            break;
        }
        probe++;
    }
    line_end = probe;

    *out_range = croft_editor_range_create(
        croft_editor_text_model_get_position_at(model, line_start),
        croft_editor_text_model_get_position_at(model, line_end)
    );
    return 1;
}

croft_editor_position croft_editor_position_create(uint32_t line_number, uint32_t column) {
    croft_editor_position position;
    position.line_number = line_number == 0u ? 1u : line_number;
    position.column = column == 0u ? 1u : column;
    return position;
}

int croft_editor_position_equals(croft_editor_position a, croft_editor_position b) {
    return a.line_number == b.line_number && a.column == b.column;
}

int croft_editor_position_compare(croft_editor_position a, croft_editor_position b) {
    if (a.line_number < b.line_number) {
        return -1;
    }
    if (a.line_number > b.line_number) {
        return 1;
    }
    if (a.column < b.column) {
        return -1;
    }
    if (a.column > b.column) {
        return 1;
    }
    return 0;
}

croft_editor_range croft_editor_range_create(croft_editor_position start,
                                             croft_editor_position end) {
    croft_editor_range range;

    if (croft_editor_position_compare(start, end) <= 0) {
        range.start_line_number = start.line_number;
        range.start_column = start.column;
        range.end_line_number = end.line_number;
        range.end_column = end.column;
    } else {
        range.start_line_number = end.line_number;
        range.start_column = end.column;
        range.end_line_number = start.line_number;
        range.end_column = start.column;
    }

    return range;
}

int croft_editor_range_is_empty(croft_editor_range range) {
    return range.start_line_number == range.end_line_number
        && range.start_column == range.end_column;
}

int croft_editor_range_contains_position(croft_editor_range range, croft_editor_position position) {
    croft_editor_position start =
        croft_editor_position_create(range.start_line_number, range.start_column);
    croft_editor_position end =
        croft_editor_position_create(range.end_line_number, range.end_column);
    return croft_editor_position_compare(start, position) <= 0
        && croft_editor_position_compare(position, end) <= 0;
}

croft_editor_selection croft_editor_selection_create(croft_editor_position selection_start,
                                                     croft_editor_position position) {
    croft_editor_selection selection;
    selection.selection_start_line_number = selection_start.line_number;
    selection.selection_start_column = selection_start.column;
    selection.position_line_number = position.line_number;
    selection.position_column = position.column;
    return selection;
}

croft_editor_selection croft_editor_selection_from_offsets(const croft_editor_text_model* model,
                                                           uint32_t anchor_offset,
                                                           uint32_t active_offset) {
    return croft_editor_selection_create(
        croft_editor_text_model_get_position_at(model, anchor_offset),
        croft_editor_text_model_get_position_at(model, active_offset)
    );
}

croft_editor_selection_direction croft_editor_selection_direction_of(croft_editor_selection selection) {
    croft_editor_position anchor = croft_editor_position_create(
        selection.selection_start_line_number,
        selection.selection_start_column
    );
    croft_editor_position active = croft_editor_position_create(
        selection.position_line_number,
        selection.position_column
    );
    return croft_editor_position_compare(anchor, active) <= 0
        ? CROFT_EDITOR_SELECTION_LTR
        : CROFT_EDITOR_SELECTION_RTL;
}

croft_editor_range croft_editor_selection_normalized_range(croft_editor_selection selection) {
    return croft_editor_range_create(
        croft_editor_position_create(selection.selection_start_line_number,
                                     selection.selection_start_column),
        croft_editor_position_create(selection.position_line_number,
                                     selection.position_column)
    );
}
