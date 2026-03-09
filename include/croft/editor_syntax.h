#ifndef CROFT_EDITOR_SYNTAX_H
#define CROFT_EDITOR_SYNTAX_H

#include "croft/editor_text_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum croft_editor_syntax_language {
    CROFT_EDITOR_SYNTAX_LANGUAGE_PLAIN_TEXT = 0,
    CROFT_EDITOR_SYNTAX_LANGUAGE_JSON = 1,
    CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN = 2,
    CROFT_EDITOR_SYNTAX_LANGUAGE_WIT = 3,
    CROFT_EDITOR_SYNTAX_LANGUAGE_WAT = 4,
    CROFT_EDITOR_SYNTAX_LANGUAGE_MARKDOWN = 5,
    CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON = 6,
    CROFT_EDITOR_SYNTAX_LANGUAGE_YAML = 7,
    CROFT_EDITOR_SYNTAX_LANGUAGE_JAVASCRIPT = 8
} croft_editor_syntax_language;

typedef enum croft_editor_syntax_token_kind {
    CROFT_EDITOR_SYNTAX_TOKEN_PLAIN = 0,
    CROFT_EDITOR_SYNTAX_TOKEN_COMMENT = 1,
    CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY = 2,
    CROFT_EDITOR_SYNTAX_TOKEN_STRING = 3,
    CROFT_EDITOR_SYNTAX_TOKEN_NUMBER = 4,
    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD = 5,
    CROFT_EDITOR_SYNTAX_TOKEN_TYPE = 6,
    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION = 7,
    CROFT_EDITOR_SYNTAX_TOKEN_INVALID = 8
} croft_editor_syntax_token_kind;

typedef struct croft_editor_syntax_token {
    uint32_t start_offset;
    uint32_t end_offset;
    croft_editor_syntax_token_kind kind;
} croft_editor_syntax_token;

croft_editor_syntax_language croft_editor_syntax_language_from_path(const char* path);

int32_t croft_editor_syntax_next_token(const croft_editor_text_model* model,
                                       croft_editor_syntax_language language,
                                       uint32_t search_offset,
                                       uint32_t limit_offset,
                                       croft_editor_syntax_token* out_token);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_EDITOR_SYNTAX_H */
