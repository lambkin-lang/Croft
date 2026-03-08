#include "croft/editor_status.h"

#include <stdio.h>

uint32_t croft_editor_line_number_digits(uint32_t line_count)
{
    uint32_t digits = 1u;

    if (line_count == 0u) {
        return 1u;
    }

    while (line_count >= 10u) {
        line_count /= 10u;
        digits++;
    }

    return digits;
}

int32_t croft_editor_status_format(const croft_editor_status_snapshot* snapshot,
                                   char* buffer,
                                   size_t buffer_size)
{
    int written;

    if (!snapshot || !buffer || buffer_size == 0u) {
        return -1;
    }

    written = snprintf(buffer,
                       buffer_size,
                       "Ln %u, Col %u  Lines %u  %s",
                       snapshot->line_number,
                       snapshot->column,
                       snapshot->line_count,
                       snapshot->is_dirty ? "Modified" : "Saved");
    if (written < 0) {
        return -1;
    }

    return 0;
}
