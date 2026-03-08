#include "croft/editor_status.h"

#include <stdio.h>
#include <string.h>

#define ASSERT_STATUS(cond)                                                \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "    ASSERT failed: %s (%s:%d)\n",             \
                    #cond, __FILE__, __LINE__);                            \
            return 1;                                                      \
        }                                                                  \
    } while (0)

int test_editor_status_line_number_digits(void)
{
    ASSERT_STATUS(croft_editor_line_number_digits(0u) == 1u);
    ASSERT_STATUS(croft_editor_line_number_digits(9u) == 1u);
    ASSERT_STATUS(croft_editor_line_number_digits(10u) == 2u);
    ASSERT_STATUS(croft_editor_line_number_digits(999u) == 3u);
    ASSERT_STATUS(croft_editor_line_number_digits(1000u) == 4u);
    return 0;
}

int test_editor_status_format(void)
{
    croft_editor_status_snapshot snapshot = {12u, 34u, 120u, 1};
    char buffer[64];

    ASSERT_STATUS(croft_editor_status_format(&snapshot, buffer, sizeof(buffer)) == 0);
    ASSERT_STATUS(strcmp(buffer, "Ln 12, Col 34  Lines 120  Modified") == 0);

    snapshot.is_dirty = 0;
    ASSERT_STATUS(croft_editor_status_format(&snapshot, buffer, sizeof(buffer)) == 0);
    ASSERT_STATUS(strcmp(buffer, "Ln 12, Col 34  Lines 120  Saved") == 0);

    return 0;
}
