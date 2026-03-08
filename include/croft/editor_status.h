#ifndef CROFT_EDITOR_STATUS_H
#define CROFT_EDITOR_STATUS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_editor_status_snapshot {
    uint32_t line_number;
    uint32_t column;
    uint32_t line_count;
    int is_dirty;
} croft_editor_status_snapshot;

uint32_t croft_editor_line_number_digits(uint32_t line_count);

int32_t croft_editor_status_format(const croft_editor_status_snapshot* snapshot,
                                   char* buffer,
                                   size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_EDITOR_STATUS_H */
