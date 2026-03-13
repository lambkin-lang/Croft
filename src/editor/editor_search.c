#include "croft/editor_search.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int32_t croft_editor_search_decode_one(const char* utf8,
                                              size_t remaining,
                                              uint32_t* consumed_out)
{
    unsigned char b0;

    if (!utf8 || remaining == 0u || !consumed_out) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    b0 = (unsigned char)utf8[0];
    if (b0 < 0x80u) {
        *consumed_out = 1u;
        return CROFT_EDITOR_OK;
    }
    if ((b0 & 0xE0u) == 0xC0u) {
        if (remaining < 2u || (((unsigned char)utf8[1]) & 0xC0u) != 0x80u) {
            return CROFT_EDITOR_ERR_INVALID;
        }
        *consumed_out = 2u;
        return CROFT_EDITOR_OK;
    }
    if ((b0 & 0xF0u) == 0xE0u) {
        if (remaining < 3u
                || (((unsigned char)utf8[1]) & 0xC0u) != 0x80u
                || (((unsigned char)utf8[2]) & 0xC0u) != 0x80u) {
            return CROFT_EDITOR_ERR_INVALID;
        }
        *consumed_out = 3u;
        return CROFT_EDITOR_OK;
    }
    if ((b0 & 0xF8u) == 0xF0u) {
        if (remaining < 4u
                || (((unsigned char)utf8[1]) & 0xC0u) != 0x80u
                || (((unsigned char)utf8[2]) & 0xC0u) != 0x80u
                || (((unsigned char)utf8[3]) & 0xC0u) != 0x80u) {
            return CROFT_EDITOR_ERR_INVALID;
        }
        *consumed_out = 4u;
        return CROFT_EDITOR_OK;
    }

    return CROFT_EDITOR_ERR_INVALID;
}

static int32_t croft_editor_search_count_codepoints(const char* utf8,
                                                    size_t utf8_len,
                                                    uint32_t* out_codepoint_count)
{
    size_t byte_offset = 0u;
    uint32_t codepoint_count = 0u;

    if (!utf8 || !out_codepoint_count) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    while (byte_offset < utf8_len) {
        uint32_t consumed = 0u;
        if (croft_editor_search_decode_one(utf8 + byte_offset,
                                           utf8_len - byte_offset,
                                           &consumed) != CROFT_EDITOR_OK) {
            return CROFT_EDITOR_ERR_INVALID;
        }
        byte_offset += consumed;
        codepoint_count++;
    }

    *out_codepoint_count = codepoint_count;
    return CROFT_EDITOR_OK;
}

static int croft_editor_search_match_at(const croft_editor_text_model* model,
                                        const char* needle_utf8,
                                        size_t needle_utf8_len,
                                        uint32_t needle_codepoint_count,
                                        uint32_t start_offset,
                                        croft_editor_search_match* out_match)
{
    uint32_t match_start_byte;
    uint32_t match_end_byte;
    size_t match_len_bytes;

    if (!model || !needle_utf8 || !out_match || needle_codepoint_count == 0u) {
        return 0;
    }
    if (start_offset + needle_codepoint_count > model->codepoint_count) {
        return 0;
    }

    match_start_byte = model->codepoint_to_byte_offsets[start_offset];
    match_end_byte = model->codepoint_to_byte_offsets[start_offset + needle_codepoint_count];
    match_len_bytes = (size_t)(match_end_byte - match_start_byte);

    if (match_len_bytes != needle_utf8_len) {
        return 0;
    }
    if (memcmp(model->utf8 + match_start_byte, needle_utf8, needle_utf8_len) != 0) {
        return 0;
    }

    out_match->start_offset = start_offset;
    out_match->end_offset = start_offset + needle_codepoint_count;
    return 1;
}

static int32_t croft_editor_search_prepare_needle(const croft_editor_text_model* model,
                                                  const char* needle_utf8,
                                                  size_t needle_utf8_len,
                                                  uint32_t* out_codepoint_count)
{
    uint32_t needle_codepoint_count = 0u;

    if (!model || !needle_utf8 || needle_utf8_len == 0u || !out_codepoint_count) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    if (croft_editor_search_count_codepoints(needle_utf8,
                                             needle_utf8_len,
                                             &needle_codepoint_count) != CROFT_EDITOR_OK) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    if (needle_codepoint_count == 0u) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    *out_codepoint_count = needle_codepoint_count;
    return CROFT_EDITOR_OK;
}

static int32_t croft_editor_search_next_resolved(const croft_editor_text_model* model,
                                                 const char* needle_utf8,
                                                 size_t needle_utf8_len,
                                                 uint32_t needle_codepoint_count,
                                                 uint32_t start_offset,
                                                 croft_editor_search_match* out_match)
{
    uint32_t offset;

    if (!model || !needle_utf8 || needle_utf8_len == 0u || !out_match || needle_codepoint_count == 0u) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    if (model->codepoint_count < needle_codepoint_count) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    if (start_offset > model->codepoint_count - needle_codepoint_count) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    for (offset = start_offset; offset + needle_codepoint_count <= model->codepoint_count; offset++) {
        if (croft_editor_search_match_at(model,
                                         needle_utf8,
                                         needle_utf8_len,
                                         needle_codepoint_count,
                                         offset,
                                         out_match)) {
            return CROFT_EDITOR_OK;
        }
    }

    return CROFT_EDITOR_ERR_INVALID;
}

static int32_t croft_editor_search_previous_resolved(const croft_editor_text_model* model,
                                                     const char* needle_utf8,
                                                     size_t needle_utf8_len,
                                                     uint32_t needle_codepoint_count,
                                                     uint32_t before_offset,
                                                     croft_editor_search_match* out_match)
{
    uint32_t offset;

    if (!model || !needle_utf8 || needle_utf8_len == 0u || !out_match || needle_codepoint_count == 0u) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    if (model->codepoint_count < needle_codepoint_count) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    if (before_offset == 0u) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    if (before_offset > model->codepoint_count) {
        before_offset = model->codepoint_count;
    }
    if (before_offset < needle_codepoint_count) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    offset = before_offset - needle_codepoint_count;
    if (offset > model->codepoint_count - needle_codepoint_count) {
        offset = model->codepoint_count - needle_codepoint_count;
    }

    for (;;) {
        if (croft_editor_search_match_at(model,
                                         needle_utf8,
                                         needle_utf8_len,
                                         needle_codepoint_count,
                                         offset,
                                         out_match)) {
            return CROFT_EDITOR_OK;
        }
        if (offset == 0u) {
            break;
        }
        offset--;
    }

    return CROFT_EDITOR_ERR_INVALID;
}

int32_t croft_editor_search_next(const croft_editor_text_model* model,
                                 const char* needle_utf8,
                                 size_t needle_utf8_len,
                                 uint32_t start_offset,
                                 croft_editor_search_match* out_match)
{
    uint32_t needle_codepoint_count = 0u;

    if (croft_editor_search_prepare_needle(model,
                                           needle_utf8,
                                           needle_utf8_len,
                                           &needle_codepoint_count) != CROFT_EDITOR_OK) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    return croft_editor_search_next_resolved(model,
                                             needle_utf8,
                                             needle_utf8_len,
                                             needle_codepoint_count,
                                             start_offset,
                                             out_match);
}

int32_t croft_editor_search_previous(const croft_editor_text_model* model,
                                     const char* needle_utf8,
                                     size_t needle_utf8_len,
                                     uint32_t before_offset,
                                     croft_editor_search_match* out_match)
{
    uint32_t needle_codepoint_count = 0u;

    if (croft_editor_search_prepare_needle(model,
                                           needle_utf8,
                                           needle_utf8_len,
                                           &needle_codepoint_count) != CROFT_EDITOR_OK) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    return croft_editor_search_previous_resolved(model,
                                                 needle_utf8,
                                                 needle_utf8_len,
                                                 needle_codepoint_count,
                                                 before_offset,
                                                 out_match);
}

int32_t croft_editor_search_count_matches(const croft_editor_text_model* model,
                                          const char* needle_utf8,
                                          size_t needle_utf8_len,
                                          uint32_t* out_match_count)
{
    croft_editor_search_match match = {0};
    uint32_t needle_codepoint_count = 0u;
    uint32_t count = 0u;
    uint32_t search_from = 0u;

    if (!out_match_count) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    *out_match_count = 0u;

    if (croft_editor_search_prepare_needle(model,
                                           needle_utf8,
                                           needle_utf8_len,
                                           &needle_codepoint_count) != CROFT_EDITOR_OK) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    while (search_from + needle_codepoint_count <= model->codepoint_count
            && croft_editor_search_next_resolved(model,
                                                 needle_utf8,
                                                 needle_utf8_len,
                                                 needle_codepoint_count,
                                                 search_from,
                                                 &match) == CROFT_EDITOR_OK) {
        if (count == UINT32_MAX) {
            return CROFT_EDITOR_ERR_OOM;
        }
        count++;
        search_from = match.end_offset;
    }

    *out_match_count = count;
    return CROFT_EDITOR_OK;
}

int32_t croft_editor_search_replace_all_utf8(const croft_editor_text_model* model,
                                             const char* needle_utf8,
                                             size_t needle_utf8_len,
                                             const char* replacement_utf8,
                                             size_t replacement_utf8_len,
                                             char** out_utf8,
                                             size_t* out_utf8_len,
                                             uint32_t* out_match_count)
{
    croft_editor_search_match match = {0};
    uint32_t needle_codepoint_count = 0u;
    uint32_t match_count = 0u;
    uint32_t search_from = 0u;
    uint32_t previous_byte = 0u;
    size_t result_len = 0u;
    size_t write_offset = 0u;
    char* result = NULL;

    if (!model || !out_utf8 || !out_utf8_len || !out_match_count) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    if (!replacement_utf8 && replacement_utf8_len > 0u) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    *out_utf8 = NULL;
    *out_utf8_len = 0u;
    *out_match_count = 0u;

    if (croft_editor_search_prepare_needle(model,
                                           needle_utf8,
                                           needle_utf8_len,
                                           &needle_codepoint_count) != CROFT_EDITOR_OK) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    while (search_from + needle_codepoint_count <= model->codepoint_count
            && croft_editor_search_next_resolved(model,
                                                 needle_utf8,
                                                 needle_utf8_len,
                                                 needle_codepoint_count,
                                                 search_from,
                                                 &match) == CROFT_EDITOR_OK) {
        size_t prefix_len = (size_t)croft_editor_text_model_byte_offset_at(model, match.start_offset)
            - (size_t)previous_byte;

        if (SIZE_MAX - result_len < prefix_len
                || SIZE_MAX - (result_len + prefix_len) < replacement_utf8_len) {
            return CROFT_EDITOR_ERR_OOM;
        }

        result_len += prefix_len + replacement_utf8_len;
        previous_byte = croft_editor_text_model_byte_offset_at(model, match.end_offset);
        search_from = match.end_offset;
        match_count++;
    }

    if (SIZE_MAX - result_len < (size_t)(model->utf8_len - previous_byte)) {
        return CROFT_EDITOR_ERR_OOM;
    }
    result_len += (size_t)(model->utf8_len - previous_byte);

    result = (char*)malloc(result_len + 1u);
    if (!result) {
        return CROFT_EDITOR_ERR_OOM;
    }

    previous_byte = 0u;
    search_from = 0u;
    while (search_from + needle_codepoint_count <= model->codepoint_count
            && croft_editor_search_next_resolved(model,
                                                 needle_utf8,
                                                 needle_utf8_len,
                                                 needle_codepoint_count,
                                                 search_from,
                                                 &match) == CROFT_EDITOR_OK) {
        uint32_t match_start_byte = croft_editor_text_model_byte_offset_at(model, match.start_offset);
        uint32_t match_end_byte = croft_editor_text_model_byte_offset_at(model, match.end_offset);
        size_t prefix_len = (size_t)(match_start_byte - previous_byte);

        if (prefix_len > 0u) {
            memcpy(result + write_offset, model->utf8 + previous_byte, prefix_len);
            write_offset += prefix_len;
        }
        if (replacement_utf8_len > 0u) {
            memcpy(result + write_offset, replacement_utf8, replacement_utf8_len);
            write_offset += replacement_utf8_len;
        }

        previous_byte = match_end_byte;
        search_from = match.end_offset;
    }

    if (previous_byte < model->utf8_len) {
        size_t suffix_len = (size_t)(model->utf8_len - previous_byte);
        memcpy(result + write_offset, model->utf8 + previous_byte, suffix_len);
        write_offset += suffix_len;
    }

    result[write_offset] = '\0';
    *out_utf8 = result;
    *out_utf8_len = write_offset;
    *out_match_count = match_count;
    return CROFT_EDITOR_OK;
}
