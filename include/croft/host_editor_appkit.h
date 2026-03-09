#ifndef CROFT_HOST_EDITOR_APPKIT_H
#define CROFT_HOST_EDITOR_APPKIT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct croft_editor_document;

typedef struct croft_editor_appkit_options {
    const char* window_title;
    int32_t auto_close_millis;
} croft_editor_appkit_options;

int32_t croft_editor_appkit_run(struct croft_editor_document** document_io,
                                const croft_editor_appkit_options* options);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_HOST_EDITOR_APPKIT_H */
