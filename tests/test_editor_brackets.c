#include "croft/editor_brackets.h"

#include <stdio.h>
#include <string.h>

#define ASSERT_BRACKETS(cond)                                              \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "    ASSERT failed: %s (%s:%d)\n",             \
                    #cond, __FILE__, __LINE__);                            \
            return 1;                                                      \
        }                                                                  \
    } while (0)

int test_editor_brackets_nested_pairs(void) {
    croft_editor_text_model model;
    croft_editor_bracket_match match = {0};

    croft_editor_text_model_init(&model);
    ASSERT_BRACKETS(croft_editor_text_model_set_text(&model,
                                                     "({[]})",
                                                     strlen("({[]})")) == CROFT_EDITOR_OK);

    ASSERT_BRACKETS(croft_editor_bracket_match_at_offset(&model, 0u, &match) == CROFT_EDITOR_OK);
    ASSERT_BRACKETS(match.open_offset == 0u);
    ASSERT_BRACKETS(match.close_offset == 5u);

    ASSERT_BRACKETS(croft_editor_bracket_match_at_offset(&model, 1u, &match) == CROFT_EDITOR_OK);
    ASSERT_BRACKETS(match.open_offset == 1u);
    ASSERT_BRACKETS(match.close_offset == 4u);

    ASSERT_BRACKETS(croft_editor_bracket_match_at_offset(&model, 2u, &match) == CROFT_EDITOR_OK);
    ASSERT_BRACKETS(match.open_offset == 2u);
    ASSERT_BRACKETS(match.close_offset == 3u);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_brackets_near_cursor(void) {
    croft_editor_text_model model;
    croft_editor_bracket_match match = {0};

    croft_editor_text_model_init(&model);
    ASSERT_BRACKETS(croft_editor_text_model_set_text(&model,
                                                     "if (a[0])",
                                                     strlen("if (a[0])")) == CROFT_EDITOR_OK);

    ASSERT_BRACKETS(croft_editor_bracket_match_near_offset(&model, 4u, &match) == CROFT_EDITOR_OK);
    ASSERT_BRACKETS(match.open_offset == 3u);
    ASSERT_BRACKETS(match.close_offset == 8u);

    ASSERT_BRACKETS(croft_editor_bracket_match_near_offset(&model, 6u, &match) == CROFT_EDITOR_OK);
    ASSERT_BRACKETS(match.open_offset == 5u);
    ASSERT_BRACKETS(match.close_offset == 7u);

    ASSERT_BRACKETS(croft_editor_bracket_match_near_offset(&model, 8u, &match) == CROFT_EDITOR_OK);
    ASSERT_BRACKETS(match.open_offset == 5u);
    ASSERT_BRACKETS(match.close_offset == 7u);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_brackets_unmatched_or_invalid(void) {
    croft_editor_text_model model;
    croft_editor_bracket_match match = {0};

    croft_editor_text_model_init(&model);
    ASSERT_BRACKETS(croft_editor_text_model_set_text(&model,
                                                     "alpha(beta",
                                                     strlen("alpha(beta")) == CROFT_EDITOR_OK);

    ASSERT_BRACKETS(croft_editor_bracket_match_at_offset(&model, 5u, &match) == CROFT_EDITOR_ERR_INVALID);
    ASSERT_BRACKETS(croft_editor_bracket_match_near_offset(&model, 0u, &match) == CROFT_EDITOR_ERR_INVALID);
    ASSERT_BRACKETS(croft_editor_bracket_match_at_offset(&model, 2u, &match) == CROFT_EDITOR_ERR_INVALID);

    croft_editor_text_model_dispose(&model);
    return 0;
}
