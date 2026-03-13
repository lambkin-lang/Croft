#include "croft/editor_line_cache.h"

#include <stdio.h>
#include <string.h>

#define ASSERT_LINE_CACHE(cond)                                         \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "    ASSERT failed: %s (%s:%d)\n",          \
                    #cond, __FILE__, __LINE__);                         \
            return 1;                                                   \
        }                                                               \
    } while (0)

int test_editor_line_cache_incremental_invalidation(void)
{
    const char* initial =
        "{\n"
        "  \"alpha\": 1,\n"
        "  \"beta\": 2\n"
        "}\n";
    const char* updated =
        "{\n"
        "  \"alpha\": 1,\n"
        "  \"gamma\": 2\n"
        "}\n";
    croft_editor_line_cache cache;
    croft_editor_tab_settings settings;
    croft_editor_text_model old_model;
    croft_editor_text_model new_model;
    const croft_editor_syntax_token* tokens = NULL;
    uint32_t token_count = 0u;

    croft_editor_line_cache_init(&cache);
    croft_editor_tab_settings_default(&settings);
    croft_editor_text_model_init(&old_model);
    croft_editor_text_model_init(&new_model);

    ASSERT_LINE_CACHE(croft_editor_text_model_set_text(&old_model, initial, strlen(initial))
                      == CROFT_EDITOR_OK);
    ASSERT_LINE_CACHE(croft_editor_line_cache_sync(&cache,
                                                   NULL,
                                                   &old_model,
                                                   CROFT_EDITOR_SYNTAX_LANGUAGE_JSON,
                                                   &settings) == CROFT_EDITOR_OK);
    ASSERT_LINE_CACHE(croft_editor_line_cache_tokens_for_line(&cache,
                                                              &old_model,
                                                              1u,
                                                              &tokens,
                                                              &token_count) == CROFT_EDITOR_OK);
    ASSERT_LINE_CACHE(croft_editor_line_cache_tokens_for_line(&cache,
                                                              &old_model,
                                                              2u,
                                                              &tokens,
                                                              &token_count) == CROFT_EDITOR_OK);
    ASSERT_LINE_CACHE(croft_editor_line_cache_tokens_for_line(&cache,
                                                              &old_model,
                                                              3u,
                                                              &tokens,
                                                              &token_count) == CROFT_EDITOR_OK);
    ASSERT_LINE_CACHE(cache.lines[0].tokens_valid == 1u);
    ASSERT_LINE_CACHE(cache.lines[1].tokens_valid == 1u);
    ASSERT_LINE_CACHE(cache.lines[2].tokens_valid == 1u);

    ASSERT_LINE_CACHE(croft_editor_text_model_set_text(&new_model, updated, strlen(updated))
                      == CROFT_EDITOR_OK);
    ASSERT_LINE_CACHE(croft_editor_line_cache_sync(&cache,
                                                   &old_model,
                                                   &new_model,
                                                   CROFT_EDITOR_SYNTAX_LANGUAGE_JSON,
                                                   &settings) == CROFT_EDITOR_OK);
    ASSERT_LINE_CACHE(cache.lines[0].tokens_valid == 1u);
    ASSERT_LINE_CACHE(cache.lines[1].tokens_valid == 1u);
    ASSERT_LINE_CACHE(cache.lines[2].tokens_valid == 0u);
    ASSERT_LINE_CACHE(cache.dirty_from_line == 3u);
    ASSERT_LINE_CACHE(croft_editor_line_cache_tokens_for_line(&cache,
                                                              &new_model,
                                                              3u,
                                                              &tokens,
                                                              &token_count) == CROFT_EDITOR_OK);
    ASSERT_LINE_CACHE(cache.lines[2].tokens_valid == 1u);
    ASSERT_LINE_CACHE(token_count > 0u);

    croft_editor_text_model_dispose(&old_model);
    croft_editor_text_model_dispose(&new_model);
    croft_editor_line_cache_dispose(&cache);
    return 0;
}
