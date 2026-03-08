#include "croft/editor_commands.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    CROFT_EDITOR_WORD_CLASS_WHITESPACE = 0,
    CROFT_EDITOR_WORD_CLASS_REGULAR = 1,
    CROFT_EDITOR_WORD_CLASS_SEPARATOR = 2
} croft_editor_word_class;

typedef struct {
    uint32_t line_start_offset;
    uint32_t removed_prefix_codepoints;
    uint32_t inserted_prefix_codepoints;
} croft_editor_indented_line;

static uint32_t croft_editor_commands_tab_size(const croft_editor_tab_settings* settings) {
    if (!settings || settings->tab_size == 0u) {
        return 4u;
    }
    return settings->tab_size;
}

static int croft_editor_commands_insert_spaces(const croft_editor_tab_settings* settings) {
    if (!settings) {
        return 1;
    }
    return settings->insert_spaces ? 1 : 0;
}

static int croft_editor_commands_append_utf8(char** buffer,
                                             size_t* length,
                                             size_t* capacity,
                                             const char* utf8,
                                             size_t utf8_len) {
    char* next_buffer;
    size_t next_capacity;

    if (!buffer || !length || !capacity || (!utf8 && utf8_len > 0u)) {
        return 0;
    }
    if (*length + utf8_len + 1u <= *capacity) {
        if (utf8_len > 0u) {
            memcpy(*buffer + *length, utf8, utf8_len);
        }
        *length += utf8_len;
        (*buffer)[*length] = '\0';
        return 1;
    }

    next_capacity = *capacity > 0u ? *capacity : 32u;
    while (next_capacity < *length + utf8_len + 1u) {
        next_capacity *= 2u;
    }

    next_buffer = (char*)realloc(*buffer, next_capacity);
    if (!next_buffer) {
        return 0;
    }

    *buffer = next_buffer;
    *capacity = next_capacity;
    if (utf8_len > 0u) {
        memcpy(*buffer + *length, utf8, utf8_len);
    }
    *length += utf8_len;
    (*buffer)[*length] = '\0';
    return 1;
}

static int croft_editor_commands_append_spaces(char** buffer,
                                               size_t* length,
                                               size_t* capacity,
                                               uint32_t count) {
    while (count > 0u) {
        if (!croft_editor_commands_append_utf8(buffer, length, capacity, " ", 1u)) {
            return 0;
        }
        count--;
    }
    return 1;
}

static uint32_t croft_editor_commands_outdent_prefix_codepoints(const croft_editor_text_model* model,
                                                                uint32_t line_number,
                                                                uint32_t tab_size) {
    const char* line_utf8;
    uint32_t line_byte_length = 0u;
    uint32_t removed = 0u;

    line_utf8 = croft_editor_text_model_line_utf8(model, line_number, &line_byte_length);
    if (!line_utf8) {
        return 0u;
    }

    if (line_byte_length > 0u && line_utf8[0] == '\t') {
        return 1u;
    }

    while (removed < line_byte_length
            && removed < tab_size
            && line_utf8[removed] == ' ') {
        removed++;
    }
    return removed;
}

static uint32_t croft_editor_commands_map_block_offset(const croft_editor_indented_line* lines,
                                                       size_t line_count,
                                                       uint32_t original_offset,
                                                       int outdent) {
    int64_t delta = 0;
    size_t i;

    if (!lines) {
        return original_offset;
    }

    for (i = 0u; i < line_count; i++) {
        uint32_t line_start = lines[i].line_start_offset;

        if (original_offset < line_start) {
            break;
        }
        if (original_offset == line_start) {
            break;
        }

        if (!outdent) {
            delta += (int64_t)lines[i].inserted_prefix_codepoints;
            continue;
        }

        if (original_offset <= line_start + lines[i].removed_prefix_codepoints) {
            delta -= (int64_t)(original_offset - line_start);
            return (uint32_t)((int64_t)original_offset + delta);
        }

        delta -= (int64_t)lines[i].removed_prefix_codepoints;
    }

    return (uint32_t)((int64_t)original_offset + delta);
}

static uint32_t croft_editor_commands_clamp_offset(const croft_editor_text_model* model,
                                                   uint32_t offset) {
    uint32_t max_offset = croft_editor_text_model_codepoint_length(model);
    if (offset > max_offset) {
        return max_offset;
    }
    return offset;
}

static void croft_editor_commands_normalize(uint32_t a,
                                            uint32_t b,
                                            uint32_t* out_start,
                                            uint32_t* out_end) {
    if (a <= b) {
        *out_start = a;
        *out_end = b;
    } else {
        *out_start = b;
        *out_end = a;
    }
}

static void croft_editor_commands_apply_move(uint32_t* anchor_offset,
                                             uint32_t* active_offset,
                                             uint32_t new_active_offset,
                                             uint32_t* preferred_column,
                                             int selecting,
                                             int preserve_preferred_column) {
    if (selecting) {
        *active_offset = new_active_offset;
    } else {
        *anchor_offset = new_active_offset;
        *active_offset = new_active_offset;
    }
    if (preferred_column && !preserve_preferred_column) {
        *preferred_column = 0u;
    }
}

static croft_editor_word_class croft_editor_commands_classify_codepoint(uint32_t codepoint) {
    if (codepoint <= 0x7Fu) {
        unsigned char ch = (unsigned char)codepoint;
        if (isspace(ch)) {
            return CROFT_EDITOR_WORD_CLASS_WHITESPACE;
        }
        if (strchr(CROFT_EDITOR_WORD_SEPARATORS, (int)ch)) {
            return CROFT_EDITOR_WORD_CLASS_SEPARATOR;
        }
    }
    return CROFT_EDITOR_WORD_CLASS_REGULAR;
}

static croft_editor_word_class croft_editor_commands_classify_offset(const croft_editor_text_model* model,
                                                                     uint32_t offset) {
    uint32_t codepoint = 0;
    if (croft_editor_text_model_codepoint_at_offset(model, offset, &codepoint) != CROFT_EDITOR_OK) {
        return CROFT_EDITOR_WORD_CLASS_WHITESPACE;
    }
    return croft_editor_commands_classify_codepoint(codepoint);
}

static int croft_editor_commands_is_ascii_lower(uint32_t codepoint) {
    return codepoint >= 'a' && codepoint <= 'z';
}

static int croft_editor_commands_is_ascii_upper(uint32_t codepoint) {
    return codepoint >= 'A' && codepoint <= 'Z';
}

static int croft_editor_commands_is_ascii_digit(uint32_t codepoint) {
    return codepoint >= '0' && codepoint <= '9';
}

static int croft_editor_commands_is_word_part_hump_start(uint32_t left_codepoint,
                                                         uint32_t right_codepoint) {
    return (croft_editor_commands_is_ascii_lower(left_codepoint)
            || croft_editor_commands_is_ascii_digit(left_codepoint))
        && croft_editor_commands_is_ascii_upper(right_codepoint);
}

static int croft_editor_commands_boundary_codepoints(const croft_editor_text_model* model,
                                                     uint32_t boundary,
                                                     uint32_t* out_left_codepoint,
                                                     uint32_t* out_right_codepoint) {
    uint32_t max_offset = croft_editor_text_model_codepoint_length(model);

    if (!out_left_codepoint || !out_right_codepoint || boundary == 0u || boundary >= max_offset) {
        return 0;
    }
    if (croft_editor_text_model_codepoint_at_offset(model,
                                                    boundary - 1u,
                                                    out_left_codepoint) != CROFT_EDITOR_OK) {
        return 0;
    }
    if (croft_editor_text_model_codepoint_at_offset(model,
                                                    boundary,
                                                    out_right_codepoint) != CROFT_EDITOR_OK) {
        return 0;
    }
    return 1;
}

static int croft_editor_commands_find_previous_run(const croft_editor_text_model* model,
                                                   uint32_t position,
                                                   uint32_t* out_start,
                                                   uint32_t* out_end,
                                                   croft_editor_word_class* out_class) {
    uint32_t probe = croft_editor_commands_clamp_offset(model, position);

    while (probe > 0u
            && croft_editor_commands_classify_offset(model, probe - 1u)
                == CROFT_EDITOR_WORD_CLASS_WHITESPACE) {
        probe--;
    }
    if (probe == 0u) {
        return 0;
    }

    *out_end = probe;
    *out_class = croft_editor_commands_classify_offset(model, probe - 1u);
    while (probe > 0u
            && croft_editor_commands_classify_offset(model, probe - 1u) == *out_class) {
        probe--;
    }
    *out_start = probe;
    return 1;
}

static int croft_editor_commands_find_next_run(const croft_editor_text_model* model,
                                               uint32_t position,
                                               uint32_t* out_start,
                                               uint32_t* out_end,
                                               croft_editor_word_class* out_class) {
    uint32_t probe = croft_editor_commands_clamp_offset(model, position);
    uint32_t max_offset = croft_editor_text_model_codepoint_length(model);

    while (probe < max_offset
            && croft_editor_commands_classify_offset(model, probe)
                == CROFT_EDITOR_WORD_CLASS_WHITESPACE) {
        probe++;
    }
    if (probe >= max_offset) {
        return 0;
    }

    *out_start = probe;
    *out_class = croft_editor_commands_classify_offset(model, probe);
    while (probe < max_offset
            && croft_editor_commands_classify_offset(model, probe) == *out_class) {
        probe++;
    }
    *out_end = probe;
    return 1;
}

static int croft_editor_commands_is_separator_bridge(const croft_editor_text_model* model,
                                                     uint32_t run_start,
                                                     uint32_t run_end) {
    uint32_t max_offset = croft_editor_text_model_codepoint_length(model);

    return (run_end - run_start) == 1u
        && run_start > 0u
        && run_end < max_offset
        && croft_editor_commands_classify_offset(model, run_start - 1u)
            == CROFT_EDITOR_WORD_CLASS_REGULAR
        && croft_editor_commands_classify_offset(model, run_end)
            == CROFT_EDITOR_WORD_CLASS_REGULAR;
}

static uint32_t croft_editor_commands_move_word_left_target(const croft_editor_text_model* model,
                                                            uint32_t position) {
    uint32_t run_start = 0;
    uint32_t run_end = 0;
    croft_editor_word_class run_class = CROFT_EDITOR_WORD_CLASS_WHITESPACE;

    if (!croft_editor_commands_find_previous_run(model, position, &run_start, &run_end, &run_class)) {
        return 0u;
    }

    if (run_class == CROFT_EDITOR_WORD_CLASS_SEPARATOR
            && croft_editor_commands_is_separator_bridge(model, run_start, run_end)
            && croft_editor_commands_find_previous_run(model,
                                                       run_start,
                                                       &run_start,
                                                       &run_end,
                                                       &run_class)) {
        return run_start;
    }

    return run_start;
}

static uint32_t croft_editor_commands_move_word_left_end_target(const croft_editor_text_model* model,
                                                                uint32_t position) {
    uint32_t run_start = 0;
    uint32_t run_end = 0;
    croft_editor_word_class run_class = CROFT_EDITOR_WORD_CLASS_WHITESPACE;

    if (!croft_editor_commands_find_previous_run(model, position, &run_start, &run_end, &run_class)) {
        return 0u;
    }

    if (run_end == position
            && !croft_editor_commands_find_previous_run(model,
                                                        run_start,
                                                        &run_start,
                                                        &run_end,
                                                        &run_class)) {
        return 0u;
    }

    if (run_class == CROFT_EDITOR_WORD_CLASS_SEPARATOR
            && croft_editor_commands_is_separator_bridge(model, run_start, run_end)
            && croft_editor_commands_find_previous_run(model,
                                                       run_start,
                                                       &run_start,
                                                       &run_end,
                                                       &run_class)) {
        return run_end;
    }

    return run_end;
}

static uint32_t croft_editor_commands_move_word_right_target(const croft_editor_text_model* model,
                                                             uint32_t position) {
    uint32_t run_start = 0;
    uint32_t run_end = 0;
    croft_editor_word_class run_class = CROFT_EDITOR_WORD_CLASS_WHITESPACE;
    uint32_t next_start = 0;
    uint32_t next_end = 0;
    croft_editor_word_class next_class = CROFT_EDITOR_WORD_CLASS_WHITESPACE;

    if (!croft_editor_commands_find_next_run(model, position, &run_start, &run_end, &run_class)) {
        return croft_editor_text_model_codepoint_length(model);
    }

    if (run_class == CROFT_EDITOR_WORD_CLASS_SEPARATOR
            && croft_editor_commands_is_separator_bridge(model, run_start, run_end)
            && croft_editor_commands_find_next_run(model, run_end, &next_start, &next_end, &next_class)
            && next_class == CROFT_EDITOR_WORD_CLASS_REGULAR) {
        return next_end;
    }

    return run_end;
}

static uint32_t croft_editor_commands_move_word_right_start_target(const croft_editor_text_model* model,
                                                                   uint32_t position) {
    uint32_t run_start = 0;
    uint32_t run_end = 0;
    croft_editor_word_class run_class = CROFT_EDITOR_WORD_CLASS_WHITESPACE;
    uint32_t next_start = 0;
    uint32_t next_end = 0;
    croft_editor_word_class next_class = CROFT_EDITOR_WORD_CLASS_WHITESPACE;
    uint32_t max_offset = croft_editor_text_model_codepoint_length(model);

    if (!croft_editor_commands_find_next_run(model, position, &run_start, &run_end, &run_class)) {
        return max_offset;
    }

    if (run_start <= position
            && position < run_end
            && !croft_editor_commands_find_next_run(model,
                                                    run_end,
                                                    &run_start,
                                                    &run_end,
                                                    &run_class)) {
        return max_offset;
    }

    if (run_class == CROFT_EDITOR_WORD_CLASS_SEPARATOR
            && croft_editor_commands_is_separator_bridge(model, run_start, run_end)
            && croft_editor_commands_find_next_run(model,
                                                   run_end,
                                                   &next_start,
                                                   &next_end,
                                                   &next_class)
            && next_class == CROFT_EDITOR_WORD_CLASS_REGULAR) {
        return next_start;
    }

    return run_start;
}

static uint32_t croft_editor_commands_move_word_part_left_segment_target(
    const croft_editor_text_model* model,
    uint32_t position) {
    croft_editor_position current_position =
        croft_editor_text_model_get_position_at(model, position);
    uint32_t line_start =
        croft_editor_text_model_line_start_offset(model, current_position.line_number);
    uint32_t boundary;

    if (position == line_start) {
        if (current_position.line_number > 1u) {
            return croft_editor_text_model_line_end_offset(model, current_position.line_number - 1u);
        }
        return 0u;
    }

    for (boundary = position - 1u; boundary > line_start; boundary--) {
        uint32_t left_codepoint = 0;
        uint32_t right_codepoint = 0;

        if (!croft_editor_commands_boundary_codepoints(model,
                                                       boundary,
                                                       &left_codepoint,
                                                       &right_codepoint)) {
            continue;
        }

        if (left_codepoint == '_' && right_codepoint != '_') {
            return boundary;
        }
        if (left_codepoint == '-' && right_codepoint != '-') {
            return boundary;
        }
        if (croft_editor_commands_is_word_part_hump_start(left_codepoint, right_codepoint)) {
            return boundary;
        }
        if (croft_editor_commands_is_ascii_upper(left_codepoint)
                && croft_editor_commands_is_ascii_upper(right_codepoint)
                && boundary + 1u < croft_editor_text_model_line_end_offset(model,
                                                                            current_position.line_number)) {
            uint32_t next_codepoint = 0;
            if (croft_editor_text_model_codepoint_at_offset(model,
                                                            boundary + 1u,
                                                            &next_codepoint) == CROFT_EDITOR_OK
                    && (croft_editor_commands_is_ascii_lower(next_codepoint)
                        || croft_editor_commands_is_ascii_digit(next_codepoint))) {
                return boundary;
            }
        }
    }

    return line_start;
}

static uint32_t croft_editor_commands_move_word_part_right_segment_target(
    const croft_editor_text_model* model,
    uint32_t position) {
    croft_editor_position current_position =
        croft_editor_text_model_get_position_at(model, position);
    uint32_t line_end =
        croft_editor_text_model_line_end_offset(model, current_position.line_number);
    uint32_t line_count = croft_editor_text_model_line_count(model);
    uint32_t boundary;

    if (position == line_end) {
        if (current_position.line_number < line_count) {
            return croft_editor_text_model_line_start_offset(model, current_position.line_number + 1u);
        }
        return croft_editor_text_model_codepoint_length(model);
    }

    for (boundary = position + 1u; boundary < line_end; boundary++) {
        uint32_t left_codepoint = 0;
        uint32_t right_codepoint = 0;

        if (!croft_editor_commands_boundary_codepoints(model,
                                                       boundary,
                                                       &left_codepoint,
                                                       &right_codepoint)) {
            continue;
        }

        if (left_codepoint != '_' && right_codepoint == '_') {
            return boundary;
        }
        if (left_codepoint != '-' && right_codepoint == '-') {
            return boundary;
        }
        if (croft_editor_commands_is_word_part_hump_start(left_codepoint, right_codepoint)) {
            return boundary;
        }
        if (croft_editor_commands_is_ascii_upper(left_codepoint)
                && croft_editor_commands_is_ascii_upper(right_codepoint)
                && boundary + 1u < line_end) {
            uint32_t next_codepoint = 0;
            if (croft_editor_text_model_codepoint_at_offset(model,
                                                            boundary + 1u,
                                                            &next_codepoint) == CROFT_EDITOR_OK
                    && (croft_editor_commands_is_ascii_lower(next_codepoint)
                        || croft_editor_commands_is_ascii_digit(next_codepoint))) {
                return boundary;
            }
        }
    }

    return line_end;
}

static uint32_t croft_editor_commands_move_word_part_left_target(const croft_editor_text_model* model,
                                                                 uint32_t position) {
    uint32_t word_start_target = croft_editor_commands_move_word_left_target(model, position);
    uint32_t word_end_target = croft_editor_commands_move_word_left_end_target(model, position);
    uint32_t segment_target =
        croft_editor_commands_move_word_part_left_segment_target(model, position);
    uint32_t target = word_start_target;

    if (word_end_target > target) {
        target = word_end_target;
    }
    if (segment_target > target) {
        target = segment_target;
    }
    return target;
}

static uint32_t croft_editor_commands_move_word_part_right_target(const croft_editor_text_model* model,
                                                                  uint32_t position) {
    uint32_t word_start_target =
        croft_editor_commands_move_word_right_start_target(model, position);
    uint32_t word_end_target = croft_editor_commands_move_word_right_target(model, position);
    uint32_t segment_target =
        croft_editor_commands_move_word_part_right_segment_target(model, position);
    uint32_t target = word_start_target;

    if (word_end_target < target) {
        target = word_end_target;
    }
    if (segment_target < target) {
        target = segment_target;
    }
    return target;
}

static int croft_editor_commands_delete_selection_or_range(uint32_t anchor_offset,
                                                           uint32_t active_offset,
                                                           uint32_t move_target,
                                                           int move_left,
                                                           uint32_t* out_start_offset,
                                                           uint32_t* out_end_offset) {
    uint32_t range_start = 0;
    uint32_t range_end = 0;

    croft_editor_commands_normalize(anchor_offset, active_offset, &range_start, &range_end);
    if (range_start != range_end) {
        *out_start_offset = range_start;
        *out_end_offset = range_end;
        return 1;
    }

    if (move_target == active_offset) {
        return 0;
    }

    if (move_left) {
        *out_start_offset = move_target;
        *out_end_offset = active_offset;
    } else {
        *out_start_offset = active_offset;
        *out_end_offset = move_target;
    }
    return 1;
}

void croft_editor_tab_settings_default(croft_editor_tab_settings* settings) {
    if (!settings) {
        return;
    }
    settings->tab_size = 4u;
    settings->insert_spaces = 1;
}

void croft_editor_tab_edit_dispose(croft_editor_tab_edit* edit) {
    if (!edit) {
        return;
    }
    free(edit->replacement_utf8);
    memset(edit, 0, sizeof(*edit));
}

void croft_editor_command_move_left(const croft_editor_text_model* model,
                                    uint32_t* anchor_offset,
                                    uint32_t* active_offset,
                                    uint32_t* preferred_column,
                                    int selecting) {
    uint32_t start = 0;
    uint32_t end = 0;

    (void)model;
    croft_editor_commands_normalize(*anchor_offset, *active_offset, &start, &end);
    if (!selecting && start != end) {
        croft_editor_commands_apply_move(anchor_offset, active_offset, start, preferred_column, 0, 0);
        return;
    }

    if (*active_offset > 0u) {
        croft_editor_commands_apply_move(anchor_offset,
                                         active_offset,
                                         *active_offset - 1u,
                                         preferred_column,
                                         selecting,
                                         0);
    }
}

void croft_editor_command_move_right(const croft_editor_text_model* model,
                                     uint32_t* anchor_offset,
                                     uint32_t* active_offset,
                                     uint32_t* preferred_column,
                                     int selecting) {
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t max_offset = croft_editor_text_model_codepoint_length(model);

    croft_editor_commands_normalize(*anchor_offset, *active_offset, &start, &end);
    if (!selecting && start != end) {
        croft_editor_commands_apply_move(anchor_offset, active_offset, end, preferred_column, 0, 0);
        return;
    }

    if (*active_offset < max_offset) {
        croft_editor_commands_apply_move(anchor_offset,
                                         active_offset,
                                         *active_offset + 1u,
                                         preferred_column,
                                         selecting,
                                         0);
    }
}

void croft_editor_command_move_up(const croft_editor_text_model* model,
                                  uint32_t* anchor_offset,
                                  uint32_t* active_offset,
                                  uint32_t* preferred_column,
                                  int selecting) {
    croft_editor_position current_position =
        croft_editor_text_model_get_position_at(model, *active_offset);
    uint32_t desired_column;
    uint32_t target_line;
    uint32_t target_offset;

    if (preferred_column && *preferred_column == 0u) {
        *preferred_column = current_position.column;
    }
    desired_column = preferred_column ? *preferred_column : current_position.column;
    target_line = current_position.line_number > 1u ? current_position.line_number - 1u : 1u;
    target_offset = croft_editor_text_model_get_offset_at(model, target_line, desired_column);
    croft_editor_commands_apply_move(anchor_offset,
                                     active_offset,
                                     target_offset,
                                     preferred_column,
                                     selecting,
                                     1);
}

void croft_editor_command_move_down(const croft_editor_text_model* model,
                                    uint32_t* anchor_offset,
                                    uint32_t* active_offset,
                                    uint32_t* preferred_column,
                                    int selecting) {
    croft_editor_position current_position =
        croft_editor_text_model_get_position_at(model, *active_offset);
    uint32_t desired_column;
    uint32_t line_count = croft_editor_text_model_line_count(model);
    uint32_t target_line;
    uint32_t target_offset;

    if (preferred_column && *preferred_column == 0u) {
        *preferred_column = current_position.column;
    }
    desired_column = preferred_column ? *preferred_column : current_position.column;
    target_line = current_position.line_number < line_count
        ? current_position.line_number + 1u
        : line_count;
    target_offset = croft_editor_text_model_get_offset_at(model, target_line, desired_column);
    croft_editor_commands_apply_move(anchor_offset,
                                     active_offset,
                                     target_offset,
                                     preferred_column,
                                     selecting,
                                     1);
}

void croft_editor_command_move_home(const croft_editor_text_model* model,
                                    uint32_t* anchor_offset,
                                    uint32_t* active_offset,
                                    uint32_t* preferred_column,
                                    int selecting) {
    croft_editor_position current_position =
        croft_editor_text_model_get_position_at(model, *active_offset);
    uint32_t target_offset =
        croft_editor_text_model_line_start_offset(model, current_position.line_number);
    croft_editor_commands_apply_move(anchor_offset,
                                     active_offset,
                                     target_offset,
                                     preferred_column,
                                     selecting,
                                     0);
}

void croft_editor_command_move_end(const croft_editor_text_model* model,
                                   uint32_t* anchor_offset,
                                   uint32_t* active_offset,
                                   uint32_t* preferred_column,
                                   int selecting) {
    croft_editor_position current_position =
        croft_editor_text_model_get_position_at(model, *active_offset);
    uint32_t target_offset =
        croft_editor_text_model_line_end_offset(model, current_position.line_number);
    croft_editor_commands_apply_move(anchor_offset,
                                     active_offset,
                                     target_offset,
                                     preferred_column,
                                     selecting,
                                     0);
}

void croft_editor_command_move_word_left(const croft_editor_text_model* model,
                                         uint32_t* anchor_offset,
                                         uint32_t* active_offset,
                                         uint32_t* preferred_column,
                                         int selecting) {
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t target_offset;

    croft_editor_commands_normalize(*anchor_offset, *active_offset, &start, &end);
    if (!selecting && start != end) {
        croft_editor_commands_apply_move(anchor_offset, active_offset, start, preferred_column, 0, 0);
        return;
    }

    target_offset = croft_editor_commands_move_word_left_target(model, *active_offset);
    croft_editor_commands_apply_move(anchor_offset,
                                     active_offset,
                                     target_offset,
                                     preferred_column,
                                     selecting,
                                     0);
}

void croft_editor_command_move_word_right(const croft_editor_text_model* model,
                                          uint32_t* anchor_offset,
                                          uint32_t* active_offset,
                                          uint32_t* preferred_column,
                                          int selecting) {
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t target_offset;

    croft_editor_commands_normalize(*anchor_offset, *active_offset, &start, &end);
    if (!selecting && start != end) {
        croft_editor_commands_apply_move(anchor_offset, active_offset, end, preferred_column, 0, 0);
        return;
    }

    target_offset = croft_editor_commands_move_word_right_target(model, *active_offset);
    croft_editor_commands_apply_move(anchor_offset,
                                     active_offset,
                                     target_offset,
                                     preferred_column,
                                     selecting,
                                     0);
}

void croft_editor_command_move_word_part_left(const croft_editor_text_model* model,
                                              uint32_t* anchor_offset,
                                              uint32_t* active_offset,
                                              uint32_t* preferred_column,
                                              int selecting) {
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t target_offset;

    croft_editor_commands_normalize(*anchor_offset, *active_offset, &start, &end);
    if (!selecting && start != end) {
        croft_editor_commands_apply_move(anchor_offset, active_offset, start, preferred_column, 0, 0);
        return;
    }

    target_offset = croft_editor_commands_move_word_part_left_target(model, *active_offset);
    croft_editor_commands_apply_move(anchor_offset,
                                     active_offset,
                                     target_offset,
                                     preferred_column,
                                     selecting,
                                     0);
}

void croft_editor_command_move_word_part_right(const croft_editor_text_model* model,
                                               uint32_t* anchor_offset,
                                               uint32_t* active_offset,
                                               uint32_t* preferred_column,
                                               int selecting) {
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t target_offset;

    croft_editor_commands_normalize(*anchor_offset, *active_offset, &start, &end);
    if (!selecting && start != end) {
        croft_editor_commands_apply_move(anchor_offset, active_offset, end, preferred_column, 0, 0);
        return;
    }

    target_offset = croft_editor_commands_move_word_part_right_target(model, *active_offset);
    croft_editor_commands_apply_move(anchor_offset,
                                     active_offset,
                                     target_offset,
                                     preferred_column,
                                     selecting,
                                     0);
}

int croft_editor_command_delete_left_range(const croft_editor_text_model* model,
                                           uint32_t anchor_offset,
                                           uint32_t active_offset,
                                           uint32_t* out_start_offset,
                                           uint32_t* out_end_offset) {
    uint32_t move_target = active_offset > 0u ? active_offset - 1u : 0u;
    (void)model;
    return croft_editor_commands_delete_selection_or_range(anchor_offset,
                                                           active_offset,
                                                           move_target,
                                                           1,
                                                           out_start_offset,
                                                           out_end_offset);
}

int croft_editor_command_delete_right_range(const croft_editor_text_model* model,
                                            uint32_t anchor_offset,
                                            uint32_t active_offset,
                                            uint32_t* out_start_offset,
                                            uint32_t* out_end_offset) {
    uint32_t max_offset = croft_editor_text_model_codepoint_length(model);
    uint32_t move_target = active_offset < max_offset ? active_offset + 1u : max_offset;
    return croft_editor_commands_delete_selection_or_range(anchor_offset,
                                                           active_offset,
                                                           move_target,
                                                           0,
                                                           out_start_offset,
                                                           out_end_offset);
}

int croft_editor_command_delete_word_left_range(const croft_editor_text_model* model,
                                                uint32_t anchor_offset,
                                                uint32_t active_offset,
                                                uint32_t* out_start_offset,
                                                uint32_t* out_end_offset) {
    uint32_t move_target = croft_editor_commands_move_word_left_target(model, active_offset);
    return croft_editor_commands_delete_selection_or_range(anchor_offset,
                                                           active_offset,
                                                           move_target,
                                                           1,
                                                           out_start_offset,
                                                           out_end_offset);
}

int croft_editor_command_delete_word_right_range(const croft_editor_text_model* model,
                                                 uint32_t anchor_offset,
                                                 uint32_t active_offset,
                                                 uint32_t* out_start_offset,
                                                 uint32_t* out_end_offset) {
    uint32_t move_target = croft_editor_commands_move_word_right_target(model, active_offset);
    return croft_editor_commands_delete_selection_or_range(anchor_offset,
                                                           active_offset,
                                                           move_target,
                                                           0,
                                                           out_start_offset,
                                                           out_end_offset);
}

int croft_editor_command_delete_word_part_left_range(const croft_editor_text_model* model,
                                                     uint32_t anchor_offset,
                                                     uint32_t active_offset,
                                                     uint32_t* out_start_offset,
                                                     uint32_t* out_end_offset) {
    uint32_t move_target = croft_editor_commands_move_word_part_left_target(model, active_offset);
    return croft_editor_commands_delete_selection_or_range(anchor_offset,
                                                           active_offset,
                                                           move_target,
                                                           1,
                                                           out_start_offset,
                                                           out_end_offset);
}

int croft_editor_command_delete_word_part_right_range(const croft_editor_text_model* model,
                                                      uint32_t anchor_offset,
                                                      uint32_t active_offset,
                                                      uint32_t* out_start_offset,
                                                      uint32_t* out_end_offset) {
    uint32_t move_target = croft_editor_commands_move_word_part_right_target(model, active_offset);
    return croft_editor_commands_delete_selection_or_range(anchor_offset,
                                                           active_offset,
                                                           move_target,
                                                           0,
                                                           out_start_offset,
                                                           out_end_offset);
}

int croft_editor_command_build_tab_edit(const croft_editor_text_model* model,
                                        uint32_t anchor_offset,
                                        uint32_t active_offset,
                                        const croft_editor_tab_settings* settings,
                                        int outdent,
                                        croft_editor_tab_edit* out_edit) {
    uint32_t tab_size;
    int insert_spaces;
    uint32_t start_offset = 0u;
    uint32_t end_offset = 0u;

    if (!model || !out_edit) {
        return 0;
    }

    memset(out_edit, 0, sizeof(*out_edit));

    tab_size = croft_editor_commands_tab_size(settings);
    insert_spaces = croft_editor_commands_insert_spaces(settings);
    anchor_offset = croft_editor_commands_clamp_offset(model, anchor_offset);
    active_offset = croft_editor_commands_clamp_offset(model, active_offset);
    croft_editor_commands_normalize(anchor_offset, active_offset, &start_offset, &end_offset);

    if (!outdent && start_offset == end_offset) {
        croft_editor_position position = croft_editor_text_model_get_position_at(model, start_offset);
        uint32_t insert_codepoints = insert_spaces
            ? (tab_size - ((position.column - 1u) % tab_size))
            : 1u;
        size_t replacement_len = (size_t)insert_codepoints;

        out_edit->replacement_utf8 = (char*)malloc(replacement_len + 1u);
        if (!out_edit->replacement_utf8) {
            return 0;
        }

        if (insert_spaces) {
            memset(out_edit->replacement_utf8, ' ', replacement_len);
        } else {
            out_edit->replacement_utf8[0] = '\t';
        }
        out_edit->replacement_utf8[replacement_len] = '\0';
        out_edit->replacement_utf8_len = replacement_len;
        out_edit->replace_start_offset = start_offset;
        out_edit->replace_end_offset = start_offset;
        out_edit->next_anchor_offset = start_offset + insert_codepoints;
        out_edit->next_active_offset = start_offset + insert_codepoints;
        return 1;
    }

    {
        uint32_t start_line;
        uint32_t end_line;
        uint32_t line_count;
        size_t affected_line_count;
        croft_editor_indented_line* lines = NULL;
        char* replacement_utf8 = NULL;
        size_t replacement_utf8_len = 0u;
        size_t replacement_utf8_cap = 0u;
        uint32_t line_number;
        int changed = 0;

        start_line = croft_editor_text_model_get_position_at(model, start_offset).line_number;
        end_line = croft_editor_text_model_get_position_at(model, end_offset).line_number;
        line_count = croft_editor_text_model_line_count(model);

        if (line_count == 0u) {
            return 0;
        }
        if (end_offset > start_offset
                && end_line > start_line
                && end_offset == croft_editor_text_model_line_start_offset(model, end_line)) {
            end_line--;
        }

        affected_line_count = (size_t)(end_line - start_line + 1u);
        lines = (croft_editor_indented_line*)calloc(affected_line_count, sizeof(*lines));
        if (!lines) {
            return 0;
        }

        out_edit->replace_start_offset = croft_editor_text_model_line_start_offset(model, start_line);
        out_edit->replace_end_offset = croft_editor_text_model_line_end_offset(model, end_line);

        for (line_number = start_line; line_number <= end_line; line_number++) {
            size_t index = (size_t)(line_number - start_line);
            const char* line_utf8;
            uint32_t line_byte_length = 0u;
            uint32_t remove_prefix = 0u;

            lines[index].line_start_offset = croft_editor_text_model_line_start_offset(model, line_number);
            line_utf8 = croft_editor_text_model_line_utf8(model, line_number, &line_byte_length);
            if (!line_utf8) {
                free(lines);
                free(replacement_utf8);
                memset(out_edit, 0, sizeof(*out_edit));
                return 0;
            }

            if (outdent) {
                remove_prefix =
                    croft_editor_commands_outdent_prefix_codepoints(model, line_number, tab_size);
                lines[index].removed_prefix_codepoints = remove_prefix;
                changed = changed || remove_prefix > 0u;
            } else {
                lines[index].inserted_prefix_codepoints = insert_spaces ? tab_size : 1u;
                changed = 1;
                if (insert_spaces) {
                    if (!croft_editor_commands_append_spaces(&replacement_utf8,
                                                             &replacement_utf8_len,
                                                             &replacement_utf8_cap,
                                                             lines[index].inserted_prefix_codepoints)) {
                        free(lines);
                        free(replacement_utf8);
                        memset(out_edit, 0, sizeof(*out_edit));
                        return 0;
                    }
                } else if (!croft_editor_commands_append_utf8(&replacement_utf8,
                                                              &replacement_utf8_len,
                                                              &replacement_utf8_cap,
                                                              "\t",
                                                              1u)) {
                    free(lines);
                    free(replacement_utf8);
                    memset(out_edit, 0, sizeof(*out_edit));
                    return 0;
                }
            }

            if (!croft_editor_commands_append_utf8(&replacement_utf8,
                                                   &replacement_utf8_len,
                                                   &replacement_utf8_cap,
                                                   line_utf8 + remove_prefix,
                                                   (size_t)(line_byte_length - remove_prefix))) {
                free(lines);
                free(replacement_utf8);
                memset(out_edit, 0, sizeof(*out_edit));
                return 0;
            }
            if (line_number < end_line
                    && !croft_editor_commands_append_utf8(&replacement_utf8,
                                                          &replacement_utf8_len,
                                                          &replacement_utf8_cap,
                                                          "\n",
                                                          1u)) {
                free(lines);
                free(replacement_utf8);
                memset(out_edit, 0, sizeof(*out_edit));
                return 0;
            }
        }

        if (!changed) {
            free(lines);
            free(replacement_utf8);
            memset(out_edit, 0, sizeof(*out_edit));
            return 0;
        }

        out_edit->replacement_utf8 = replacement_utf8;
        out_edit->replacement_utf8_len = replacement_utf8_len;
        out_edit->next_anchor_offset = croft_editor_commands_map_block_offset(lines,
                                                                              affected_line_count,
                                                                              anchor_offset,
                                                                              outdent);
        out_edit->next_active_offset = croft_editor_commands_map_block_offset(lines,
                                                                              affected_line_count,
                                                                              active_offset,
                                                                              outdent);
        free(lines);
    }

    return 1;
}
