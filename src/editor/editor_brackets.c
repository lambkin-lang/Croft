#include "croft/editor_brackets.h"

typedef struct {
    uint32_t open_codepoint;
    uint32_t close_codepoint;
    int direction;
} croft_editor_bracket_descriptor;

static int croft_editor_bracket_descriptor_for_codepoint(uint32_t codepoint,
                                                         croft_editor_bracket_descriptor* out_descriptor) {
    if (!out_descriptor) {
        return 0;
    }

    switch (codepoint) {
        case '(':
            out_descriptor->open_codepoint = '(';
            out_descriptor->close_codepoint = ')';
            out_descriptor->direction = 1;
            return 1;
        case ')':
            out_descriptor->open_codepoint = '(';
            out_descriptor->close_codepoint = ')';
            out_descriptor->direction = -1;
            return 1;
        case '[':
            out_descriptor->open_codepoint = '[';
            out_descriptor->close_codepoint = ']';
            out_descriptor->direction = 1;
            return 1;
        case ']':
            out_descriptor->open_codepoint = '[';
            out_descriptor->close_codepoint = ']';
            out_descriptor->direction = -1;
            return 1;
        case '{':
            out_descriptor->open_codepoint = '{';
            out_descriptor->close_codepoint = '}';
            out_descriptor->direction = 1;
            return 1;
        case '}':
            out_descriptor->open_codepoint = '{';
            out_descriptor->close_codepoint = '}';
            out_descriptor->direction = -1;
            return 1;
        default:
            return 0;
    }
}

int32_t croft_editor_bracket_match_at_offset(const croft_editor_text_model* model,
                                             uint32_t bracket_offset,
                                             croft_editor_bracket_match* out_match) {
    uint32_t bracket_codepoint = 0u;
    croft_editor_bracket_descriptor descriptor;
    uint32_t depth = 0u;
    uint32_t max_offset;

    if (!model || !out_match) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    max_offset = croft_editor_text_model_codepoint_length(model);
    if (bracket_offset >= max_offset
            || croft_editor_text_model_codepoint_at_offset(model,
                                                           bracket_offset,
                                                           &bracket_codepoint) != CROFT_EDITOR_OK
            || !croft_editor_bracket_descriptor_for_codepoint(bracket_codepoint, &descriptor)) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    if (descriptor.direction > 0) {
        uint32_t probe;

        for (probe = bracket_offset + 1u; probe < max_offset; probe++) {
            uint32_t codepoint = 0u;

            if (croft_editor_text_model_codepoint_at_offset(model, probe, &codepoint) != CROFT_EDITOR_OK) {
                return CROFT_EDITOR_ERR_INVALID;
            }
            if (codepoint == descriptor.open_codepoint) {
                depth++;
            } else if (codepoint == descriptor.close_codepoint) {
                if (depth == 0u) {
                    out_match->open_offset = bracket_offset;
                    out_match->close_offset = probe;
                    return CROFT_EDITOR_OK;
                }
                depth--;
            }
        }
    } else {
        uint32_t probe = bracket_offset;

        while (probe > 0u) {
            uint32_t codepoint = 0u;

            probe--;
            if (croft_editor_text_model_codepoint_at_offset(model, probe, &codepoint) != CROFT_EDITOR_OK) {
                return CROFT_EDITOR_ERR_INVALID;
            }
            if (codepoint == descriptor.close_codepoint) {
                depth++;
            } else if (codepoint == descriptor.open_codepoint) {
                if (depth == 0u) {
                    out_match->open_offset = probe;
                    out_match->close_offset = bracket_offset;
                    return CROFT_EDITOR_OK;
                }
                depth--;
            }
        }
    }

    return CROFT_EDITOR_ERR_INVALID;
}

int32_t croft_editor_bracket_match_near_offset(const croft_editor_text_model* model,
                                               uint32_t cursor_offset,
                                               croft_editor_bracket_match* out_match) {
    uint32_t max_offset;

    if (!model || !out_match) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    max_offset = croft_editor_text_model_codepoint_length(model);
    if (cursor_offset > max_offset) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    if (cursor_offset > 0u
            && croft_editor_bracket_match_at_offset(model, cursor_offset - 1u, out_match)
                == CROFT_EDITOR_OK) {
        return CROFT_EDITOR_OK;
    }
    if (cursor_offset < max_offset
            && croft_editor_bracket_match_at_offset(model, cursor_offset, out_match)
                == CROFT_EDITOR_OK) {
        return CROFT_EDITOR_OK;
    }

    return CROFT_EDITOR_ERR_INVALID;
}
