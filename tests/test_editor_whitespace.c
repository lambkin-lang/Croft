#include "croft/editor_whitespace.h"

#include <stdio.h>
#include <string.h>

#define ASSERT_WHITESPACE(cond)                                           \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "    ASSERT failed: %s (%s:%d)\n",            \
                    #cond, __FILE__, __LINE__);                           \
            return 1;                                                     \
        }                                                                 \
    } while (0)

int test_editor_whitespace_describe_lines(void) {
    croft_editor_text_model model;
    croft_editor_tab_settings settings;
    croft_editor_whitespace_line line = {0};

    croft_editor_text_model_init(&model);
    croft_editor_tab_settings_default(&settings);
    ASSERT_WHITESPACE(croft_editor_text_model_set_text(&model,
                                                       "\t  alpha\nbeta gamma\n\t\t",
                                                       strlen("\t  alpha\nbeta gamma\n\t\t"))
                      == CROFT_EDITOR_OK);

    ASSERT_WHITESPACE(croft_editor_whitespace_describe_line(&model, 1u, &settings, &line)
                      == CROFT_EDITOR_OK);
    ASSERT_WHITESPACE(line.line_start_offset == 0u);
    ASSERT_WHITESPACE(line.line_end_offset == 8u);
    ASSERT_WHITESPACE(line.leading_indent_columns == 6u);
    ASSERT_WHITESPACE(line.indent_guide_count == 1u);

    ASSERT_WHITESPACE(croft_editor_whitespace_describe_line(&model, 2u, &settings, &line)
                      == CROFT_EDITOR_OK);
    ASSERT_WHITESPACE(line.line_start_offset == 9u);
    ASSERT_WHITESPACE(line.line_end_offset == 19u);
    ASSERT_WHITESPACE(line.leading_indent_columns == 0u);
    ASSERT_WHITESPACE(line.indent_guide_count == 0u);

    ASSERT_WHITESPACE(croft_editor_whitespace_describe_line(&model, 3u, &settings, &line)
                      == CROFT_EDITOR_OK);
    ASSERT_WHITESPACE(line.line_start_offset == 20u);
    ASSERT_WHITESPACE(line.line_end_offset == 22u);
    ASSERT_WHITESPACE(line.leading_indent_columns == 8u);
    ASSERT_WHITESPACE(line.indent_guide_count == 2u);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_whitespace_markers(void) {
    croft_editor_text_model model;
    croft_editor_tab_settings settings;
    croft_editor_visible_whitespace marker = {0};

    croft_editor_text_model_init(&model);
    croft_editor_tab_settings_default(&settings);
    ASSERT_WHITESPACE(croft_editor_text_model_set_text(&model,
                                                       "\t  alpha\nbeta gamma\n\t\t",
                                                       strlen("\t  alpha\nbeta gamma\n\t\t"))
                      == CROFT_EDITOR_OK);

    ASSERT_WHITESPACE(croft_editor_whitespace_find_in_line(&model, 1u, &settings, 0u, &marker)
                      == CROFT_EDITOR_OK);
    ASSERT_WHITESPACE(marker.offset == 0u);
    ASSERT_WHITESPACE(marker.visual_column == 1u);
    ASSERT_WHITESPACE(marker.visual_width == 4u);
    ASSERT_WHITESPACE(marker.kind == CROFT_EDITOR_VISIBLE_WHITESPACE_TAB);

    ASSERT_WHITESPACE(croft_editor_whitespace_find_in_line(&model, 1u, &settings, 1u, &marker)
                      == CROFT_EDITOR_OK);
    ASSERT_WHITESPACE(marker.offset == 1u);
    ASSERT_WHITESPACE(marker.visual_column == 5u);
    ASSERT_WHITESPACE(marker.visual_width == 1u);
    ASSERT_WHITESPACE(marker.kind == CROFT_EDITOR_VISIBLE_WHITESPACE_SPACE);

    ASSERT_WHITESPACE(croft_editor_whitespace_find_in_line(&model, 2u, &settings, 9u, &marker)
                      == CROFT_EDITOR_OK);
    ASSERT_WHITESPACE(marker.offset == 13u);
    ASSERT_WHITESPACE(marker.visual_column == 5u);
    ASSERT_WHITESPACE(marker.visual_width == 1u);
    ASSERT_WHITESPACE(marker.kind == CROFT_EDITOR_VISIBLE_WHITESPACE_SPACE);

    ASSERT_WHITESPACE(croft_editor_whitespace_find_in_line(&model, 3u, &settings, 21u, &marker)
                      == CROFT_EDITOR_OK);
    ASSERT_WHITESPACE(marker.offset == 21u);
    ASSERT_WHITESPACE(marker.visual_column == 5u);
    ASSERT_WHITESPACE(marker.visual_width == 4u);
    ASSERT_WHITESPACE(marker.kind == CROFT_EDITOR_VISIBLE_WHITESPACE_TAB);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_whitespace_invalid_inputs(void) {
    croft_editor_text_model model;
    croft_editor_tab_settings settings;
    croft_editor_visible_whitespace marker = {0};
    croft_editor_whitespace_line line = {0};

    croft_editor_text_model_init(&model);
    croft_editor_tab_settings_default(&settings);
    ASSERT_WHITESPACE(croft_editor_text_model_set_text(&model,
                                                       "alpha",
                                                       strlen("alpha")) == CROFT_EDITOR_OK);

    ASSERT_WHITESPACE(croft_editor_whitespace_describe_line(NULL, 1u, &settings, &line)
                      == CROFT_EDITOR_ERR_INVALID);
    ASSERT_WHITESPACE(croft_editor_whitespace_find_in_line(&model, 1u, &settings, 6u, &marker)
                      == CROFT_EDITOR_ERR_INVALID);
    ASSERT_WHITESPACE(croft_editor_whitespace_find_in_line(&model, 1u, &settings, 0u, &marker)
                      == CROFT_EDITOR_ERR_INVALID);

    croft_editor_text_model_dispose(&model);
    return 0;
}
