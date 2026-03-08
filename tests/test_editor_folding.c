#include "croft/editor_folding.h"

#include <stdio.h>
#include <string.h>

#define ASSERT_FOLDING(cond)                                              \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "    ASSERT failed: %s (%s:%d)\n",            \
                    #cond, __FILE__, __LINE__);                           \
            return 1;                                                     \
        }                                                                 \
    } while (0)

int test_editor_folding_nested_regions(void)
{
    croft_editor_text_model model;
    croft_editor_tab_settings settings;
    croft_editor_fold_region region = {0};
    const char* source =
        "root\n"
        "    child\n"
        "        grand\n"
        "    sibling\n"
        "after\n";

    croft_editor_text_model_init(&model);
    croft_editor_tab_settings_default(&settings);
    ASSERT_FOLDING(croft_editor_text_model_set_text(&model, source, strlen(source)) == CROFT_EDITOR_OK);

    ASSERT_FOLDING(croft_editor_fold_region_for_line(&model, 1u, &settings, &region) == CROFT_EDITOR_OK);
    ASSERT_FOLDING(region.start_line_number == 1u);
    ASSERT_FOLDING(region.end_line_number == 4u);
    ASSERT_FOLDING(region.start_offset == 0u);
    ASSERT_FOLDING(region.body_start_offset == croft_editor_text_model_line_start_offset(&model, 2u));
    ASSERT_FOLDING(region.end_offset == croft_editor_text_model_line_end_offset(&model, 4u));
    ASSERT_FOLDING(region.header_indent_columns == 0u);
    ASSERT_FOLDING(region.body_indent_columns == 4u);

    ASSERT_FOLDING(croft_editor_fold_region_for_line(&model, 2u, &settings, &region) == CROFT_EDITOR_OK);
    ASSERT_FOLDING(region.start_line_number == 2u);
    ASSERT_FOLDING(region.end_line_number == 3u);
    ASSERT_FOLDING(region.header_indent_columns == 4u);
    ASSERT_FOLDING(region.body_indent_columns == 8u);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_folding_blank_lines_and_tabs(void)
{
    croft_editor_text_model model;
    croft_editor_tab_settings settings;
    croft_editor_fold_region region = {0};
    const char* source =
        "root\n"
        "\tbranch\n"
        "\n"
        "\tleaf\n"
        "tail\n";

    croft_editor_text_model_init(&model);
    croft_editor_tab_settings_default(&settings);
    ASSERT_FOLDING(croft_editor_text_model_set_text(&model, source, strlen(source)) == CROFT_EDITOR_OK);

    ASSERT_FOLDING(croft_editor_fold_region_for_line(&model, 1u, &settings, &region) == CROFT_EDITOR_OK);
    ASSERT_FOLDING(region.end_line_number == 4u);
    ASSERT_FOLDING(region.body_indent_columns == 4u);

    croft_editor_text_model_dispose(&model);
    return 0;
}

int test_editor_folding_invalid_lines(void)
{
    croft_editor_text_model model;
    croft_editor_tab_settings settings;
    croft_editor_fold_region region = {0};
    const char* source =
        "root\n"
        "    child\n"
        "flat\n";

    croft_editor_text_model_init(&model);
    croft_editor_tab_settings_default(&settings);
    ASSERT_FOLDING(croft_editor_text_model_set_text(&model, source, strlen(source)) == CROFT_EDITOR_OK);

    ASSERT_FOLDING(croft_editor_fold_region_for_line(NULL, 1u, &settings, &region)
                   == CROFT_EDITOR_ERR_INVALID);
    ASSERT_FOLDING(croft_editor_fold_region_for_line(&model, 0u, &settings, &region)
                   == CROFT_EDITOR_ERR_INVALID);
    ASSERT_FOLDING(croft_editor_fold_region_for_line(&model, 2u, &settings, &region)
                   == CROFT_EDITOR_ERR_INVALID);

    ASSERT_FOLDING(croft_editor_text_model_set_text(&model, "root\n    \nflat\n", strlen("root\n    \nflat\n"))
                   == CROFT_EDITOR_OK);
    ASSERT_FOLDING(croft_editor_fold_region_for_line(&model, 2u, &settings, &region)
                   == CROFT_EDITOR_ERR_INVALID);

    croft_editor_text_model_dispose(&model);
    return 0;
}
