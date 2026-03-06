#include "croft/editor_commands.h"

#include <stdio.h>
#include <string.h>

#define ASSERT_COMMAND(cond)                                               \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "    ASSERT failed: %s (%s:%d)\n",             \
                    #cond, __FILE__, __LINE__);                            \
            return 1;                                                      \
        }                                                                  \
    } while (0)

int test_editor_commands_word_moves(void) {
    croft_editor_text_model model;
    uint32_t anchor = 0;
    uint32_t active = 0;
    uint32_t preferred = 0;

    croft_editor_text_model_init(&model);
    ASSERT_COMMAND(croft_editor_text_model_set_text(&model, "alpha beta.gamma", 16u) == CROFT_EDITOR_OK);

    anchor = active = 16u;
    croft_editor_command_move_word_left(&model, &anchor, &active, &preferred, 0);
    ASSERT_COMMAND(anchor == 11u && active == 11u);

    croft_editor_command_move_word_left(&model, &anchor, &active, &preferred, 0);
    ASSERT_COMMAND(anchor == 6u && active == 6u);

    anchor = active = 6u;
    croft_editor_command_move_word_right(&model, &anchor, &active, &preferred, 0);
    ASSERT_COMMAND(anchor == 10u && active == 10u);

    croft_editor_command_move_word_right(&model, &anchor, &active, &preferred, 0);
    ASSERT_COMMAND(anchor == 16u && active == 16u);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_commands_word_deletes(void) {
    croft_editor_text_model model;
    uint32_t start = 0;
    uint32_t end = 0;

    croft_editor_text_model_init(&model);
    ASSERT_COMMAND(croft_editor_text_model_set_text(&model, "alpha beta.gamma", 16u) == CROFT_EDITOR_OK);

    ASSERT_COMMAND(croft_editor_command_delete_word_left_range(&model, 6u, 6u, &start, &end));
    ASSERT_COMMAND(start == 0u && end == 6u);

    ASSERT_COMMAND(croft_editor_command_delete_word_right_range(&model, 10u, 10u, &start, &end));
    ASSERT_COMMAND(start == 10u && end == 16u);

    ASSERT_COMMAND(croft_editor_command_delete_word_left_range(&model, 3u, 8u, &start, &end));
    ASSERT_COMMAND(start == 3u && end == 8u);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_commands_vertical_column_memory(void) {
    croft_editor_text_model model;
    uint32_t anchor = 0;
    uint32_t active = 0;
    uint32_t preferred = 0;
    croft_editor_position position;

    croft_editor_text_model_init(&model);
    ASSERT_COMMAND(croft_editor_text_model_set_text(&model, "abcde\nx\nwxyz", 12u) == CROFT_EDITOR_OK);

    anchor = active = croft_editor_text_model_get_offset_at(&model, 3u, 4u);
    croft_editor_command_move_up(&model, &anchor, &active, &preferred, 0);
    position = croft_editor_text_model_get_position_at(&model, active);
    ASSERT_COMMAND(preferred == 4u);
    ASSERT_COMMAND(position.line_number == 2u);
    ASSERT_COMMAND(position.column == 2u);

    croft_editor_command_move_up(&model, &anchor, &active, &preferred, 0);
    position = croft_editor_text_model_get_position_at(&model, active);
    ASSERT_COMMAND(position.line_number == 1u);
    ASSERT_COMMAND(position.column == 4u);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_commands_shift_word_selection(void) {
    croft_editor_text_model model;
    uint32_t anchor = 0;
    uint32_t active = 0;
    uint32_t preferred = 0;
    croft_editor_selection selection;
    croft_editor_range range;

    croft_editor_text_model_init(&model);
    ASSERT_COMMAND(croft_editor_text_model_set_text(&model, "alpha beta.gamma", 16u) == CROFT_EDITOR_OK);

    anchor = active = 6u;
    croft_editor_command_move_word_right(&model, &anchor, &active, &preferred, 1);
    selection = croft_editor_selection_from_offsets(&model, anchor, active);
    range = croft_editor_selection_normalized_range(selection);
    ASSERT_COMMAND(anchor == 6u);
    ASSERT_COMMAND(active == 10u);
    ASSERT_COMMAND(range.start_column == 7u);
    ASSERT_COMMAND(range.end_column == 11u);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_commands_word_part_moves(void) {
    croft_editor_text_model model;
    uint32_t anchor = 0;
    uint32_t active = 0;
    uint32_t preferred = 0;

    croft_editor_text_model_init(&model);

    ASSERT_COMMAND(croft_editor_text_model_set_text(&model,
                                                    "fooBarBaz",
                                                    strlen("fooBarBaz")) == CROFT_EDITOR_OK);
    anchor = active = 9u;
    croft_editor_command_move_word_part_left(&model, &anchor, &active, &preferred, 0);
    ASSERT_COMMAND(anchor == 6u && active == 6u);
    croft_editor_command_move_word_part_left(&model, &anchor, &active, &preferred, 0);
    ASSERT_COMMAND(anchor == 3u && active == 3u);
    croft_editor_command_move_word_part_left(&model, &anchor, &active, &preferred, 0);
    ASSERT_COMMAND(anchor == 0u && active == 0u);

    croft_editor_command_move_word_part_right(&model, &anchor, &active, &preferred, 0);
    ASSERT_COMMAND(anchor == 3u && active == 3u);
    croft_editor_command_move_word_part_right(&model, &anchor, &active, &preferred, 0);
    ASSERT_COMMAND(anchor == 6u && active == 6u);
    croft_editor_command_move_word_part_right(&model, &anchor, &active, &preferred, 0);
    ASSERT_COMMAND(anchor == 9u && active == 9u);

    ASSERT_COMMAND(croft_editor_text_model_set_text(&model,
                                                    "foo_bar",
                                                    strlen("foo_bar")) == CROFT_EDITOR_OK);
    anchor = active = 0u;
    croft_editor_command_move_word_part_right(&model, &anchor, &active, &preferred, 0);
    ASSERT_COMMAND(anchor == 3u && active == 3u);
    croft_editor_command_move_word_part_right(&model, &anchor, &active, &preferred, 0);
    ASSERT_COMMAND(anchor == 7u && active == 7u);
    croft_editor_command_move_word_part_left(&model, &anchor, &active, &preferred, 0);
    ASSERT_COMMAND(anchor == 4u && active == 4u);
    croft_editor_command_move_word_part_left(&model, &anchor, &active, &preferred, 0);
    ASSERT_COMMAND(anchor == 0u && active == 0u);

    ASSERT_COMMAND(croft_editor_text_model_set_text(&model,
                                                    "demonstration     of",
                                                    strlen("demonstration     of")) == CROFT_EDITOR_OK);
    anchor = active = 0u;
    croft_editor_command_move_word_part_right(&model, &anchor, &active, &preferred, 0);
    ASSERT_COMMAND(anchor == 13u && active == 13u);
    croft_editor_command_move_word_part_right(&model, &anchor, &active, &preferred, 0);
    ASSERT_COMMAND(anchor == 18u && active == 18u);
    croft_editor_command_move_word_part_left(&model, &anchor, &active, &preferred, 0);
    ASSERT_COMMAND(anchor == 13u && active == 13u);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_commands_word_part_deletes(void) {
    croft_editor_text_model model;
    uint32_t start = 0;
    uint32_t end = 0;

    croft_editor_text_model_init(&model);

    ASSERT_COMMAND(croft_editor_text_model_set_text(&model,
                                                    "fooBarBaz",
                                                    strlen("fooBarBaz")) == CROFT_EDITOR_OK);
    ASSERT_COMMAND(croft_editor_command_delete_word_part_left_range(&model, 9u, 9u, &start, &end));
    ASSERT_COMMAND(start == 6u && end == 9u);
    ASSERT_COMMAND(croft_editor_command_delete_word_part_right_range(&model, 0u, 0u, &start, &end));
    ASSERT_COMMAND(start == 0u && end == 3u);

    ASSERT_COMMAND(croft_editor_text_model_set_text(&model,
                                                    "foo_bar",
                                                    strlen("foo_bar")) == CROFT_EDITOR_OK);
    ASSERT_COMMAND(croft_editor_command_delete_word_part_left_range(&model, 7u, 7u, &start, &end));
    ASSERT_COMMAND(start == 4u && end == 7u);
    ASSERT_COMMAND(croft_editor_command_delete_word_part_left_range(&model, 4u, 4u, &start, &end));
    ASSERT_COMMAND(start == 0u && end == 4u);
    ASSERT_COMMAND(croft_editor_command_delete_word_part_right_range(&model, 0u, 0u, &start, &end));
    ASSERT_COMMAND(start == 0u && end == 3u);

    ASSERT_COMMAND(croft_editor_text_model_set_text(&model,
                                                    "demonstration     of",
                                                    strlen("demonstration     of")) == CROFT_EDITOR_OK);
    ASSERT_COMMAND(croft_editor_command_delete_word_part_right_range(&model, 13u, 13u, &start, &end));
    ASSERT_COMMAND(start == 13u && end == 18u);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_commands_shift_word_part_selection(void) {
    croft_editor_text_model model;
    uint32_t anchor = 0;
    uint32_t active = 0;
    uint32_t preferred = 0;
    croft_editor_selection selection;
    croft_editor_range range;

    croft_editor_text_model_init(&model);
    ASSERT_COMMAND(croft_editor_text_model_set_text(&model,
                                                    "fooBar",
                                                    strlen("fooBar")) == CROFT_EDITOR_OK);

    anchor = active = 0u;
    croft_editor_command_move_word_part_right(&model, &anchor, &active, &preferred, 1);
    selection = croft_editor_selection_from_offsets(&model, anchor, active);
    range = croft_editor_selection_normalized_range(selection);
    ASSERT_COMMAND(anchor == 0u);
    ASSERT_COMMAND(active == 3u);
    ASSERT_COMMAND(range.start_column == 1u);
    ASSERT_COMMAND(range.end_column == 4u);

    croft_editor_text_model_dispose(&model);
    return 0;
}
