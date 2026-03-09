#include "croft/editor_syntax.h"

#include <string.h>

static int croft_editor_syntax_ascii_case_equal(char a, char b) {
    if (a >= 'A' && a <= 'Z') {
        a = (char)(a - 'A' + 'a');
    }
    if (b >= 'A' && b <= 'Z') {
        b = (char)(b - 'A' + 'a');
    }
    return a == b;
}

static int croft_editor_syntax_path_has_suffix(const char* path, const char* suffix) {
    size_t path_len;
    size_t suffix_len;
    size_t index;

    if (!path || !suffix) {
        return 0;
    }

    path_len = strlen(path);
    suffix_len = strlen(suffix);
    if (path_len < suffix_len) {
        return 0;
    }

    for (index = 0u; index < suffix_len; index++) {
        if (!croft_editor_syntax_ascii_case_equal(path[path_len - suffix_len + index], suffix[index])) {
            return 0;
        }
    }
    return 1;
}

static int croft_editor_syntax_json_is_whitespace(uint32_t codepoint) {
    return codepoint == ' ' || codepoint == '\t' || codepoint == '\r' || codepoint == '\n';
}

static int croft_editor_syntax_json_is_digit(uint32_t codepoint) {
    return codepoint >= '0' && codepoint <= '9';
}

static int croft_editor_syntax_json_is_punctuation(uint32_t codepoint) {
    return codepoint == '{'
        || codepoint == '}'
        || codepoint == '['
        || codepoint == ']'
        || codepoint == ','
        || codepoint == ':';
}

static int croft_editor_syntax_json_is_boundary(uint32_t codepoint) {
    return croft_editor_syntax_json_is_whitespace(codepoint)
        || croft_editor_syntax_json_is_punctuation(codepoint);
}

static int croft_editor_syntax_codepoint_at(const croft_editor_text_model* model,
                                            uint32_t offset,
                                            uint32_t* out_codepoint) {
    return croft_editor_text_model_codepoint_at_offset(model, offset, out_codepoint) == CROFT_EDITOR_OK;
}

static uint32_t croft_editor_syntax_json_scan_invalid(const croft_editor_text_model* model,
                                                      uint32_t start_offset,
                                                      uint32_t limit_offset) {
    uint32_t offset = start_offset;
    uint32_t codepoint = 0u;

    while (offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        if (croft_editor_syntax_json_is_boundary(codepoint)) {
            break;
        }
        offset++;
    }
    if (offset == start_offset) {
        offset++;
    }
    return offset;
}

static uint32_t croft_editor_syntax_json_skip_whitespace(const croft_editor_text_model* model,
                                                         uint32_t offset,
                                                         uint32_t limit_offset) {
    uint32_t codepoint = 0u;

    while (offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        if (!croft_editor_syntax_json_is_whitespace(codepoint)) {
            break;
        }
        offset++;
    }
    return offset;
}

static uint32_t croft_editor_syntax_json_scan_string(const croft_editor_text_model* model,
                                                     uint32_t start_offset,
                                                     uint32_t limit_offset,
                                                     int* out_closed) {
    uint32_t offset = start_offset + 1u;
    uint32_t codepoint = 0u;
    int escaped = 0;

    if (out_closed) {
        *out_closed = 0;
    }

    while (offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        if (!escaped && codepoint == '"') {
            if (out_closed) {
                *out_closed = 1;
            }
            return offset + 1u;
        }
        if (!escaped && codepoint == '\\') {
            escaped = 1;
        } else {
            escaped = 0;
        }
        offset++;
    }
    return limit_offset;
}

static int croft_editor_syntax_json_string_is_property(const croft_editor_text_model* model,
                                                       uint32_t end_offset,
                                                       uint32_t limit_offset) {
    uint32_t probe = croft_editor_syntax_json_skip_whitespace(model, end_offset, limit_offset);
    uint32_t codepoint = 0u;

    if (probe >= limit_offset || !croft_editor_syntax_codepoint_at(model, probe, &codepoint)) {
        return 0;
    }
    return codepoint == ':';
}

static uint32_t croft_editor_syntax_json_scan_number(const croft_editor_text_model* model,
                                                     uint32_t start_offset,
                                                     uint32_t limit_offset,
                                                     int* out_valid) {
    uint32_t offset = start_offset;
    uint32_t codepoint = 0u;

    if (out_valid) {
        *out_valid = 0;
    }

    if (offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint) && codepoint == '-') {
        offset++;
    }
    if (offset >= limit_offset || !croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        return offset;
    }

    if (codepoint == '0') {
        offset++;
    } else if (codepoint >= '1' && codepoint <= '9') {
        do {
            offset++;
        } while (offset < limit_offset
                 && croft_editor_syntax_codepoint_at(model, offset, &codepoint)
                 && croft_editor_syntax_json_is_digit(codepoint));
    } else {
        return offset;
    }

    if (offset < limit_offset
            && croft_editor_syntax_codepoint_at(model, offset, &codepoint)
            && codepoint == '.') {
        uint32_t digits_start;

        offset++;
        digits_start = offset;
        while (offset < limit_offset
                && croft_editor_syntax_codepoint_at(model, offset, &codepoint)
                && croft_editor_syntax_json_is_digit(codepoint)) {
            offset++;
        }
        if (digits_start == offset) {
            return offset;
        }
    }

    if (offset < limit_offset
            && croft_editor_syntax_codepoint_at(model, offset, &codepoint)
            && (codepoint == 'e' || codepoint == 'E')) {
        uint32_t digits_start;

        offset++;
        if (offset < limit_offset
                && croft_editor_syntax_codepoint_at(model, offset, &codepoint)
                && (codepoint == '+' || codepoint == '-')) {
            offset++;
        }
        digits_start = offset;
        while (offset < limit_offset
                && croft_editor_syntax_codepoint_at(model, offset, &codepoint)
                && croft_editor_syntax_json_is_digit(codepoint)) {
            offset++;
        }
        if (digits_start == offset) {
            return offset;
        }
    }

    if (offset < limit_offset
            && croft_editor_syntax_codepoint_at(model, offset, &codepoint)
            && !croft_editor_syntax_json_is_boundary(codepoint)) {
        return croft_editor_syntax_json_scan_invalid(model, start_offset, limit_offset);
    }

    if (out_valid) {
        *out_valid = 1;
    }
    return offset;
}

static int croft_editor_syntax_json_match_literal(const croft_editor_text_model* model,
                                                  uint32_t start_offset,
                                                  uint32_t limit_offset,
                                                  const char* literal) {
    uint32_t offset = start_offset;
    size_t index = 0u;
    uint32_t codepoint = 0u;

    while (literal[index] != '\0') {
        if (offset >= limit_offset || !croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
            return 0;
        }
        if (codepoint != (uint32_t)(unsigned char)literal[index]) {
            return 0;
        }
        offset++;
        index++;
    }

    if (offset < limit_offset
            && croft_editor_syntax_codepoint_at(model, offset, &codepoint)
            && !croft_editor_syntax_json_is_boundary(codepoint)) {
        return 0;
    }
    return 1;
}

static int32_t croft_editor_syntax_json_next_token(const croft_editor_text_model* model,
                                                   uint32_t search_offset,
                                                   uint32_t limit_offset,
                                                   croft_editor_syntax_token* out_token) {
    uint32_t offset;
    uint32_t codepoint = 0u;

    if (!model || !out_token || search_offset >= limit_offset) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    offset = croft_editor_syntax_json_skip_whitespace(model, search_offset, limit_offset);
    if (offset >= limit_offset || !croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    out_token->start_offset = offset;
    out_token->end_offset = offset + 1u;
    out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION;

    if (codepoint == '"') {
        int closed = 0;
        out_token->end_offset =
            croft_editor_syntax_json_scan_string(model, offset, limit_offset, &closed);
        out_token->kind = closed
            ? (croft_editor_syntax_json_string_is_property(model,
                                                           out_token->end_offset,
                                                           limit_offset)
                ? CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY
                : CROFT_EDITOR_SYNTAX_TOKEN_STRING)
            : CROFT_EDITOR_SYNTAX_TOKEN_INVALID;
        return CROFT_EDITOR_OK;
    }

    if (codepoint == '-' || croft_editor_syntax_json_is_digit(codepoint)) {
        int valid = 0;
        out_token->end_offset =
            croft_editor_syntax_json_scan_number(model, offset, limit_offset, &valid);
        if (out_token->end_offset <= offset) {
            out_token->end_offset = croft_editor_syntax_json_scan_invalid(model, offset, limit_offset);
        }
        out_token->kind = valid ? CROFT_EDITOR_SYNTAX_TOKEN_NUMBER : CROFT_EDITOR_SYNTAX_TOKEN_INVALID;
        return CROFT_EDITOR_OK;
    }

    if (croft_editor_syntax_json_match_literal(model, offset, limit_offset, "true")) {
        out_token->end_offset = offset + 4u;
        out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD;
        return CROFT_EDITOR_OK;
    }
    if (croft_editor_syntax_json_match_literal(model, offset, limit_offset, "false")) {
        out_token->end_offset = offset + 5u;
        out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD;
        return CROFT_EDITOR_OK;
    }
    if (croft_editor_syntax_json_match_literal(model, offset, limit_offset, "null")) {
        out_token->end_offset = offset + 4u;
        out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD;
        return CROFT_EDITOR_OK;
    }

    if (croft_editor_syntax_json_is_punctuation(codepoint)) {
        return CROFT_EDITOR_OK;
    }

    out_token->end_offset = croft_editor_syntax_json_scan_invalid(model, offset, limit_offset);
    out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_INVALID;
    return CROFT_EDITOR_OK;
}

croft_editor_syntax_language croft_editor_syntax_language_from_path(const char* path) {
    if (croft_editor_syntax_path_has_suffix(path, ".json")) {
        return CROFT_EDITOR_SYNTAX_LANGUAGE_JSON;
    }
    return CROFT_EDITOR_SYNTAX_LANGUAGE_PLAIN_TEXT;
}

int32_t croft_editor_syntax_next_token(const croft_editor_text_model* model,
                                       croft_editor_syntax_language language,
                                       uint32_t search_offset,
                                       uint32_t limit_offset,
                                       croft_editor_syntax_token* out_token) {
    if (!model || !out_token || limit_offset > croft_editor_text_model_codepoint_length(model)) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    if (search_offset >= limit_offset) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    if (language == CROFT_EDITOR_SYNTAX_LANGUAGE_JSON) {
        return croft_editor_syntax_json_next_token(model, search_offset, limit_offset, out_token);
    }
    return CROFT_EDITOR_ERR_INVALID;
}
