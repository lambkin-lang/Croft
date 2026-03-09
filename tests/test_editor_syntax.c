#include "croft/editor_syntax.h"

#include <stdio.h>
#include <string.h>

#define ASSERT_SYNTAX(cond)                                                \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "    ASSERT failed: %s (%s:%d)\n",             \
                    #cond, __FILE__, __LINE__);                            \
            return 1;                                                      \
        }                                                                  \
    } while (0)

static int assert_next_token(const croft_editor_text_model* model,
                             croft_editor_syntax_language language,
                             uint32_t* offset_io,
                             croft_editor_syntax_token_kind expected_kind,
                             const char* expected_text) {
    croft_editor_syntax_token token = {0};
    uint32_t start_byte;
    uint32_t end_byte;
    size_t expected_len = strlen(expected_text);

    ASSERT_SYNTAX(model);
    ASSERT_SYNTAX(offset_io);
    ASSERT_SYNTAX(croft_editor_syntax_next_token(model,
                                                 language,
                                                 *offset_io,
                                                 croft_editor_text_model_codepoint_length(model),
                                                 &token) == CROFT_EDITOR_OK);
    ASSERT_SYNTAX(token.kind == expected_kind);

    start_byte = croft_editor_text_model_byte_offset_at(model, token.start_offset);
    end_byte = croft_editor_text_model_byte_offset_at(model, token.end_offset);
    ASSERT_SYNTAX((size_t)(end_byte - start_byte) == expected_len);
    ASSERT_SYNTAX(memcmp(croft_editor_text_model_text(model) + start_byte,
                         expected_text,
                         expected_len) == 0);
    *offset_io = token.end_offset;
    return 0;
}

int test_editor_syntax_language_from_path(void) {
    ASSERT_SYNTAX(croft_editor_syntax_language_from_path(NULL)
                  == CROFT_EDITOR_SYNTAX_LANGUAGE_PLAIN_TEXT);
    ASSERT_SYNTAX(croft_editor_syntax_language_from_path("notes.txt")
                  == CROFT_EDITOR_SYNTAX_LANGUAGE_PLAIN_TEXT);
    ASSERT_SYNTAX(croft_editor_syntax_language_from_path("/tmp/settings.json")
                  == CROFT_EDITOR_SYNTAX_LANGUAGE_JSON);
    ASSERT_SYNTAX(croft_editor_syntax_language_from_path("/tmp/SETTINGS.JSON")
                  == CROFT_EDITOR_SYNTAX_LANGUAGE_JSON);
    return 0;
}

int test_editor_syntax_json_tokens(void) {
    const char* json = "{\"name\":\"croft\",\"count\":42,\"enabled\":true,\"none\":null}";
    croft_editor_text_model model;
    uint32_t offset = 0u;

    croft_editor_text_model_init(&model);
    ASSERT_SYNTAX(croft_editor_text_model_set_text(&model, json, strlen(json)) == CROFT_EDITOR_OK);

    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_JSON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "{") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_JSON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY, "\"name\"") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_JSON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ":") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_JSON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_STRING, "\"croft\"") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_JSON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ",") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_JSON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY, "\"count\"") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_JSON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ":") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_JSON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_NUMBER, "42") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_JSON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ",") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_JSON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY, "\"enabled\"") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_JSON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ":") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_JSON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "true") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_JSON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ",") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_JSON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY, "\"none\"") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_JSON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ":") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_JSON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "null") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_JSON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "}") == 0);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_syntax_invalid_inputs(void) {
    const char* json = "{ invalid: 1. }";
    croft_editor_text_model model;
    croft_editor_syntax_token token = {0};

    croft_editor_text_model_init(&model);
    ASSERT_SYNTAX(croft_editor_text_model_set_text(&model, json, strlen(json)) == CROFT_EDITOR_OK);

    ASSERT_SYNTAX(croft_editor_syntax_next_token(NULL,
                                                 CROFT_EDITOR_SYNTAX_LANGUAGE_JSON,
                                                 0u,
                                                 1u,
                                                 &token) == CROFT_EDITOR_ERR_INVALID);
    ASSERT_SYNTAX(croft_editor_syntax_next_token(&model,
                                                 CROFT_EDITOR_SYNTAX_LANGUAGE_PLAIN_TEXT,
                                                 0u,
                                                 croft_editor_text_model_codepoint_length(&model),
                                                 &token) == CROFT_EDITOR_ERR_INVALID);
    ASSERT_SYNTAX(croft_editor_syntax_next_token(&model,
                                                 CROFT_EDITOR_SYNTAX_LANGUAGE_JSON,
                                                 0u,
                                                 0u,
                                                 &token) == CROFT_EDITOR_ERR_INVALID);
    ASSERT_SYNTAX(croft_editor_syntax_next_token(&model,
                                                 CROFT_EDITOR_SYNTAX_LANGUAGE_JSON,
                                                 2u,
                                                 croft_editor_text_model_codepoint_length(&model),
                                                 &token) == CROFT_EDITOR_OK);
    ASSERT_SYNTAX(token.kind == CROFT_EDITOR_SYNTAX_TOKEN_INVALID);
    ASSERT_SYNTAX(token.end_offset > token.start_offset);

    croft_editor_text_model_dispose(&model);
    return 0;
}
