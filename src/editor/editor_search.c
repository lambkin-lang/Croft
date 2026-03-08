#include "croft/editor_search.h"

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

int32_t croft_editor_search_next(const croft_editor_text_model* model,
                                 const char* needle_utf8,
                                 size_t needle_utf8_len,
                                 uint32_t start_offset,
                                 croft_editor_search_match* out_match)
{
    uint32_t needle_codepoint_count = 0u;
    uint32_t offset;

    if (!model || !needle_utf8 || needle_utf8_len == 0u || !out_match) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    if (croft_editor_search_count_codepoints(needle_utf8,
                                             needle_utf8_len,
                                             &needle_codepoint_count) != CROFT_EDITOR_OK) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    if (needle_codepoint_count == 0u || model->codepoint_count < needle_codepoint_count) {
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

int32_t croft_editor_search_previous(const croft_editor_text_model* model,
                                     const char* needle_utf8,
                                     size_t needle_utf8_len,
                                     uint32_t before_offset,
                                     croft_editor_search_match* out_match)
{
    uint32_t needle_codepoint_count = 0u;
    uint32_t offset;

    if (!model || !needle_utf8 || needle_utf8_len == 0u || !out_match) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    if (croft_editor_search_count_codepoints(needle_utf8,
                                             needle_utf8_len,
                                             &needle_codepoint_count) != CROFT_EDITOR_OK) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    if (needle_codepoint_count == 0u || model->codepoint_count < needle_codepoint_count) {
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
