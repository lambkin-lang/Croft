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
    ASSERT_SYNTAX(croft_editor_syntax_language_from_path("/tmp/README.md")
                  == CROFT_EDITOR_SYNTAX_LANGUAGE_MARKDOWN);
    ASSERT_SYNTAX(croft_editor_syntax_language_from_path("/tmp/NOTES.MARKDOWN")
                  == CROFT_EDITOR_SYNTAX_LANGUAGE_MARKDOWN);
    ASSERT_SYNTAX(croft_editor_syntax_language_from_path("/tmp/tool.py")
                  == CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON);
    ASSERT_SYNTAX(croft_editor_syntax_language_from_path("/tmp/config.yaml")
                  == CROFT_EDITOR_SYNTAX_LANGUAGE_YAML);
    ASSERT_SYNTAX(croft_editor_syntax_language_from_path("/tmp/CONFIG.YML")
                  == CROFT_EDITOR_SYNTAX_LANGUAGE_YAML);
    ASSERT_SYNTAX(croft_editor_syntax_language_from_path("/tmp/prelude.lamb")
                  == CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN);
    ASSERT_SYNTAX(croft_editor_syntax_language_from_path("/tmp/PRELUDE.LAMBKIN")
                  == CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN);
    ASSERT_SYNTAX(croft_editor_syntax_language_from_path("/tmp/common-core.wit")
                  == CROFT_EDITOR_SYNTAX_LANGUAGE_WIT);
    ASSERT_SYNTAX(croft_editor_syntax_language_from_path("/tmp/dummy_guest.wat")
                  == CROFT_EDITOR_SYNTAX_LANGUAGE_WAT);
    ASSERT_SYNTAX(croft_editor_syntax_language_from_path("/tmp/dummy_guest.wast")
                  == CROFT_EDITOR_SYNTAX_LANGUAGE_WAT);
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

int test_editor_syntax_markdown_tokens(void) {
    const char* source =
        "# Heading\n"
        "- item with `code` and [link](https://example.com)\n"
        "> quote\n";
    croft_editor_text_model model;
    uint32_t offset = 0u;

    croft_editor_text_model_init(&model);
    ASSERT_SYNTAX(croft_editor_text_model_set_text(&model, source, strlen(source)) == CROFT_EDITOR_OK);

    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_MARKDOWN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "# Heading") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_MARKDOWN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "-") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_MARKDOWN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PLAIN, "item") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_MARKDOWN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PLAIN, "with") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_MARKDOWN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_STRING, "`code`") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_MARKDOWN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PLAIN, "and") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_MARKDOWN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY, "[link]") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_MARKDOWN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "(") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_MARKDOWN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PLAIN, "https://example.com") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_MARKDOWN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ")") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_MARKDOWN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_COMMENT, "> quote") == 0);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_syntax_python_tokens(void) {
    const char* source =
        "@cache\n"
        "class Greeter:\n"
        "    def greet(self, name: str) -> None:\n"
        "        # hi\n"
        "        return \"hello\" if name else None\n";
    croft_editor_text_model model;
    uint32_t offset = 0u;

    croft_editor_text_model_init(&model);
    ASSERT_SYNTAX(croft_editor_text_model_set_text(&model, source, strlen(source)) == CROFT_EDITOR_OK);

    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY, "@cache") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "class") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PLAIN, "Greeter") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ":") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "def") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PLAIN, "greet") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "(") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PLAIN, "self") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ",") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PLAIN, "name") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ":") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_TYPE, "str") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ")") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "->") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "None") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ":") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_COMMENT, "# hi") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "return") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_STRING, "\"hello\"") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "if") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PLAIN, "name") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "else") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_PYTHON, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "None") == 0);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_syntax_yaml_tokens(void) {
    const char* source =
        "---\n"
        "name: croft\n"
        "enabled: true\n"
        "count: 42\n"
        "items:\n"
        "  - first\n"
        "  - \"second\"\n"
        "# note\n"
        "...\n";
    croft_editor_text_model model;
    uint32_t offset = 0u;

    croft_editor_text_model_init(&model);
    ASSERT_SYNTAX(croft_editor_text_model_set_text(&model, source, strlen(source)) == CROFT_EDITOR_OK);

    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_YAML, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "---") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_YAML, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY, "name") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_YAML, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ":") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_YAML, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PLAIN, "croft") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_YAML, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY, "enabled") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_YAML, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ":") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_YAML, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "true") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_YAML, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY, "count") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_YAML, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ":") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_YAML, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_NUMBER, "42") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_YAML, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY, "items") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_YAML, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ":") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_YAML, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "-") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_YAML, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PLAIN, "first") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_YAML, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "-") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_YAML, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_STRING, "\"second\"") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_YAML, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_COMMENT, "# note") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_YAML, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "...") == 0);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_syntax_lambkin_tokens(void) {
    const char* source = "-- comment\nfunc (main) -> integer {\n  let value = -42_i64\n  claim true\n}\n";
    croft_editor_text_model model;
    uint32_t offset = 0u;

    croft_editor_text_model_init(&model);
    ASSERT_SYNTAX(croft_editor_text_model_set_text(&model, source, strlen(source)) == CROFT_EDITOR_OK);

    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_COMMENT, "-- comment") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "func") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "(") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PLAIN, "main") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ")") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "->") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_TYPE, "integer") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "{") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "let") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PLAIN, "value") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "=") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_NUMBER, "-42_i64") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "claim") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "true") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_LAMBKIN, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "}") == 0);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_syntax_wit_tokens(void) {
    const char* source =
        "package lambkin:common-core@0.1.0;\n"
        "interface types {\n"
        "  record text-open {\n"
        "    initial: list<u8>,\n"
        "  }\n"
        "}\n";
    croft_editor_text_model model;
    uint32_t offset = 0u;

    croft_editor_text_model_init(&model);
    ASSERT_SYNTAX(croft_editor_text_model_set_text(&model, source, strlen(source)) == CROFT_EDITOR_OK);

    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "package") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY, "lambkin") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ":") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PLAIN, "common-core") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "@") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_NUMBER, "0.1") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ".") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_NUMBER, "0") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ";") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "interface") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PLAIN, "types") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "{") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "record") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PLAIN, "text-open") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "{") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY, "initial") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ":") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_TYPE, "list") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "<") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_TYPE, "u8") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ">") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WIT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ",") == 0);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_syntax_wat_tokens(void) {
    const char* source =
        "(module\n"
        "  (import \"env\" \"host_log\" (func $host_log (param i32 i32) (result i32)))\n"
        "  ;; hello\n"
        "  (i32.add (local.get $arg) (i32.const 42))\n"
        ")\n";
    croft_editor_text_model model;
    uint32_t offset = 0u;

    croft_editor_text_model_init(&model);
    ASSERT_SYNTAX(croft_editor_text_model_set_text(&model, source, strlen(source)) == CROFT_EDITOR_OK);

    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "(") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "module") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "(") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "import") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_STRING, "\"env\"") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_STRING, "\"host_log\"") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "(") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "func") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY, "$host_log") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "(") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "param") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_TYPE, "i32") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_TYPE, "i32") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ")") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "(") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "result") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_TYPE, "i32") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ")") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ")") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ")") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_COMMENT, ";; hello") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "(") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "i32.add") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "(") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "local.get") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY, "$arg") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, ")") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION, "(") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD, "i32.const") == 0);
    ASSERT_SYNTAX(assert_next_token(&model, CROFT_EDITOR_SYNTAX_LANGUAGE_WAT, &offset,
                                    CROFT_EDITOR_SYNTAX_TOKEN_NUMBER, "42") == 0);

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
