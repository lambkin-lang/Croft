#include "croft/editor_search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_SEARCH(cond)                                                \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "    ASSERT failed: %s (%s:%d)\n",             \
                    #cond, __FILE__, __LINE__);                            \
            return 1;                                                      \
        }                                                                  \
    } while (0)

int test_editor_search_next_previous(void)
{
    croft_editor_text_model model;
    croft_editor_search_match match = {0};

    croft_editor_text_model_init(&model);
    ASSERT_SEARCH(croft_editor_text_model_set_text(&model,
                                                   "alpha beta alpha gamma alpha",
                                                   28u) == CROFT_EDITOR_OK);

    ASSERT_SEARCH(croft_editor_search_next(&model, "alpha", 5u, 0u, &match) == CROFT_EDITOR_OK);
    ASSERT_SEARCH(match.start_offset == 0u);
    ASSERT_SEARCH(match.end_offset == 5u);

    ASSERT_SEARCH(croft_editor_search_next(&model, "alpha", 5u, match.end_offset, &match) == CROFT_EDITOR_OK);
    ASSERT_SEARCH(match.start_offset == 11u);
    ASSERT_SEARCH(match.end_offset == 16u);

    ASSERT_SEARCH(croft_editor_search_previous(&model, "alpha", 5u, match.start_offset, &match)
                  == CROFT_EDITOR_OK);
    ASSERT_SEARCH(match.start_offset == 0u);
    ASSERT_SEARCH(match.end_offset == 5u);

    ASSERT_SEARCH(croft_editor_search_previous(&model, "alpha", 5u, 5u, &match)
                  == CROFT_EDITOR_OK);
    ASSERT_SEARCH(match.start_offset == 0u);
    ASSERT_SEARCH(match.end_offset == 5u);

    ASSERT_SEARCH(croft_editor_search_previous(&model, "alpha", 5u, 1u, &match)
                  == CROFT_EDITOR_ERR_INVALID);
    ASSERT_SEARCH(croft_editor_search_previous(&model, "alpha", 5u, 4u, &match)
                  == CROFT_EDITOR_ERR_INVALID);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_search_multibyte(void)
{
    croft_editor_text_model model;
    croft_editor_search_match match = {0};
    const char* text = "pi \xCF\x80\nbeta \xCE\xB2\npi \xCF\x80";

    croft_editor_text_model_init(&model);
    ASSERT_SEARCH(croft_editor_text_model_set_text(&model, text, strlen(text)) == CROFT_EDITOR_OK);

    ASSERT_SEARCH(croft_editor_search_next(&model, "\xCF\x80", 2u, 0u, &match) == CROFT_EDITOR_OK);
    ASSERT_SEARCH(match.start_offset == 3u);
    ASSERT_SEARCH(match.end_offset == 4u);

    ASSERT_SEARCH(croft_editor_search_next(&model, "\xCF\x80", 2u, match.end_offset, &match)
                  == CROFT_EDITOR_OK);
    ASSERT_SEARCH(match.start_offset == 15u);
    ASSERT_SEARCH(match.end_offset == 16u);

    ASSERT_SEARCH(croft_editor_search_previous(&model, "\xCF\x80", 2u, match.start_offset, &match)
                  == CROFT_EDITOR_OK);
    ASSERT_SEARCH(match.start_offset == 3u);
    ASSERT_SEARCH(match.end_offset == 4u);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_search_invalid_queries(void)
{
    croft_editor_text_model model;
    croft_editor_search_match match = {0};

    croft_editor_text_model_init(&model);
    ASSERT_SEARCH(croft_editor_text_model_set_text(&model, "alpha", 5u) == CROFT_EDITOR_OK);
    ASSERT_SEARCH(croft_editor_search_next(&model, "", 0u, 0u, &match) == CROFT_EDITOR_ERR_INVALID);
    ASSERT_SEARCH(croft_editor_search_next(&model, "z", 1u, 0u, &match) == CROFT_EDITOR_ERR_INVALID);
    ASSERT_SEARCH(croft_editor_search_previous(&model, "alpha", 5u, 0u, &match) == CROFT_EDITOR_ERR_INVALID);
    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_search_count_matches_non_overlapping(void)
{
    croft_editor_text_model model;
    uint32_t match_count = 0u;

    croft_editor_text_model_init(&model);
    ASSERT_SEARCH(croft_editor_text_model_set_text(&model, "aaaa", 4u) == CROFT_EDITOR_OK);

    ASSERT_SEARCH(croft_editor_search_count_matches(&model, "aa", 2u, &match_count)
                  == CROFT_EDITOR_OK);
    ASSERT_SEARCH(match_count == 2u);

    ASSERT_SEARCH(croft_editor_search_count_matches(&model, "aaaaa", 5u, &match_count)
                  == CROFT_EDITOR_OK);
    ASSERT_SEARCH(match_count == 0u);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_search_replace_all_utf8(void)
{
    croft_editor_text_model model;
    char* replaced = NULL;
    size_t replaced_len = 0u;
    uint32_t match_count = 0u;

    croft_editor_text_model_init(&model);
    ASSERT_SEARCH(croft_editor_text_model_set_text(&model, "alpha beta alpha", 16u)
                  == CROFT_EDITOR_OK);

    ASSERT_SEARCH(croft_editor_search_replace_all_utf8(&model,
                                                       "alpha",
                                                       5u,
                                                       "omega",
                                                       5u,
                                                       &replaced,
                                                       &replaced_len,
                                                       &match_count) == CROFT_EDITOR_OK);
    ASSERT_SEARCH(match_count == 2u);
    ASSERT_SEARCH(replaced != NULL);
    ASSERT_SEARCH(replaced_len == strlen("omega beta omega"));
    ASSERT_SEARCH(strcmp(replaced, "omega beta omega") == 0);
    free(replaced);
    replaced = NULL;

    ASSERT_SEARCH(croft_editor_search_replace_all_utf8(&model,
                                                       "missing",
                                                       7u,
                                                       "",
                                                       0u,
                                                       &replaced,
                                                       &replaced_len,
                                                       &match_count) == CROFT_EDITOR_OK);
    ASSERT_SEARCH(match_count == 0u);
    ASSERT_SEARCH(replaced != NULL);
    ASSERT_SEARCH(strcmp(replaced, "alpha beta alpha") == 0);
    free(replaced);

    croft_editor_text_model_dispose(&model);
    return 0;
}
