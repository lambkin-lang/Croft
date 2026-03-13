#ifndef CROFT_EDITOR_LINE_CACHE_H
#define CROFT_EDITOR_LINE_CACHE_H

#include "croft/editor_folding.h"
#include "croft/editor_syntax.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_editor_line_cache_entry {
    croft_editor_syntax_token* tokens;
    uint32_t token_count;
    uint32_t token_capacity;
    uint8_t tokens_valid;
    uint8_t fold_valid;
    uint8_t has_fold_region;
    croft_editor_fold_region fold_region;
} croft_editor_line_cache_entry;

typedef struct croft_editor_line_cache {
    croft_editor_line_cache_entry* lines;
    uint32_t line_count;
    uint32_t dirty_from_line;
    croft_editor_syntax_language language;
    croft_editor_tab_settings tab_settings;
} croft_editor_line_cache;

void croft_editor_line_cache_init(croft_editor_line_cache* cache);
void croft_editor_line_cache_dispose(croft_editor_line_cache* cache);

int32_t croft_editor_line_cache_sync(croft_editor_line_cache* cache,
                                     const croft_editor_text_model* old_model,
                                     const croft_editor_text_model* new_model,
                                     croft_editor_syntax_language language,
                                     const croft_editor_tab_settings* settings);

void croft_editor_line_cache_invalidate_all(croft_editor_line_cache* cache);

int32_t croft_editor_line_cache_tokens_for_line(croft_editor_line_cache* cache,
                                                const croft_editor_text_model* model,
                                                uint32_t line_number,
                                                const croft_editor_syntax_token** out_tokens,
                                                uint32_t* out_token_count);

int32_t croft_editor_line_cache_fold_region_for_line(croft_editor_line_cache* cache,
                                                     const croft_editor_text_model* model,
                                                     uint32_t line_number,
                                                     croft_editor_fold_region* out_region);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_EDITOR_LINE_CACHE_H */
