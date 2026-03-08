#include "croft/editor_search.h"

#include <stdio.h>
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

    ASSERT_SEARCH(croft_editor_search_previous(&model, "alpha", 5u, 1u, &match)
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
