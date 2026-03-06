#include "croft/editor_text_model.h"

#include <stdio.h>
#include <string.h>

#define ASSERT_EDITOR(cond)                                                \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "    ASSERT failed: %s (%s:%d)\n",             \
                    #cond, __FILE__, __LINE__);                            \
            return 1;                                                      \
        }                                                                  \
    } while (0)

int test_editor_text_model_offsets(void) {
    croft_editor_text_model model;
    uint32_t line_len_bytes = 0;
    const char* line_text;

    croft_editor_text_model_init(&model);
    ASSERT_EDITOR(croft_editor_text_model_set_text(&model, "alpha\nbeta\n", 11u) == CROFT_EDITOR_OK);
    ASSERT_EDITOR(croft_editor_text_model_line_count(&model) == 3u);
    ASSERT_EDITOR(croft_editor_text_model_codepoint_length(&model) == 11u);
    ASSERT_EDITOR(croft_editor_text_model_get_offset_at(&model, 1u, 1u) == 0u);
    ASSERT_EDITOR(croft_editor_text_model_get_offset_at(&model, 1u, 6u) == 5u);
    ASSERT_EDITOR(croft_editor_text_model_get_offset_at(&model, 2u, 1u) == 6u);
    ASSERT_EDITOR(croft_editor_text_model_get_offset_at(&model, 2u, 5u) == 10u);
    ASSERT_EDITOR(croft_editor_text_model_get_offset_at(&model, 3u, 1u) == 11u);

    ASSERT_EDITOR(croft_editor_text_model_get_position_at(&model, 0u).line_number == 1u);
    ASSERT_EDITOR(croft_editor_text_model_get_position_at(&model, 0u).column == 1u);
    ASSERT_EDITOR(croft_editor_text_model_get_position_at(&model, 5u).line_number == 1u);
    ASSERT_EDITOR(croft_editor_text_model_get_position_at(&model, 5u).column == 6u);
    ASSERT_EDITOR(croft_editor_text_model_get_position_at(&model, 6u).line_number == 2u);
    ASSERT_EDITOR(croft_editor_text_model_get_position_at(&model, 6u).column == 1u);
    ASSERT_EDITOR(croft_editor_text_model_get_position_at(&model, 11u).line_number == 3u);
    ASSERT_EDITOR(croft_editor_text_model_get_position_at(&model, 11u).column == 1u);

    line_text = croft_editor_text_model_line_utf8(&model, 2u, &line_len_bytes);
    ASSERT_EDITOR(line_len_bytes == 4u);
    ASSERT_EDITOR(memcmp(line_text, "beta", 4u) == 0);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_text_model_multibyte(void) {
    croft_editor_text_model model;
    uint32_t line_len_bytes = 0;
    const char* line_text;
    croft_editor_position position;

    croft_editor_text_model_init(&model);
    ASSERT_EDITOR(croft_editor_text_model_set_text(&model, "a\xCF\x80\n\xCE\xB2", 6u) == CROFT_EDITOR_OK);

    ASSERT_EDITOR(croft_editor_text_model_length(&model) == 6u);
    ASSERT_EDITOR(croft_editor_text_model_codepoint_length(&model) == 4u);
    ASSERT_EDITOR(croft_editor_text_model_line_count(&model) == 2u);
    ASSERT_EDITOR(croft_editor_text_model_get_offset_at(&model, 1u, 3u) == 2u);
    ASSERT_EDITOR(croft_editor_text_model_get_offset_at(&model, 2u, 2u) == 4u);
    ASSERT_EDITOR(croft_editor_text_model_byte_offset_at(&model, 0u) == 0u);
    ASSERT_EDITOR(croft_editor_text_model_byte_offset_at(&model, 1u) == 1u);
    ASSERT_EDITOR(croft_editor_text_model_byte_offset_at(&model, 2u) == 3u);
    ASSERT_EDITOR(croft_editor_text_model_byte_offset_at(&model, 3u) == 4u);
    ASSERT_EDITOR(croft_editor_text_model_byte_offset_at(&model, 4u) == 6u);

    position = croft_editor_text_model_get_position_at(&model, 3u);
    ASSERT_EDITOR(position.line_number == 2u);
    ASSERT_EDITOR(position.column == 1u);

    line_text = croft_editor_text_model_line_utf8(&model, 1u, &line_len_bytes);
    ASSERT_EDITOR(line_len_bytes == 3u);
    ASSERT_EDITOR(memcmp(line_text, "a\xCF\x80", 3u) == 0);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_text_model_selection(void) {
    croft_editor_text_model model;
    croft_editor_selection selection;
    croft_editor_range range;
    uint32_t anchor;
    uint32_t active;

    croft_editor_text_model_init(&model);
    ASSERT_EDITOR(croft_editor_text_model_set_text(&model, "one\ntwo\nthree", 13u) == CROFT_EDITOR_OK);

    anchor = croft_editor_text_model_get_offset_at(&model, 2u, 3u);
    active = croft_editor_text_model_get_offset_at(&model, 1u, 2u);
    selection = croft_editor_selection_from_offsets(&model, anchor, active);
    ASSERT_EDITOR(croft_editor_selection_direction_of(selection) == CROFT_EDITOR_SELECTION_RTL);

    range = croft_editor_selection_normalized_range(selection);
    ASSERT_EDITOR(range.start_line_number == 1u);
    ASSERT_EDITOR(range.start_column == 2u);
    ASSERT_EDITOR(range.end_line_number == 2u);
    ASSERT_EDITOR(range.end_column == 3u);
    ASSERT_EDITOR(!croft_editor_range_is_empty(range));
    ASSERT_EDITOR(croft_editor_range_contains_position(
        range,
        croft_editor_position_create(2u, 2u)
    ));

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_text_model_word_ranges(void) {
    croft_editor_text_model model;
    croft_editor_range range;

    croft_editor_text_model_init(&model);
    ASSERT_EDITOR(croft_editor_text_model_set_text(&model, "alpha beta.gamma", 16u) == CROFT_EDITOR_OK);

    ASSERT_EDITOR(croft_editor_text_model_get_word_range_at(
        &model,
        croft_editor_position_create(1u, 8u),
        NULL,
        &range
    ));
    ASSERT_EDITOR(range.start_line_number == 1u);
    ASSERT_EDITOR(range.start_column == 7u);
    ASSERT_EDITOR(range.end_line_number == 1u);
    ASSERT_EDITOR(range.end_column == 11u);

    ASSERT_EDITOR(croft_editor_text_model_get_word_range_at(
        &model,
        croft_editor_position_create(1u, 11u),
        NULL,
        &range
    ));
    ASSERT_EDITOR(range.start_column == 7u);
    ASSERT_EDITOR(range.end_column == 11u);

    ASSERT_EDITOR(croft_editor_text_model_get_word_range_at(
        &model,
        croft_editor_position_create(1u, 6u),
        NULL,
        &range
    ));
    ASSERT_EDITOR(range.start_column == 1u);
    ASSERT_EDITOR(range.end_column == 6u);

    croft_editor_text_model_dispose(&model);
    return 0;
}
