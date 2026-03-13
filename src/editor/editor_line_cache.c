#include "croft/editor_line_cache.h"

#include <stdlib.h>
#include <string.h>

static void croft_editor_line_cache_reset_entry(croft_editor_line_cache_entry* entry)
{
    if (!entry) {
        return;
    }

    free(entry->tokens);
    memset(entry, 0, sizeof(*entry));
}

static void croft_editor_line_cache_invalidate_from(croft_editor_line_cache* cache,
                                                    uint32_t line_number)
{
    uint32_t index;

    if (!cache) {
        return;
    }

    if (line_number == 0u) {
        line_number = 1u;
    }
    if (line_number > cache->line_count) {
        cache->dirty_from_line = cache->line_count + 1u;
        return;
    }

    for (index = line_number; index <= cache->line_count; index++) {
        croft_editor_line_cache_entry* entry = &cache->lines[index - 1u];
        entry->tokens_valid = 0u;
        entry->fold_valid = 0u;
        entry->has_fold_region = 0u;
        entry->token_count = 0u;
    }
    cache->dirty_from_line = line_number;
}

static uint32_t croft_editor_line_cache_first_dirty_line(const croft_editor_text_model* old_model,
                                                         const croft_editor_text_model* new_model)
{
    uint32_t prefix = 0u;
    uint32_t limit;
    uint32_t line_number = 1u;

    if (!old_model || !new_model) {
        return 1u;
    }

    limit = old_model->utf8_len < new_model->utf8_len ? old_model->utf8_len : new_model->utf8_len;
    while (prefix < limit && old_model->utf8[prefix] == new_model->utf8[prefix]) {
        prefix++;
    }

    if (prefix == old_model->utf8_len && prefix == new_model->utf8_len) {
        return new_model->line_count + 1u;
    }

    {
        uint32_t index;
        for (index = 0u; index < prefix; index++) {
            if ((uint8_t)new_model->utf8[index] == '\n') {
                line_number++;
            }
        }
    }
    return line_number;
}

static int croft_editor_line_cache_settings_equal(const croft_editor_tab_settings* a,
                                                  const croft_editor_tab_settings* b)
{
    if (!a || !b) {
        return 0;
    }
    return a->tab_size == b->tab_size && a->insert_spaces == b->insert_spaces;
}

static int32_t croft_editor_line_cache_reserve_tokens(croft_editor_line_cache_entry* entry,
                                                      uint32_t capacity)
{
    croft_editor_syntax_token* resized;

    if (!entry) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    if (capacity <= entry->token_capacity) {
        return CROFT_EDITOR_OK;
    }

    resized = (croft_editor_syntax_token*)realloc(entry->tokens,
                                                  sizeof(croft_editor_syntax_token) * (size_t)capacity);
    if (!resized) {
        return CROFT_EDITOR_ERR_OOM;
    }

    entry->tokens = resized;
    entry->token_capacity = capacity;
    return CROFT_EDITOR_OK;
}

void croft_editor_line_cache_init(croft_editor_line_cache* cache)
{
    if (!cache) {
        return;
    }

    memset(cache, 0, sizeof(*cache));
    croft_editor_tab_settings_default(&cache->tab_settings);
}

void croft_editor_line_cache_dispose(croft_editor_line_cache* cache)
{
    uint32_t index;

    if (!cache) {
        return;
    }

    for (index = 0u; index < cache->line_count; index++) {
        croft_editor_line_cache_reset_entry(&cache->lines[index]);
    }
    free(cache->lines);
    memset(cache, 0, sizeof(*cache));
}

void croft_editor_line_cache_invalidate_all(croft_editor_line_cache* cache)
{
    croft_editor_line_cache_invalidate_from(cache, 1u);
}

int32_t croft_editor_line_cache_sync(croft_editor_line_cache* cache,
                                     const croft_editor_text_model* old_model,
                                     const croft_editor_text_model* new_model,
                                     croft_editor_syntax_language language,
                                     const croft_editor_tab_settings* settings)
{
    uint32_t new_line_count;
    uint32_t index;
    croft_editor_line_cache_entry* resized;
    uint32_t dirty_from_line = 1u;
    croft_editor_tab_settings effective_settings;

    if (!cache) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    croft_editor_tab_settings_default(&effective_settings);
    if (settings) {
        effective_settings = *settings;
    }

    new_line_count = new_model ? croft_editor_text_model_line_count(new_model) : 0u;
    if (new_line_count == 0u && new_model) {
        new_line_count = 1u;
    }

    if (cache->language != language
            || !croft_editor_line_cache_settings_equal(&cache->tab_settings, &effective_settings)) {
        dirty_from_line = 1u;
    } else if (old_model && new_model) {
        dirty_from_line = croft_editor_line_cache_first_dirty_line(old_model, new_model);
    }

    if (new_line_count < cache->line_count) {
        for (index = new_line_count; index < cache->line_count; index++) {
            croft_editor_line_cache_reset_entry(&cache->lines[index]);
        }
    }

    resized = NULL;
    if (new_line_count > 0u) {
        resized = (croft_editor_line_cache_entry*)realloc(cache->lines,
                                                          sizeof(croft_editor_line_cache_entry)
                                                              * (size_t)new_line_count);
        if (!resized) {
            return CROFT_EDITOR_ERR_OOM;
        }
    } else {
        free(cache->lines);
    }
    cache->lines = resized;

    for (index = cache->line_count; index < new_line_count; index++) {
        memset(&cache->lines[index], 0, sizeof(cache->lines[index]));
    }

    cache->line_count = new_line_count;
    cache->language = language;
    cache->tab_settings = effective_settings;
    croft_editor_line_cache_invalidate_from(cache, dirty_from_line);
    return CROFT_EDITOR_OK;
}

int32_t croft_editor_line_cache_tokens_for_line(croft_editor_line_cache* cache,
                                                const croft_editor_text_model* model,
                                                uint32_t line_number,
                                                const croft_editor_syntax_token** out_tokens,
                                                uint32_t* out_token_count)
{
    croft_editor_line_cache_entry* entry;
    uint32_t search_offset;
    uint32_t line_end_offset;

    if (!cache || !model || !out_tokens || !out_token_count
            || line_number == 0u || line_number > cache->line_count) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    entry = &cache->lines[line_number - 1u];
    if (!entry->tokens_valid) {
        entry->token_count = 0u;
        line_end_offset = croft_editor_text_model_line_end_offset(model, line_number);
        search_offset = croft_editor_text_model_line_start_offset(model, line_number);

        if (cache->language != CROFT_EDITOR_SYNTAX_LANGUAGE_PLAIN_TEXT) {
            while (search_offset < line_end_offset) {
                croft_editor_syntax_token token = {0};

                if (croft_editor_syntax_next_token(model,
                                                   cache->language,
                                                   search_offset,
                                                   line_end_offset,
                                                   &token) != CROFT_EDITOR_OK
                        || token.end_offset <= token.start_offset) {
                    break;
                }
                if (entry->token_count == entry->token_capacity
                        && croft_editor_line_cache_reserve_tokens(
                               entry,
                               entry->token_capacity > 0u ? entry->token_capacity * 2u : 8u)
                            != CROFT_EDITOR_OK) {
                    return CROFT_EDITOR_ERR_OOM;
                }
                entry->tokens[entry->token_count++] = token;
                search_offset = token.end_offset;
            }
        }

        entry->tokens_valid = 1u;
    }

    *out_tokens = entry->tokens;
    *out_token_count = entry->token_count;
    return CROFT_EDITOR_OK;
}

int32_t croft_editor_line_cache_fold_region_for_line(croft_editor_line_cache* cache,
                                                     const croft_editor_text_model* model,
                                                     uint32_t line_number,
                                                     croft_editor_fold_region* out_region)
{
    croft_editor_line_cache_entry* entry;

    if (!cache || !model || !out_region
            || line_number == 0u || line_number > cache->line_count) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    entry = &cache->lines[line_number - 1u];
    if (!entry->fold_valid) {
        entry->has_fold_region =
            croft_editor_fold_region_for_line(model,
                                              line_number,
                                              &cache->tab_settings,
                                              &entry->fold_region) == CROFT_EDITOR_OK
                ? 1u
                : 0u;
        entry->fold_valid = 1u;
    }

    if (!entry->has_fold_region) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    *out_region = entry->fold_region;
    return CROFT_EDITOR_OK;
}
