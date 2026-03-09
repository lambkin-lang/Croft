#include "croft/editor_syntax.h"

#include <string.h>

typedef int (*croft_editor_syntax_boundary_fn)(uint32_t codepoint);

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

static int croft_editor_syntax_codepoint_at(const croft_editor_text_model* model,
                                            uint32_t offset,
                                            uint32_t* out_codepoint) {
    return croft_editor_text_model_codepoint_at_offset(model, offset, out_codepoint) == CROFT_EDITOR_OK;
}

static int croft_editor_syntax_is_whitespace(uint32_t codepoint) {
    return codepoint == ' ' || codepoint == '\t' || codepoint == '\r' || codepoint == '\n';
}

static int croft_editor_syntax_is_linebreak(uint32_t codepoint) {
    return codepoint == '\r' || codepoint == '\n';
}

static int croft_editor_syntax_is_digit(uint32_t codepoint) {
    return codepoint >= '0' && codepoint <= '9';
}

static int croft_editor_syntax_is_hex_digit(uint32_t codepoint) {
    return croft_editor_syntax_is_digit(codepoint)
        || (codepoint >= 'a' && codepoint <= 'f')
        || (codepoint >= 'A' && codepoint <= 'F');
}

static int croft_editor_syntax_is_ascii_alpha(uint32_t codepoint) {
    return (codepoint >= 'a' && codepoint <= 'z')
        || (codepoint >= 'A' && codepoint <= 'Z');
}

static int croft_editor_syntax_is_ascii_alnum(uint32_t codepoint) {
    return croft_editor_syntax_is_ascii_alpha(codepoint)
        || croft_editor_syntax_is_digit(codepoint);
}

static uint32_t croft_editor_syntax_skip_whitespace(const croft_editor_text_model* model,
                                                    uint32_t offset,
                                                    uint32_t limit_offset) {
    uint32_t codepoint = 0u;

    while (offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        if (!croft_editor_syntax_is_whitespace(codepoint)) {
            break;
        }
        offset++;
    }
    return offset;
}

static uint32_t croft_editor_syntax_scan_line_comment(const croft_editor_text_model* model,
                                                      uint32_t start_offset,
                                                      uint32_t limit_offset) {
    uint32_t offset = start_offset;
    uint32_t codepoint = 0u;

    while (offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        if (croft_editor_syntax_is_linebreak(codepoint)) {
            break;
        }
        offset++;
    }
    return offset;
}

static uint32_t croft_editor_syntax_scan_string(const croft_editor_text_model* model,
                                                uint32_t start_offset,
                                                uint32_t limit_offset,
                                                uint32_t quote,
                                                int* out_closed) {
    uint32_t offset = start_offset + 1u;
    uint32_t codepoint = 0u;
    int escaped = 0;

    if (out_closed) {
        *out_closed = 0;
    }

    while (offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        if (croft_editor_syntax_is_linebreak(codepoint) && quote == '\'') {
            return offset;
        }
        if (!escaped && codepoint == quote) {
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

static uint32_t croft_editor_syntax_scan_invalid(const croft_editor_text_model* model,
                                                 uint32_t start_offset,
                                                 uint32_t limit_offset,
                                                 croft_editor_syntax_boundary_fn is_boundary) {
    uint32_t offset = start_offset;
    uint32_t codepoint = 0u;

    while (offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        if ((is_boundary && is_boundary(codepoint)) || croft_editor_syntax_is_whitespace(codepoint)) {
            break;
        }
        offset++;
    }
    if (offset == start_offset) {
        offset++;
    }
    return offset;
}

static int croft_editor_syntax_underscore_starts_suffix(const croft_editor_text_model* model,
                                                        uint32_t offset,
                                                        uint32_t limit_offset) {
    uint32_t codepoint = 0u;
    uint32_t next_codepoint = 0u;

    if (offset + 1u >= limit_offset) {
        return 0;
    }
    if (!croft_editor_syntax_codepoint_at(model, offset, &codepoint)
            || !croft_editor_syntax_codepoint_at(model, offset + 1u, &next_codepoint)) {
        return 0;
    }
    return codepoint == '_' && croft_editor_syntax_is_ascii_alpha(next_codepoint);
}

static uint32_t croft_editor_syntax_scan_number(const croft_editor_text_model* model,
                                                uint32_t start_offset,
                                                uint32_t limit_offset) {
    uint32_t offset = start_offset;
    uint32_t codepoint = 0u;
    int saw_digits = 0;

    if (offset < limit_offset
            && croft_editor_syntax_codepoint_at(model, offset, &codepoint)
            && codepoint == '-') {
        offset++;
    }

    if (offset + 1u < limit_offset
            && croft_editor_syntax_codepoint_at(model, offset, &codepoint)
            && codepoint == '0') {
        uint32_t prefix = 0u;

        if (croft_editor_syntax_codepoint_at(model, offset + 1u, &prefix)
                && (prefix == 'x' || prefix == 'X' || prefix == 'b' || prefix == 'B'
                    || prefix == 'o' || prefix == 'O')) {
            offset += 2u;
            while (offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
                int ok = 0;
                if (prefix == 'x' || prefix == 'X') {
                    ok = croft_editor_syntax_is_hex_digit(codepoint)
                        || (codepoint == '_'
                            && !croft_editor_syntax_underscore_starts_suffix(model, offset, limit_offset));
                } else if (prefix == 'b' || prefix == 'B') {
                    ok = codepoint == '0'
                        || codepoint == '1'
                        || (codepoint == '_'
                            && !croft_editor_syntax_underscore_starts_suffix(model, offset, limit_offset));
                } else {
                    ok = (codepoint >= '0' && codepoint <= '7')
                        || (codepoint == '_'
                            && !croft_editor_syntax_underscore_starts_suffix(model, offset, limit_offset));
                }
                if (!ok) {
                    break;
                }
                saw_digits = 1;
                offset++;
            }
        }
    }

    if (!saw_digits) {
        while (offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
            if (!croft_editor_syntax_is_digit(codepoint)
                    && (codepoint != '_'
                        || croft_editor_syntax_underscore_starts_suffix(model, offset, limit_offset))) {
                break;
            }
            if (croft_editor_syntax_is_digit(codepoint)) {
                saw_digits = 1;
            }
            offset++;
        }

        if (offset < limit_offset
                && croft_editor_syntax_codepoint_at(model, offset, &codepoint)
                && codepoint == '.') {
            uint32_t probe = offset + 1u;
            uint32_t next_codepoint = 0u;

            if (probe < limit_offset
                    && croft_editor_syntax_codepoint_at(model, probe, &next_codepoint)
                    && croft_editor_syntax_is_digit(next_codepoint)) {
                offset++;
                while (offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
                    if (!croft_editor_syntax_is_digit(codepoint)
                            && (codepoint != '_'
                                || croft_editor_syntax_underscore_starts_suffix(model, offset, limit_offset))) {
                        break;
                    }
                    offset++;
                }
            }
        }

        if (offset < limit_offset
                && croft_editor_syntax_codepoint_at(model, offset, &codepoint)
                && (codepoint == 'e' || codepoint == 'E')) {
            uint32_t probe = offset + 1u;
            uint32_t next_codepoint = 0u;

            if (probe < limit_offset && croft_editor_syntax_codepoint_at(model, probe, &next_codepoint)) {
                if (next_codepoint == '+' || next_codepoint == '-') {
                    probe++;
                }
            }
            if (probe < limit_offset
                    && croft_editor_syntax_codepoint_at(model, probe, &next_codepoint)
                    && croft_editor_syntax_is_digit(next_codepoint)) {
                offset++;
                if (offset < limit_offset
                        && croft_editor_syntax_codepoint_at(model, offset, &codepoint)
                        && (codepoint == '+' || codepoint == '-')) {
                    offset++;
                }
                while (offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
                    if (!croft_editor_syntax_is_digit(codepoint)
                            && (codepoint != '_'
                                || croft_editor_syntax_underscore_starts_suffix(model, offset, limit_offset))) {
                        break;
                    }
                    offset++;
                }
            }
        }
    }

    if (saw_digits && offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        if (codepoint == '_') {
            offset++;
            while (offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
                if (!croft_editor_syntax_is_ascii_alnum(codepoint) && codepoint != '_') {
                    break;
                }
                offset++;
            }
        }
    }

    if (!saw_digits) {
        return start_offset;
    }
    return offset;
}

static int croft_editor_syntax_token_equals(const croft_editor_text_model* model,
                                            uint32_t start_offset,
                                            uint32_t end_offset,
                                            const char* text) {
    const char* model_text;
    uint32_t start_byte;
    uint32_t end_byte;
    size_t text_len;

    if (!model || !text || end_offset < start_offset) {
        return 0;
    }

    model_text = croft_editor_text_model_text(model);
    start_byte = croft_editor_text_model_byte_offset_at(model, start_offset);
    end_byte = croft_editor_text_model_byte_offset_at(model, end_offset);
    text_len = strlen(text);
    if ((size_t)(end_byte - start_byte) != text_len) {
        return 0;
    }
    return memcmp(model_text + start_byte, text, text_len) == 0;
}

static int croft_editor_syntax_token_in_list(const croft_editor_text_model* model,
                                             uint32_t start_offset,
                                             uint32_t end_offset,
                                             const char* const* list,
                                             size_t list_count) {
    size_t index;

    for (index = 0u; index < list_count; index++) {
        if (croft_editor_syntax_token_equals(model, start_offset, end_offset, list[index])) {
            return 1;
        }
    }
    return 0;
}

static int croft_editor_syntax_token_contains_char(const croft_editor_text_model* model,
                                                   uint32_t start_offset,
                                                   uint32_t end_offset,
                                                   char needle) {
    const char* model_text = croft_editor_text_model_text(model);
    uint32_t start_byte = croft_editor_text_model_byte_offset_at(model, start_offset);
    uint32_t end_byte = croft_editor_text_model_byte_offset_at(model, end_offset);
    uint32_t index;

    for (index = start_byte; index < end_byte; index++) {
        if (model_text[index] == needle) {
            return 1;
        }
    }
    return 0;
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
    return croft_editor_syntax_json_is_punctuation(codepoint)
        || croft_editor_syntax_is_whitespace(codepoint);
}

static int croft_editor_syntax_json_string_is_property(const croft_editor_text_model* model,
                                                       uint32_t end_offset,
                                                       uint32_t limit_offset) {
    uint32_t probe = croft_editor_syntax_skip_whitespace(model, end_offset, limit_offset);
    uint32_t codepoint = 0u;

    if (probe >= limit_offset || !croft_editor_syntax_codepoint_at(model, probe, &codepoint)) {
        return 0;
    }
    return codepoint == ':';
}

static int croft_editor_syntax_json_match_literal(const croft_editor_text_model* model,
                                                  uint32_t start_offset,
                                                  uint32_t end_offset,
                                                  const char* literal) {
    if (!croft_editor_syntax_token_equals(model, start_offset, end_offset, literal)) {
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

    offset = croft_editor_syntax_skip_whitespace(model, search_offset, limit_offset);
    if (offset >= limit_offset || !croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    out_token->start_offset = offset;
    out_token->end_offset = offset + 1u;
    out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION;

    if (codepoint == '"') {
        int closed = 0;
        out_token->end_offset = croft_editor_syntax_scan_string(model, offset, limit_offset, '"', &closed);
        out_token->kind = closed
            ? (croft_editor_syntax_json_string_is_property(model, out_token->end_offset, limit_offset)
                ? CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY
                : CROFT_EDITOR_SYNTAX_TOKEN_STRING)
            : CROFT_EDITOR_SYNTAX_TOKEN_INVALID;
        return CROFT_EDITOR_OK;
    }

    if (codepoint == '-' || croft_editor_syntax_is_digit(codepoint)) {
        uint32_t end_offset = croft_editor_syntax_scan_number(model, offset, limit_offset);
        out_token->end_offset = end_offset > offset
            ? end_offset
            : croft_editor_syntax_scan_invalid(model,
                                               offset,
                                               limit_offset,
                                               croft_editor_syntax_json_is_boundary);
        out_token->kind = end_offset > offset
            ? CROFT_EDITOR_SYNTAX_TOKEN_NUMBER
            : CROFT_EDITOR_SYNTAX_TOKEN_INVALID;
        return CROFT_EDITOR_OK;
    }

    if (croft_editor_syntax_json_is_punctuation(codepoint)) {
        return CROFT_EDITOR_OK;
    }

    out_token->end_offset =
        croft_editor_syntax_scan_invalid(model, offset, limit_offset, croft_editor_syntax_json_is_boundary);
    if (croft_editor_syntax_json_match_literal(model, out_token->start_offset, out_token->end_offset, "true")
            || croft_editor_syntax_json_match_literal(model, out_token->start_offset, out_token->end_offset, "false")
            || croft_editor_syntax_json_match_literal(model, out_token->start_offset, out_token->end_offset, "null")) {
        out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD;
    } else {
        out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_INVALID;
    }
    return CROFT_EDITOR_OK;
}

static int croft_editor_syntax_lambkin_is_symbolic_char(uint32_t codepoint) {
    return codepoint == '+'
        || codepoint == '-'
        || codepoint == '*'
        || codepoint == '/'
        || codepoint == '<'
        || codepoint == '>'
        || codepoint == '&'
        || codepoint == '~';
}

static int croft_editor_syntax_lambkin_is_identifier_start(uint32_t codepoint) {
    return croft_editor_syntax_is_ascii_alpha(codepoint)
        || codepoint == '_'
        || croft_editor_syntax_lambkin_is_symbolic_char(codepoint);
}

static int croft_editor_syntax_lambkin_is_identifier_continue(uint32_t codepoint) {
    return croft_editor_syntax_lambkin_is_identifier_start(codepoint)
        || croft_editor_syntax_is_digit(codepoint);
}

static int croft_editor_syntax_lambkin_is_punctuation(uint32_t codepoint) {
    return codepoint == '('
        || codepoint == ')'
        || codepoint == '['
        || codepoint == ']'
        || codepoint == '{'
        || codepoint == '}'
        || codepoint == ':'
        || codepoint == ','
        || codepoint == '.'
        || codepoint == ';'
        || codepoint == '=';
}

static int croft_editor_syntax_lambkin_is_boundary(uint32_t codepoint) {
    return croft_editor_syntax_lambkin_is_punctuation(codepoint)
        || croft_editor_syntax_is_whitespace(codepoint);
}

static uint32_t croft_editor_syntax_scan_lambkin_identifier(const croft_editor_text_model* model,
                                                            uint32_t start_offset,
                                                            uint32_t limit_offset) {
    uint32_t offset = start_offset;
    uint32_t codepoint = 0u;

    if (!croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        return start_offset;
    }

    if (codepoint == '`') {
        offset++;
        while (offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
            offset++;
            if (codepoint == '`') {
                break;
            }
        }
        return offset;
    }

    if (codepoint == '%') {
        offset++;
    }

    while (offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        if (!croft_editor_syntax_lambkin_is_identifier_continue(codepoint)) {
            break;
        }
        offset++;
    }
    return offset;
}

static int32_t croft_editor_syntax_lambkin_next_token(const croft_editor_text_model* model,
                                                      uint32_t search_offset,
                                                      uint32_t limit_offset,
                                                      croft_editor_syntax_token* out_token) {
    static const char* const lambkin_keywords[] = {
        "let", "local", "func", "functype", "param", "record", "resource", "form",
        "variant", "enum", "flags", "case", "match", "end", "if", "else", "else-if",
        "fi", "loop", "while", "until", "repeat", "for", "between", "nop", "true",
        "false", "claim", "requires", "ensures", "invariant", "predicate", "measure",
        "type", "lamb", "result", "it"
    };
    static const char* const lambkin_types[] = {
        "integer", "floating-point", "char", "string", "boolean", "bool", "unit",
        "bytes", "list", "tuple", "option", "result", "i8", "i16", "i32", "i64",
        "u8", "u16", "u32", "u64", "f32", "f64"
    };
    uint32_t offset;
    uint32_t codepoint = 0u;
    uint32_t next_codepoint = 0u;

    if (!model || !out_token || search_offset >= limit_offset) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    offset = croft_editor_syntax_skip_whitespace(model, search_offset, limit_offset);
    if (offset >= limit_offset || !croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    out_token->start_offset = offset;
    out_token->end_offset = offset + 1u;
    out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PLAIN;

    if (offset + 1u < limit_offset && croft_editor_syntax_codepoint_at(model, offset + 1u, &next_codepoint)) {
        if ((codepoint == '-' && next_codepoint == '-') || (codepoint == '/' && next_codepoint == '/')) {
            out_token->end_offset = croft_editor_syntax_scan_line_comment(model, offset, limit_offset);
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_COMMENT;
            return CROFT_EDITOR_OK;
        }
        if ((codepoint == ':' && next_codepoint == '=') || (codepoint == '-' && next_codepoint == '>')) {
            out_token->end_offset = offset + 2u;
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION;
            return CROFT_EDITOR_OK;
        }
    }

    if (codepoint == '"' || codepoint == '\'') {
        int closed = 0;
        out_token->end_offset = croft_editor_syntax_scan_string(model, offset, limit_offset, codepoint, &closed);
        out_token->kind = closed ? CROFT_EDITOR_SYNTAX_TOKEN_STRING : CROFT_EDITOR_SYNTAX_TOKEN_INVALID;
        return CROFT_EDITOR_OK;
    }

    if (codepoint == '-' || croft_editor_syntax_is_digit(codepoint)) {
        uint32_t number_end = croft_editor_syntax_scan_number(model, offset, limit_offset);
        if (number_end > offset) {
            out_token->end_offset = number_end;
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_NUMBER;
            return CROFT_EDITOR_OK;
        }
    }

    if (croft_editor_syntax_lambkin_is_punctuation(codepoint)) {
        out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION;
        return CROFT_EDITOR_OK;
    }

    if (codepoint == '`'
            || codepoint == '%'
            || croft_editor_syntax_lambkin_is_identifier_start(codepoint)) {
        out_token->end_offset = croft_editor_syntax_scan_lambkin_identifier(model, offset, limit_offset);
        if (croft_editor_syntax_token_in_list(model,
                                              out_token->start_offset,
                                              out_token->end_offset,
                                              lambkin_keywords,
                                              sizeof(lambkin_keywords) / sizeof(lambkin_keywords[0]))) {
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD;
        } else if (croft_editor_syntax_token_in_list(model,
                                                     out_token->start_offset,
                                                     out_token->end_offset,
                                                     lambkin_types,
                                                     sizeof(lambkin_types) / sizeof(lambkin_types[0]))) {
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_TYPE;
        } else {
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PLAIN;
        }
        return CROFT_EDITOR_OK;
    }

    out_token->end_offset =
        croft_editor_syntax_scan_invalid(model, offset, limit_offset, croft_editor_syntax_lambkin_is_boundary);
    out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_INVALID;
    return CROFT_EDITOR_OK;
}

static int croft_editor_syntax_wit_is_identifier_start(uint32_t codepoint) {
    return croft_editor_syntax_is_ascii_alpha(codepoint) || codepoint == '_';
}

static int croft_editor_syntax_wit_is_identifier_continue(uint32_t codepoint) {
    return croft_editor_syntax_wit_is_identifier_start(codepoint)
        || croft_editor_syntax_is_digit(codepoint)
        || codepoint == '-';
}

static int croft_editor_syntax_wit_is_punctuation(uint32_t codepoint) {
    return codepoint == '('
        || codepoint == ')'
        || codepoint == '{'
        || codepoint == '}'
        || codepoint == '<'
        || codepoint == '>'
        || codepoint == ':'
        || codepoint == ','
        || codepoint == '.'
        || codepoint == ';'
        || codepoint == '='
        || codepoint == '@';
}

static int croft_editor_syntax_wit_is_boundary(uint32_t codepoint) {
    return croft_editor_syntax_wit_is_punctuation(codepoint)
        || croft_editor_syntax_is_whitespace(codepoint);
}

static uint32_t croft_editor_syntax_scan_wit_identifier(const croft_editor_text_model* model,
                                                        uint32_t start_offset,
                                                        uint32_t limit_offset) {
    uint32_t offset = start_offset;
    uint32_t codepoint = 0u;

    if (!croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        return start_offset;
    }

    if (codepoint == '%') {
        offset++;
    }

    while (offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        if (!croft_editor_syntax_wit_is_identifier_continue(codepoint)) {
            break;
        }
        offset++;
    }
    return offset;
}

static int croft_editor_syntax_wit_identifier_is_property(const croft_editor_text_model* model,
                                                          uint32_t end_offset,
                                                          uint32_t limit_offset) {
    uint32_t probe = croft_editor_syntax_skip_whitespace(model, end_offset, limit_offset);
    uint32_t codepoint = 0u;

    if (probe >= limit_offset || !croft_editor_syntax_codepoint_at(model, probe, &codepoint)) {
        return 0;
    }
    return codepoint == ':';
}

static int32_t croft_editor_syntax_wit_next_token(const croft_editor_text_model* model,
                                                  uint32_t search_offset,
                                                  uint32_t limit_offset,
                                                  croft_editor_syntax_token* out_token) {
    static const char* const wit_keywords[] = {
        "package", "world", "interface", "import", "export", "use", "include", "with",
        "as", "type", "record", "resource", "func", "static", "constructor", "enum",
        "flags", "variant", "case"
    };
    static const char* const wit_types[] = {
        "bool", "string", "char", "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64",
        "f32", "f64", "list", "option", "result", "tuple", "future", "stream", "own",
        "borrow"
    };
    uint32_t offset;
    uint32_t codepoint = 0u;
    uint32_t next_codepoint = 0u;

    if (!model || !out_token || search_offset >= limit_offset) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    offset = croft_editor_syntax_skip_whitespace(model, search_offset, limit_offset);
    if (offset >= limit_offset || !croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    out_token->start_offset = offset;
    out_token->end_offset = offset + 1u;
    out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PLAIN;

    if (offset + 1u < limit_offset && croft_editor_syntax_codepoint_at(model, offset + 1u, &next_codepoint)) {
        if (codepoint == '/' && next_codepoint == '/') {
            out_token->end_offset = croft_editor_syntax_scan_line_comment(model, offset, limit_offset);
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_COMMENT;
            return CROFT_EDITOR_OK;
        }
        if (codepoint == '-' && next_codepoint == '>') {
            out_token->end_offset = offset + 2u;
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION;
            return CROFT_EDITOR_OK;
        }
    }

    if (codepoint == '"') {
        int closed = 0;
        out_token->end_offset = croft_editor_syntax_scan_string(model, offset, limit_offset, '"', &closed);
        out_token->kind = closed ? CROFT_EDITOR_SYNTAX_TOKEN_STRING : CROFT_EDITOR_SYNTAX_TOKEN_INVALID;
        return CROFT_EDITOR_OK;
    }

    if (croft_editor_syntax_is_digit(codepoint)) {
        uint32_t number_end = croft_editor_syntax_scan_number(model, offset, limit_offset);
        if (number_end > offset) {
            out_token->end_offset = number_end;
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_NUMBER;
            return CROFT_EDITOR_OK;
        }
    }

    if (croft_editor_syntax_wit_is_punctuation(codepoint)) {
        out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION;
        return CROFT_EDITOR_OK;
    }

    if (codepoint == '%' || croft_editor_syntax_wit_is_identifier_start(codepoint)) {
        out_token->end_offset = croft_editor_syntax_scan_wit_identifier(model, offset, limit_offset);
        if (croft_editor_syntax_token_in_list(model,
                                              out_token->start_offset,
                                              out_token->end_offset,
                                              wit_keywords,
                                              sizeof(wit_keywords) / sizeof(wit_keywords[0]))) {
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD;
        } else if (croft_editor_syntax_token_in_list(model,
                                                     out_token->start_offset,
                                                     out_token->end_offset,
                                                     wit_types,
                                                     sizeof(wit_types) / sizeof(wit_types[0]))) {
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_TYPE;
        } else if (croft_editor_syntax_wit_identifier_is_property(model,
                                                                  out_token->end_offset,
                                                                  limit_offset)) {
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY;
        } else {
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PLAIN;
        }
        return CROFT_EDITOR_OK;
    }

    out_token->end_offset =
        croft_editor_syntax_scan_invalid(model, offset, limit_offset, croft_editor_syntax_wit_is_boundary);
    out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_INVALID;
    return CROFT_EDITOR_OK;
}

static int croft_editor_syntax_wat_is_symbol_start(uint32_t codepoint) {
    return croft_editor_syntax_is_ascii_alpha(codepoint)
        || codepoint == '_'
        || codepoint == '$';
}

static int croft_editor_syntax_wat_is_symbol_continue(uint32_t codepoint) {
    return croft_editor_syntax_is_ascii_alnum(codepoint)
        || codepoint == '_'
        || codepoint == '$'
        || codepoint == '.'
        || codepoint == '-'
        || codepoint == '!'
        || codepoint == '?'
        || codepoint == '/'
        || codepoint == '*'
        || codepoint == '+'
        || codepoint == '<'
        || codepoint == '>'
        || codepoint == '='
        || codepoint == ':';
}

static int croft_editor_syntax_wat_is_boundary(uint32_t codepoint) {
    return codepoint == '('
        || codepoint == ')'
        || codepoint == '"'
        || croft_editor_syntax_is_whitespace(codepoint);
}

static uint32_t croft_editor_syntax_scan_wat_symbol(const croft_editor_text_model* model,
                                                    uint32_t start_offset,
                                                    uint32_t limit_offset) {
    uint32_t offset = start_offset;
    uint32_t codepoint = 0u;

    while (offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        if (!croft_editor_syntax_wat_is_symbol_continue(codepoint)) {
            break;
        }
        offset++;
    }
    return offset;
}

static uint32_t croft_editor_syntax_scan_wat_block_comment(const croft_editor_text_model* model,
                                                           uint32_t start_offset,
                                                           uint32_t limit_offset,
                                                           int* out_closed) {
    uint32_t offset = start_offset + 2u;
    uint32_t codepoint = 0u;
    uint32_t next_codepoint = 0u;

    if (out_closed) {
        *out_closed = 0;
    }

    while (offset + 1u < limit_offset
            && croft_editor_syntax_codepoint_at(model, offset, &codepoint)
            && croft_editor_syntax_codepoint_at(model, offset + 1u, &next_codepoint)) {
        if (codepoint == ';' && next_codepoint == ')') {
            if (out_closed) {
                *out_closed = 1;
            }
            return offset + 2u;
        }
        offset++;
    }
    return limit_offset;
}

static int32_t croft_editor_syntax_wat_next_token(const croft_editor_text_model* model,
                                                  uint32_t search_offset,
                                                  uint32_t limit_offset,
                                                  croft_editor_syntax_token* out_token) {
    static const char* const wat_keywords[] = {
        "module", "import", "export", "func", "param", "result", "memory", "data",
        "table", "global", "local", "call", "if", "then", "else", "block", "loop",
        "mut", "elem", "type", "start"
    };
    static const char* const wat_types[] = {
        "i32", "i64", "f32", "f64", "funcref", "externref", "v128"
    };
    uint32_t offset;
    uint32_t codepoint = 0u;
    uint32_t next_codepoint = 0u;

    if (!model || !out_token || search_offset >= limit_offset) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    offset = croft_editor_syntax_skip_whitespace(model, search_offset, limit_offset);
    if (offset >= limit_offset || !croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    out_token->start_offset = offset;
    out_token->end_offset = offset + 1u;
    out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PLAIN;

    if (offset + 1u < limit_offset && croft_editor_syntax_codepoint_at(model, offset + 1u, &next_codepoint)) {
        if (codepoint == ';' && next_codepoint == ';') {
            out_token->end_offset = croft_editor_syntax_scan_line_comment(model, offset, limit_offset);
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_COMMENT;
            return CROFT_EDITOR_OK;
        }
        if (codepoint == '(' && next_codepoint == ';') {
            int closed = 0;
            out_token->end_offset = croft_editor_syntax_scan_wat_block_comment(model,
                                                                                offset,
                                                                                limit_offset,
                                                                                &closed);
            out_token->kind = closed ? CROFT_EDITOR_SYNTAX_TOKEN_COMMENT : CROFT_EDITOR_SYNTAX_TOKEN_INVALID;
            return CROFT_EDITOR_OK;
        }
    }

    if (codepoint == '"') {
        int closed = 0;
        out_token->end_offset = croft_editor_syntax_scan_string(model, offset, limit_offset, '"', &closed);
        out_token->kind = closed ? CROFT_EDITOR_SYNTAX_TOKEN_STRING : CROFT_EDITOR_SYNTAX_TOKEN_INVALID;
        return CROFT_EDITOR_OK;
    }

    if (codepoint == '(' || codepoint == ')') {
        out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION;
        return CROFT_EDITOR_OK;
    }

    if (codepoint == '-' || croft_editor_syntax_is_digit(codepoint)) {
        uint32_t number_end = croft_editor_syntax_scan_number(model, offset, limit_offset);
        if (number_end > offset) {
            out_token->end_offset = number_end;
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_NUMBER;
            return CROFT_EDITOR_OK;
        }
    }

    if (croft_editor_syntax_wat_is_symbol_start(codepoint)) {
        out_token->end_offset = croft_editor_syntax_scan_wat_symbol(model, offset, limit_offset);
        if (codepoint == '$') {
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY;
        } else if (croft_editor_syntax_token_in_list(model,
                                                     out_token->start_offset,
                                                     out_token->end_offset,
                                                     wat_types,
                                                     sizeof(wat_types) / sizeof(wat_types[0]))) {
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_TYPE;
        } else if (croft_editor_syntax_token_in_list(model,
                                                     out_token->start_offset,
                                                     out_token->end_offset,
                                                     wat_keywords,
                                                     sizeof(wat_keywords) / sizeof(wat_keywords[0]))
                || croft_editor_syntax_token_contains_char(model,
                                                           out_token->start_offset,
                                                           out_token->end_offset,
                                                           '.')) {
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD;
        } else {
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PLAIN;
        }
        return CROFT_EDITOR_OK;
    }

    out_token->end_offset =
        croft_editor_syntax_scan_invalid(model, offset, limit_offset, croft_editor_syntax_wat_is_boundary);
    out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_INVALID;
    return CROFT_EDITOR_OK;
}

static int croft_editor_syntax_is_line_leading_space_only(const croft_editor_text_model* model,
                                                          uint32_t offset) {
    uint32_t probe = offset;
    uint32_t codepoint = 0u;

    while (probe > 0u) {
        probe--;
        if (!croft_editor_syntax_codepoint_at(model, probe, &codepoint)) {
            return 0;
        }
        if (croft_editor_syntax_is_linebreak(codepoint)) {
            return 1;
        }
        if (codepoint != ' ' && codepoint != '\t') {
            return 0;
        }
    }
    return 1;
}

static uint32_t croft_editor_syntax_scan_until_char_or_linebreak(const croft_editor_text_model* model,
                                                                 uint32_t start_offset,
                                                                 uint32_t limit_offset,
                                                                 uint32_t needle,
                                                                 int* out_found) {
    uint32_t offset = start_offset;
    uint32_t codepoint = 0u;

    if (out_found) {
        *out_found = 0;
    }

    while (offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        if (croft_editor_syntax_is_linebreak(codepoint)) {
            break;
        }
        if (codepoint == needle) {
            if (out_found) {
                *out_found = 1;
            }
            return offset + 1u;
        }
        offset++;
    }
    return offset;
}

static int croft_editor_syntax_markdown_is_inline_punctuation(uint32_t codepoint) {
    return codepoint == '['
        || codepoint == ']'
        || codepoint == '('
        || codepoint == ')'
        || codepoint == '*'
        || codepoint == '_'
        || codepoint == '~'
        || codepoint == '!'
        || codepoint == '`'
        || codepoint == '#'
        || codepoint == '>'
        || codepoint == '-'
        || codepoint == '+';
}

static uint32_t croft_editor_syntax_scan_markdown_text(const croft_editor_text_model* model,
                                                       uint32_t start_offset,
                                                       uint32_t limit_offset) {
    uint32_t offset = start_offset;
    uint32_t codepoint = 0u;

    while (offset < limit_offset && croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        if (croft_editor_syntax_is_whitespace(codepoint)
                || croft_editor_syntax_markdown_is_inline_punctuation(codepoint)) {
            break;
        }
        offset++;
    }
    return offset;
}

static uint32_t croft_editor_syntax_scan_markdown_link_label(const croft_editor_text_model* model,
                                                             uint32_t start_offset,
                                                             uint32_t limit_offset,
                                                             int* out_closed) {
    return croft_editor_syntax_scan_until_char_or_linebreak(model,
                                                            start_offset + 1u,
                                                            limit_offset,
                                                            ']',
                                                            out_closed);
}

static int32_t croft_editor_syntax_markdown_next_token(const croft_editor_text_model* model,
                                                       uint32_t search_offset,
                                                       uint32_t limit_offset,
                                                       croft_editor_syntax_token* out_token) {
    uint32_t offset;
    uint32_t codepoint = 0u;
    uint32_t next_codepoint = 0u;
    int line_leading_space_only;

    if (!model || !out_token || search_offset >= limit_offset) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    offset = croft_editor_syntax_skip_whitespace(model, search_offset, limit_offset);
    if (offset >= limit_offset || !croft_editor_syntax_codepoint_at(model, offset, &codepoint)) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    line_leading_space_only = croft_editor_syntax_is_line_leading_space_only(model, offset);
    out_token->start_offset = offset;
    out_token->end_offset = offset + 1u;
    out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PLAIN;

    if (line_leading_space_only) {
        if (codepoint == '#') {
            out_token->end_offset = croft_editor_syntax_scan_line_comment(model, offset, limit_offset);
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD;
            return CROFT_EDITOR_OK;
        }
        if (codepoint == '>') {
            out_token->end_offset = croft_editor_syntax_scan_line_comment(model, offset, limit_offset);
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_COMMENT;
            return CROFT_EDITOR_OK;
        }
        if ((codepoint == '`' || codepoint == '~')
                && offset + 2u < limit_offset
                && croft_editor_syntax_codepoint_at(model, offset + 1u, &next_codepoint)
                && next_codepoint == codepoint) {
            uint32_t third_codepoint = 0u;
            if (croft_editor_syntax_codepoint_at(model, offset + 2u, &third_codepoint)
                    && third_codepoint == codepoint) {
                out_token->end_offset = croft_editor_syntax_scan_line_comment(model, offset, limit_offset);
                out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD;
                return CROFT_EDITOR_OK;
            }
        }
        if ((codepoint == '-' || codepoint == '+' || codepoint == '*')
                && offset + 1u < limit_offset
                && croft_editor_syntax_codepoint_at(model, offset + 1u, &next_codepoint)
                && croft_editor_syntax_is_whitespace(next_codepoint)) {
            out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION;
            return CROFT_EDITOR_OK;
        }
        if (croft_editor_syntax_is_digit(codepoint)) {
            uint32_t marker_end = offset;
            while (marker_end < limit_offset
                    && croft_editor_syntax_codepoint_at(model, marker_end, &next_codepoint)
                    && croft_editor_syntax_is_digit(next_codepoint)) {
                marker_end++;
            }
            if (marker_end + 1u < limit_offset
                    && croft_editor_syntax_codepoint_at(model, marker_end, &next_codepoint)
                    && next_codepoint == '.'
                    && croft_editor_syntax_codepoint_at(model, marker_end + 1u, &codepoint)
                    && croft_editor_syntax_is_whitespace(codepoint)) {
                out_token->end_offset = marker_end + 1u;
                out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION;
                return CROFT_EDITOR_OK;
            }
        }
    }

    if (codepoint == '`') {
        int closed = 0;
        out_token->end_offset =
            croft_editor_syntax_scan_until_char_or_linebreak(model, offset + 1u, limit_offset, '`', &closed);
        out_token->kind = closed ? CROFT_EDITOR_SYNTAX_TOKEN_STRING : CROFT_EDITOR_SYNTAX_TOKEN_INVALID;
        return CROFT_EDITOR_OK;
    }

    if (codepoint == '[') {
        int closed = 0;
        out_token->end_offset =
            croft_editor_syntax_scan_markdown_link_label(model, offset, limit_offset, &closed);
        out_token->kind = closed ? CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY : CROFT_EDITOR_SYNTAX_TOKEN_INVALID;
        return CROFT_EDITOR_OK;
    }

    if (codepoint == ']'
            || codepoint == '('
            || codepoint == ')'
            || codepoint == '*'
            || codepoint == '_'
            || codepoint == '~'
            || codepoint == '!') {
        out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION;
        return CROFT_EDITOR_OK;
    }

    out_token->end_offset = croft_editor_syntax_scan_markdown_text(model, offset, limit_offset);
    if (out_token->end_offset <= offset) {
        out_token->end_offset = offset + 1u;
        out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION;
    } else {
        out_token->kind = CROFT_EDITOR_SYNTAX_TOKEN_PLAIN;
    }
    return CROFT_EDITOR_OK;
}

croft_editor_syntax_language croft_editor_syntax_language_from_path(const char* path) {
    if (croft_editor_syntax_path_has_suffix(path, ".json")) {
        return CROFT_EDITOR_SYNTAX_LANGUAGE_JSON;
    }
    if (croft_editor_syntax_path_has_suffix(path, ".md")
            || croft_editor_syntax_path_has_suffix(path, ".markdown")) {
        return CROFT_EDITOR_SYNTAX_LANGUAGE_MARKDOWN;
    }
    if (croft_editor_syntax_path_has_suffix(path, ".lamb")
            || croft_editor_syntax_path_has_suffix(path, ".lambkin")) {
        return CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN;
    }
    if (croft_editor_syntax_path_has_suffix(path, ".wit")) {
        return CROFT_EDITOR_SYNTAX_LANGUAGE_WIT;
    }
    if (croft_editor_syntax_path_has_suffix(path, ".wat")
            || croft_editor_syntax_path_has_suffix(path, ".wast")) {
        return CROFT_EDITOR_SYNTAX_LANGUAGE_WAT;
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

    switch (language) {
        case CROFT_EDITOR_SYNTAX_LANGUAGE_JSON:
            return croft_editor_syntax_json_next_token(model, search_offset, limit_offset, out_token);
        case CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN:
            return croft_editor_syntax_lambkin_next_token(model, search_offset, limit_offset, out_token);
        case CROFT_EDITOR_SYNTAX_LANGUAGE_WIT:
            return croft_editor_syntax_wit_next_token(model, search_offset, limit_offset, out_token);
        case CROFT_EDITOR_SYNTAX_LANGUAGE_WAT:
            return croft_editor_syntax_wat_next_token(model, search_offset, limit_offset, out_token);
        case CROFT_EDITOR_SYNTAX_LANGUAGE_MARKDOWN:
            return croft_editor_syntax_markdown_next_token(model, search_offset, limit_offset, out_token);
        default:
            break;
    }
    return CROFT_EDITOR_ERR_INVALID;
}
