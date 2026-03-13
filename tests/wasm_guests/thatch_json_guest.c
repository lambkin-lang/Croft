#include "thatch_json_guest.h"

#include "croft/wit_guest_runtime.h"
#include "croft/wit_runtime_support.h"

#include <limits.h>
#include <stdint.h>

#define CROFT_GJ_TAG_NULL   0x01
#define CROFT_GJ_TAG_TRUE   0x02
#define CROFT_GJ_TAG_FALSE  0x03
#define CROFT_GJ_TAG_INT    0x04
#define CROFT_GJ_TAG_STRING 0x06
#define CROFT_GJ_TAG_ARRAY  0x07
#define CROFT_GJ_TAG_OBJECT 0x08
#define CROFT_GJ_TAG_KEY    0x09

typedef struct {
    const uint8_t *src;
    uint32_t pos;
    uint32_t len;
    ThatchRegion *region;
} CroftGuestJsonParser;

typedef struct {
    char *data;
    uint32_t cap;
    uint32_t len;
    uint32_t overflowed;
} CroftGuestJsonTextBuf;

static uint32_t croft_guest_json_strlen(const char *text)
{
    return text ? (uint32_t)sap_wit_rt_strlen(text) : 0u;
}

static int croft_guest_json_text_equals(const char *lhs, const char *rhs)
{
    uint32_t i = 0u;

    if (lhs == rhs) {
        return 1;
    }
    if (!lhs || !rhs) {
        return 0;
    }
    while (lhs[i] != '\0' && rhs[i] != '\0') {
        if (lhs[i] != rhs[i]) {
            return 0;
        }
        i++;
    }
    return lhs[i] == rhs[i];
}

static void croft_guest_json_skip_ws(CroftGuestJsonParser *parser)
{
    while (parser && parser->pos < parser->len) {
        uint8_t c = parser->src[parser->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            parser->pos++;
        } else {
            break;
        }
    }
}

static int croft_guest_json_match(CroftGuestJsonParser *parser,
                                  const char *lit,
                                  uint32_t lit_len)
{
    if (!parser || !lit || parser->pos + lit_len > parser->len) {
        return 0;
    }
    uint32_t i;
    for (i = 0u; i < lit_len; i++) {
        if (parser->src[parser->pos + i] != (uint8_t)lit[i]) {
            return 0;
        }
    }
    parser->pos += lit_len;
    return 1;
}

static int croft_guest_json_peek(const CroftGuestJsonParser *parser)
{
    if (!parser || parser->pos >= parser->len) {
        return -1;
    }
    return parser->src[parser->pos];
}

static int croft_guest_json_hex_digit(uint8_t c)
{
    if (c >= '0' && c <= '9') {
        return (int)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return (int)(c - 'a') + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return (int)(c - 'A') + 10;
    }
    return -1;
}

static int croft_guest_json_read_hex4(CroftGuestJsonParser *parser, uint32_t *out)
{
    uint32_t i;

    if (!parser || !out || parser->pos + 4u > parser->len) {
        return ERR_PARSE;
    }
    *out = 0u;
    for (i = 0u; i < 4u; i++) {
        int digit = croft_guest_json_hex_digit(parser->src[parser->pos++]);
        if (digit < 0) {
            return ERR_PARSE;
        }
        *out = (*out << 4) | (uint32_t)digit;
    }
    return ERR_OK;
}

static int croft_guest_json_utf8_encode(uint32_t cp, uint8_t out[4])
{
    if (cp <= 0x7Fu) {
        out[0] = (uint8_t)cp;
        return 1;
    }
    if (cp <= 0x7FFu) {
        out[0] = (uint8_t)(0xC0u | (cp >> 6));
        out[1] = (uint8_t)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp <= 0xFFFFu) {
        out[0] = (uint8_t)(0xE0u | (cp >> 12));
        out[1] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (uint8_t)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    if (cp <= 0x10FFFFu) {
        out[0] = (uint8_t)(0xF0u | (cp >> 18));
        out[1] = (uint8_t)(0x80u | ((cp >> 12) & 0x3Fu));
        out[2] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu));
        out[3] = (uint8_t)(0x80u | (cp & 0x3Fu));
        return 4;
    }
    return 0;
}

static int croft_guest_json_parse_value(CroftGuestJsonParser *parser);

static int croft_guest_json_parse_string(CroftGuestJsonParser *parser, uint8_t tag)
{
    ThatchCursor len_loc = 0u;
    int rc;

    if (!parser || croft_guest_json_peek(parser) != '"') {
        return ERR_PARSE;
    }
    parser->pos++;

    rc = thatch_write_tag(parser->region, tag);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = thatch_reserve_skip(parser->region, &len_loc);
    if (rc != ERR_OK) {
        return rc;
    }

    while (parser->pos < parser->len) {
        uint8_t c = parser->src[parser->pos];
        if (c == '"') {
            parser->pos++;
            return thatch_commit_skip(parser->region, len_loc);
        }
        if (c == '\\') {
            uint8_t byte = 0u;
            parser->pos++;
            if (parser->pos >= parser->len) {
                return ERR_PARSE;
            }
            c = parser->src[parser->pos++];
            switch (c) {
                case '"': byte = '"'; break;
                case '\\': byte = '\\'; break;
                case '/': byte = '/'; break;
                case 'b': byte = '\b'; break;
                case 'f': byte = '\f'; break;
                case 'n': byte = '\n'; break;
                case 'r': byte = '\r'; break;
                case 't': byte = '\t'; break;
                case 'u': {
                    uint32_t cp = 0u;
                    uint8_t utf8[4];
                    int utf8_len;

                    rc = croft_guest_json_read_hex4(parser, &cp);
                    if (rc != ERR_OK) {
                        return rc;
                    }
                    if (cp >= 0xD800u && cp <= 0xDBFFu) {
                        uint32_t lo = 0u;
                        if (parser->pos + 1u >= parser->len
                                || parser->src[parser->pos] != '\\'
                                || parser->src[parser->pos + 1u] != 'u') {
                            return ERR_PARSE;
                        }
                        parser->pos += 2u;
                        rc = croft_guest_json_read_hex4(parser, &lo);
                        if (rc != ERR_OK || lo < 0xDC00u || lo > 0xDFFFu) {
                            return ERR_PARSE;
                        }
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                    } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
                        return ERR_PARSE;
                    }
                    utf8_len = croft_guest_json_utf8_encode(cp, utf8);
                    if (utf8_len <= 0) {
                        return ERR_PARSE;
                    }
                    rc = thatch_write_data(parser->region, utf8, (uint32_t)utf8_len);
                    if (rc != ERR_OK) {
                        return rc;
                    }
                    continue;
                }
                default:
                    return ERR_PARSE;
            }
            rc = thatch_write_data(parser->region, &byte, 1u);
            if (rc != ERR_OK) {
                return rc;
            }
            continue;
        }
        if (c < 0x20u) {
            return ERR_PARSE;
        }
        rc = thatch_write_data(parser->region, &c, 1u);
        if (rc != ERR_OK) {
            return rc;
        }
        parser->pos++;
    }
    return ERR_PARSE;
}

static int croft_guest_json_parse_number(CroftGuestJsonParser *parser)
{
    uint32_t start;
    uint32_t i;
    int negative;
    int64_t value = 0;

    if (!parser) {
        return ERR_INVALID;
    }
    start = parser->pos;
    if (parser->pos < parser->len && parser->src[parser->pos] == '-') {
        parser->pos++;
    }
    if (parser->pos >= parser->len) {
        return ERR_PARSE;
    }
    if (parser->src[parser->pos] == '0') {
        parser->pos++;
    } else if (parser->src[parser->pos] >= '1' && parser->src[parser->pos] <= '9') {
        while (parser->pos < parser->len
                && parser->src[parser->pos] >= '0'
                && parser->src[parser->pos] <= '9') {
            parser->pos++;
        }
    } else {
        return ERR_PARSE;
    }
    if (parser->pos < parser->len
            && (parser->src[parser->pos] == '.'
                || parser->src[parser->pos] == 'e'
                || parser->src[parser->pos] == 'E')) {
        return ERR_TYPE;
    }

    negative = parser->src[start] == '-';
    i = negative ? (start + 1u) : start;
    while (i < parser->pos) {
        int digit = parser->src[i] - '0';
        if (negative) {
            if (value < (INT64_MIN + digit) / 10) {
                return ERR_RANGE;
            }
            value = value * 10 - digit;
        } else {
            if (value > (INT64_MAX - digit) / 10) {
                return ERR_RANGE;
            }
            value = value * 10 + digit;
        }
        i++;
    }
    if (thatch_write_tag(parser->region, CROFT_GJ_TAG_INT) != ERR_OK) {
        return ERR_OOM;
    }
    return thatch_write_data(parser->region, &value, sizeof(value));
}

static int croft_guest_json_parse_array(CroftGuestJsonParser *parser)
{
    ThatchCursor skip_loc = 0u;
    int rc;

    parser->pos++;
    rc = thatch_write_tag(parser->region, CROFT_GJ_TAG_ARRAY);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = thatch_reserve_skip(parser->region, &skip_loc);
    if (rc != ERR_OK) {
        return rc;
    }
    croft_guest_json_skip_ws(parser);
    if (croft_guest_json_peek(parser) == ']') {
        parser->pos++;
        return thatch_commit_skip(parser->region, skip_loc);
    }
    for (;;) {
        croft_guest_json_skip_ws(parser);
        rc = croft_guest_json_parse_value(parser);
        if (rc != ERR_OK) {
            return rc;
        }
        croft_guest_json_skip_ws(parser);
        if (croft_guest_json_peek(parser) == ',') {
            parser->pos++;
            continue;
        }
        if (croft_guest_json_peek(parser) == ']') {
            parser->pos++;
            return thatch_commit_skip(parser->region, skip_loc);
        }
        return ERR_PARSE;
    }
}

static int croft_guest_json_parse_object(CroftGuestJsonParser *parser)
{
    ThatchCursor skip_loc = 0u;
    int rc;

    parser->pos++;
    rc = thatch_write_tag(parser->region, CROFT_GJ_TAG_OBJECT);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = thatch_reserve_skip(parser->region, &skip_loc);
    if (rc != ERR_OK) {
        return rc;
    }
    croft_guest_json_skip_ws(parser);
    if (croft_guest_json_peek(parser) == '}') {
        parser->pos++;
        return thatch_commit_skip(parser->region, skip_loc);
    }
    for (;;) {
        croft_guest_json_skip_ws(parser);
        rc = croft_guest_json_parse_string(parser, CROFT_GJ_TAG_KEY);
        if (rc != ERR_OK) {
            return rc;
        }
        croft_guest_json_skip_ws(parser);
        if (croft_guest_json_peek(parser) != ':') {
            return ERR_PARSE;
        }
        parser->pos++;
        croft_guest_json_skip_ws(parser);
        rc = croft_guest_json_parse_value(parser);
        if (rc != ERR_OK) {
            return rc;
        }
        croft_guest_json_skip_ws(parser);
        if (croft_guest_json_peek(parser) == ',') {
            parser->pos++;
            continue;
        }
        if (croft_guest_json_peek(parser) == '}') {
            parser->pos++;
            return thatch_commit_skip(parser->region, skip_loc);
        }
        return ERR_PARSE;
    }
}

static int croft_guest_json_parse_value(CroftGuestJsonParser *parser)
{
    int ch;

    croft_guest_json_skip_ws(parser);
    ch = croft_guest_json_peek(parser);
    switch (ch) {
        case 'n':
            if (!croft_guest_json_match(parser, "null", 4u)) {
                return ERR_PARSE;
            }
            return thatch_write_tag(parser->region, CROFT_GJ_TAG_NULL);
        case 't':
            if (!croft_guest_json_match(parser, "true", 4u)) {
                return ERR_PARSE;
            }
            return thatch_write_tag(parser->region, CROFT_GJ_TAG_TRUE);
        case 'f':
            if (!croft_guest_json_match(parser, "false", 5u)) {
                return ERR_PARSE;
            }
            return thatch_write_tag(parser->region, CROFT_GJ_TAG_FALSE);
        case '"':
            return croft_guest_json_parse_string(parser, CROFT_GJ_TAG_STRING);
        case '[':
            return croft_guest_json_parse_array(parser);
        case '{':
            return croft_guest_json_parse_object(parser);
        default:
            if (ch == '-' || (ch >= '0' && ch <= '9')) {
                return croft_guest_json_parse_number(parser);
            }
            return ERR_PARSE;
    }
}

static int croft_guest_json_val_size(const ThatchRegion *region,
                                     ThatchCursor pos,
                                     uint32_t *size_out)
{
    ThatchCursor cursor = pos;
    uint8_t tag = 0u;
    uint32_t len = 0u;
    int rc;

    if (!region || !size_out) {
        return ERR_INVALID;
    }
    rc = thatch_read_tag(region, &cursor, &tag);
    if (rc != ERR_OK) {
        return rc;
    }
    switch (tag) {
        case CROFT_GJ_TAG_NULL:
        case CROFT_GJ_TAG_TRUE:
        case CROFT_GJ_TAG_FALSE:
            *size_out = 1u;
            return ERR_OK;
        case CROFT_GJ_TAG_INT:
            *size_out = 1u + 8u;
            return ERR_OK;
        case CROFT_GJ_TAG_STRING:
        case CROFT_GJ_TAG_KEY:
            rc = thatch_read_data(region, &cursor, 4u, &len);
            if (rc != ERR_OK) {
                return rc;
            }
            *size_out = 1u + 4u + len;
            return ERR_OK;
        case CROFT_GJ_TAG_ARRAY:
        case CROFT_GJ_TAG_OBJECT:
            rc = thatch_read_skip_len(region, &cursor, &len);
            if (rc != ERR_OK) {
                return rc;
            }
            *size_out = 1u + 4u + len;
            return ERR_OK;
        default:
            return ERR_TYPE;
    }
}

static int croft_guest_json_read_string_like(const ThatchRegion *region,
                                             ThatchCursor *cursor,
                                             uint8_t expected_tag,
                                             const uint8_t **data_out,
                                             uint32_t *len_out)
{
    uint8_t tag = 0u;
    int rc;

    rc = thatch_read_tag(region, cursor, &tag);
    if (rc != ERR_OK) {
        return rc;
    }
    if (tag != expected_tag) {
        return ERR_TYPE;
    }
    rc = thatch_read_data(region, cursor, 4u, len_out);
    if (rc != ERR_OK) {
        return rc;
    }
    return thatch_read_ptr(region, cursor, *len_out, (const void **)data_out);
}

static int croft_guest_json_buf_put(CroftGuestJsonTextBuf *buf, const char *text, uint32_t len)
{
    if (!buf || !text) {
        return ERR_INVALID;
    }
    if (buf->overflowed || buf->len + len + 1u > buf->cap) {
        buf->overflowed = 1u;
        return ERR_FULL;
    }
    sap_wit_rt_memcpy(buf->data + buf->len, text, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return ERR_OK;
}

static int croft_guest_json_buf_put_u8(CroftGuestJsonTextBuf *buf, const uint8_t *text, uint32_t len)
{
    return croft_guest_json_buf_put(buf, (const char *)text, len);
}

static int croft_guest_json_buf_put_ch(CroftGuestJsonTextBuf *buf, char ch)
{
    return croft_guest_json_buf_put(buf, &ch, 1u);
}

static int croft_guest_json_buf_put_indent(CroftGuestJsonTextBuf *buf, uint32_t indent)
{
    uint32_t i;

    for (i = 0u; i < indent; i++) {
        if (croft_guest_json_buf_put(buf, "  ", 2u) != ERR_OK) {
            return ERR_FULL;
        }
    }
    return ERR_OK;
}

static int croft_guest_json_buf_put_u32(CroftGuestJsonTextBuf *buf, uint32_t value)
{
    char tmp[16];
    uint32_t len = 0u;

    if (value == 0u) {
        return croft_guest_json_buf_put_ch(buf, '0');
    }
    while (value > 0u && len < sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (len > 0u) {
        if (croft_guest_json_buf_put_ch(buf, tmp[--len]) != ERR_OK) {
            return ERR_FULL;
        }
    }
    return ERR_OK;
}

static int croft_guest_json_buf_put_i64(CroftGuestJsonTextBuf *buf, int64_t value)
{
    uint64_t mag;

    if (value < 0) {
        if (croft_guest_json_buf_put_ch(buf, '-') != ERR_OK) {
            return ERR_FULL;
        }
        mag = (uint64_t)(-(value + 1)) + 1u;
    } else {
        mag = (uint64_t)value;
    }
    if (mag > UINT32_MAX) {
        return croft_guest_json_buf_put(buf, "<bigint>", 8u);
    }
    return croft_guest_json_buf_put_u32(buf, (uint32_t)mag);
}

static int croft_guest_json_path_is_expanded(const char *path,
                                             const char *const *expanded_paths,
                                             uint32_t expanded_path_count)
{
    uint32_t i;

    for (i = 0u; i < expanded_path_count; i++) {
        if (expanded_paths[i] && croft_guest_json_text_equals(path, expanded_paths[i])) {
            return 1;
        }
    }
    return 0;
}

static int croft_guest_json_append_path(CroftGuestJsonTextBuf *buf,
                                        const char *base,
                                        const uint8_t *name,
                                        uint32_t name_len)
{
    if (base && base[0] != '\0') {
        if (croft_guest_json_buf_put(buf, base, croft_guest_json_strlen(base)) != ERR_OK) {
            return ERR_FULL;
        }
    } else {
        if (croft_guest_json_buf_put_ch(buf, '.') != ERR_OK) {
            return ERR_FULL;
        }
    }
    if (base && base[0] != '\0' && base[croft_guest_json_strlen(base) - 1u] != '.') {
        if (croft_guest_json_buf_put_ch(buf, '.') != ERR_OK) {
            return ERR_FULL;
        }
    }
    return croft_guest_json_buf_put_u8(buf, name, name_len);
}

static int croft_guest_json_render_value(const ThatchRegion *region,
                                         ThatchCursor pos,
                                         const char *path,
                                         const char *label,
                                         uint32_t indent,
                                         const char *const *expanded_paths,
                                         uint32_t expanded_path_count,
                                         CroftGuestJsonTextBuf *buf);

static int croft_guest_json_render_object_children(const ThatchRegion *region,
                                                   ThatchCursor pos,
                                                   const char *path,
                                                   uint32_t indent,
                                                   const char *const *expanded_paths,
                                                   uint32_t expanded_path_count,
                                                   CroftGuestJsonTextBuf *buf)
{
    ThatchCursor cursor = pos;
    uint8_t tag = 0u;
    uint32_t skip_len = 0u;
    ThatchCursor end = 0u;
    int rc;

    rc = thatch_read_tag(region, &cursor, &tag);
    if (rc != ERR_OK || tag != CROFT_GJ_TAG_OBJECT) {
        return rc != ERR_OK ? rc : ERR_TYPE;
    }
    rc = thatch_read_skip_len(region, &cursor, &skip_len);
    if (rc != ERR_OK) {
        return rc;
    }
    end = cursor + skip_len;
    while (cursor < end) {
        const uint8_t *name = NULL;
        uint32_t name_len = 0u;
        char child_path[256];
        CroftGuestJsonTextBuf path_buf = {child_path, sizeof(child_path), 0u, 0u};

        rc = croft_guest_json_read_string_like(region, &cursor, CROFT_GJ_TAG_KEY, &name, &name_len);
        if (rc != ERR_OK) {
            return rc;
        }
        child_path[0] = '\0';
        rc = croft_guest_json_append_path(&path_buf, path, name, name_len);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_guest_json_render_value(region,
                                           cursor,
                                           child_path,
                                           (const char *)name,
                                           indent + 1u,
                                           expanded_paths,
                                           expanded_path_count,
                                           buf);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_guest_json_val_size(region, cursor, &skip_len);
        if (rc != ERR_OK) {
            return rc;
        }
        cursor += skip_len;
    }
    return ERR_OK;
}

static int croft_guest_json_count_object_keys(const ThatchRegion *region,
                                              ThatchCursor pos,
                                              uint32_t *count_out)
{
    ThatchCursor cursor = pos;
    uint8_t tag = 0u;
    uint32_t skip_len = 0u;
    ThatchCursor end = 0u;
    uint32_t count = 0u;
    int rc;

    rc = thatch_read_tag(region, &cursor, &tag);
    if (rc != ERR_OK || tag != CROFT_GJ_TAG_OBJECT) {
        return rc != ERR_OK ? rc : ERR_TYPE;
    }
    rc = thatch_read_skip_len(region, &cursor, &skip_len);
    if (rc != ERR_OK) {
        return rc;
    }
    end = cursor + skip_len;
    while (cursor < end) {
        rc = croft_guest_json_val_size(region, cursor, &skip_len);
        if (rc != ERR_OK) {
            return rc;
        }
        cursor += skip_len;
        rc = croft_guest_json_val_size(region, cursor, &skip_len);
        if (rc != ERR_OK) {
            return rc;
        }
        cursor += skip_len;
        count++;
    }
    *count_out = count;
    return ERR_OK;
}

static int croft_guest_json_count_array_items(const ThatchRegion *region,
                                              ThatchCursor pos,
                                              uint32_t *count_out)
{
    ThatchCursor cursor = pos;
    uint8_t tag = 0u;
    uint32_t skip_len = 0u;
    ThatchCursor end = 0u;
    uint32_t count = 0u;
    int rc;

    rc = thatch_read_tag(region, &cursor, &tag);
    if (rc != ERR_OK || tag != CROFT_GJ_TAG_ARRAY) {
        return rc != ERR_OK ? rc : ERR_TYPE;
    }
    rc = thatch_read_skip_len(region, &cursor, &skip_len);
    if (rc != ERR_OK) {
        return rc;
    }
    end = cursor + skip_len;
    while (cursor < end) {
        rc = croft_guest_json_val_size(region, cursor, &skip_len);
        if (rc != ERR_OK) {
            return rc;
        }
        cursor += skip_len;
        count++;
    }
    *count_out = count;
    return ERR_OK;
}

static int croft_guest_json_render_array_children(const ThatchRegion *region,
                                                  ThatchCursor pos,
                                                  const char *path,
                                                  uint32_t indent,
                                                  const char *const *expanded_paths,
                                                  uint32_t expanded_path_count,
                                                  CroftGuestJsonTextBuf *buf)
{
    ThatchCursor cursor = pos;
    uint8_t tag = 0u;
    uint32_t skip_len = 0u;
    ThatchCursor end = 0u;
    uint32_t index = 0u;
    int rc;

    rc = thatch_read_tag(region, &cursor, &tag);
    if (rc != ERR_OK || tag != CROFT_GJ_TAG_ARRAY) {
        return rc != ERR_OK ? rc : ERR_TYPE;
    }
    rc = thatch_read_skip_len(region, &cursor, &skip_len);
    if (rc != ERR_OK) {
        return rc;
    }
    end = cursor + skip_len;
    while (cursor < end) {
        char child_path[256];
        CroftGuestJsonTextBuf path_buf = {child_path, sizeof(child_path), 0u, 0u};

        child_path[0] = '\0';
        if (path && path[0] != '\0') {
            rc = croft_guest_json_buf_put(&path_buf, path, croft_guest_json_strlen(path));
            if (rc != ERR_OK) {
                return rc;
            }
        } else {
            rc = croft_guest_json_buf_put_ch(&path_buf, '.');
            if (rc != ERR_OK) {
                return rc;
            }
        }
        rc = croft_guest_json_buf_put_ch(&path_buf, '[');
        if (rc == ERR_OK) {
            rc = croft_guest_json_buf_put_u32(&path_buf, index);
        }
        if (rc == ERR_OK) {
            rc = croft_guest_json_buf_put_ch(&path_buf, ']');
        }
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_guest_json_render_value(region,
                                           cursor,
                                           child_path,
                                           child_path + croft_guest_json_strlen(path ? path : ""),
                                           indent + 1u,
                                           expanded_paths,
                                           expanded_path_count,
                                           buf);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_guest_json_val_size(region, cursor, &skip_len);
        if (rc != ERR_OK) {
            return rc;
        }
        cursor += skip_len;
        index++;
    }
    return ERR_OK;
}

static int croft_guest_json_render_value(const ThatchRegion *region,
                                         ThatchCursor pos,
                                         const char *path,
                                         const char *label,
                                         uint32_t indent,
                                         const char *const *expanded_paths,
                                         uint32_t expanded_path_count,
                                         CroftGuestJsonTextBuf *buf)
{
    ThatchCursor cursor = pos;
    uint8_t tag = 0u;
    int rc;

    rc = thatch_read_tag(region, &cursor, &tag);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_guest_json_buf_put_indent(buf, indent);
    if (rc != ERR_OK) {
        return rc;
    }
    if (label && label[0] != '\0') {
        rc = croft_guest_json_buf_put(buf, label, croft_guest_json_strlen(label));
        if (rc == ERR_OK) {
            rc = croft_guest_json_buf_put(buf, ": ", 2u);
        }
        if (rc != ERR_OK) {
            return rc;
        }
    }
    switch (tag) {
        case CROFT_GJ_TAG_NULL:
            rc = croft_guest_json_buf_put(buf, "null\n", 5u);
            break;
        case CROFT_GJ_TAG_TRUE:
            rc = croft_guest_json_buf_put(buf, "true\n", 5u);
            break;
        case CROFT_GJ_TAG_FALSE:
            rc = croft_guest_json_buf_put(buf, "false\n", 6u);
            break;
        case CROFT_GJ_TAG_INT: {
            int64_t value = 0;
            rc = thatch_read_data(region, &cursor, sizeof(value), &value);
            if (rc == ERR_OK) {
                rc = croft_guest_json_buf_put_i64(buf, value);
            }
            if (rc == ERR_OK) {
                rc = croft_guest_json_buf_put_ch(buf, '\n');
            }
            break;
        }
        case CROFT_GJ_TAG_STRING: {
            const uint8_t *text = NULL;
            uint32_t len = 0u;

            rc = thatch_read_data(region, &cursor, 4u, &len);
            if (rc == ERR_OK) {
                rc = thatch_read_ptr(region, &cursor, len, (const void **)&text);
            }
            if (rc == ERR_OK) {
                rc = croft_guest_json_buf_put_ch(buf, '"');
            }
            if (rc == ERR_OK) {
                rc = croft_guest_json_buf_put_u8(buf, text, len);
            }
            if (rc == ERR_OK) {
                rc = croft_guest_json_buf_put(buf, "\"\n", 2u);
            }
            break;
        }
        case CROFT_GJ_TAG_OBJECT: {
            uint32_t count = 0u;
            rc = croft_guest_json_count_object_keys(region, pos, &count);
            if (rc == ERR_OK) {
                rc = croft_guest_json_buf_put_ch(buf, '{');
            }
            if (rc == ERR_OK) {
                rc = croft_guest_json_buf_put_u32(buf, count);
            }
            if (rc == ERR_OK) {
                rc = croft_guest_json_buf_put(buf, " keys}\n", 7u);
            }
            if (rc == ERR_OK && croft_guest_json_path_is_expanded(path, expanded_paths, expanded_path_count)) {
                rc = croft_guest_json_render_object_children(region,
                                                             pos,
                                                             path,
                                                             indent,
                                                             expanded_paths,
                                                             expanded_path_count,
                                                             buf);
            }
            break;
        }
        case CROFT_GJ_TAG_ARRAY: {
            uint32_t count = 0u;
            rc = croft_guest_json_count_array_items(region, pos, &count);
            if (rc == ERR_OK) {
                rc = croft_guest_json_buf_put_ch(buf, '[');
            }
            if (rc == ERR_OK) {
                rc = croft_guest_json_buf_put_u32(buf, count);
            }
            if (rc == ERR_OK) {
                rc = croft_guest_json_buf_put(buf, " items]\n", 8u);
            }
            if (rc == ERR_OK && croft_guest_json_path_is_expanded(path, expanded_paths, expanded_path_count)) {
                rc = croft_guest_json_render_array_children(region,
                                                            pos,
                                                            path,
                                                            indent,
                                                            expanded_paths,
                                                            expanded_path_count,
                                                            buf);
            }
            break;
        }
        default:
            rc = ERR_TYPE;
            break;
    }
    return rc;
}

static int croft_guest_json_collect_paths(const ThatchRegion *region,
                                          ThatchCursor pos,
                                          const char *path,
                                          CroftGuestJsonTextBuf *buf)
{
    ThatchCursor cursor = pos;
    uint8_t tag = 0u;
    int rc;

    rc = thatch_read_tag(region, &cursor, &tag);
    if (rc != ERR_OK) {
        return rc;
    }
    if ((tag == CROFT_GJ_TAG_OBJECT || tag == CROFT_GJ_TAG_ARRAY) && path && path[0] != '\0') {
        rc = croft_guest_json_buf_put(buf, path, croft_guest_json_strlen(path));
        if (rc == ERR_OK) {
            rc = croft_guest_json_buf_put_ch(buf, '\n');
        }
        if (rc != ERR_OK) {
            return rc;
        }
    }
    if (tag == CROFT_GJ_TAG_OBJECT) {
        ThatchCursor child = cursor;
        uint32_t skip_len = 0u;
        ThatchCursor end = 0u;

        rc = thatch_read_skip_len(region, &child, &skip_len);
        if (rc != ERR_OK) {
            return rc;
        }
        end = child + skip_len;
        while (child < end) {
            const uint8_t *name = NULL;
            uint32_t name_len = 0u;
            char child_path[256];
            CroftGuestJsonTextBuf path_buf = {child_path, sizeof(child_path), 0u, 0u};

            rc = croft_guest_json_read_string_like(region, &child, CROFT_GJ_TAG_KEY, &name, &name_len);
            if (rc != ERR_OK) {
                return rc;
            }
            child_path[0] = '\0';
            rc = croft_guest_json_append_path(&path_buf, path, name, name_len);
            if (rc != ERR_OK) {
                return rc;
            }
            rc = croft_guest_json_collect_paths(region, child, child_path, buf);
            if (rc != ERR_OK) {
                return rc;
            }
            rc = croft_guest_json_val_size(region, child, &skip_len);
            if (rc != ERR_OK) {
                return rc;
            }
            child += skip_len;
        }
    } else if (tag == CROFT_GJ_TAG_ARRAY) {
        ThatchCursor child = cursor;
        uint32_t skip_len = 0u;
        ThatchCursor end = 0u;
        uint32_t index = 0u;

        rc = thatch_read_skip_len(region, &child, &skip_len);
        if (rc != ERR_OK) {
            return rc;
        }
        end = child + skip_len;
        while (child < end) {
            char child_path[256];
            CroftGuestJsonTextBuf path_buf = {child_path, sizeof(child_path), 0u, 0u};

            child_path[0] = '\0';
            if (path && path[0] != '\0') {
                rc = croft_guest_json_buf_put(&path_buf, path, croft_guest_json_strlen(path));
            } else {
                rc = croft_guest_json_buf_put_ch(&path_buf, '.');
            }
            if (rc == ERR_OK) {
                rc = croft_guest_json_buf_put_ch(&path_buf, '[');
            }
            if (rc == ERR_OK) {
                rc = croft_guest_json_buf_put_u32(&path_buf, index);
            }
            if (rc == ERR_OK) {
                rc = croft_guest_json_buf_put_ch(&path_buf, ']');
            }
            if (rc != ERR_OK) {
                return rc;
            }
            rc = croft_guest_json_collect_paths(region, child, child_path, buf);
            if (rc != ERR_OK) {
                return rc;
            }
            rc = croft_guest_json_val_size(region, child, &skip_len);
            if (rc != ERR_OK) {
                return rc;
            }
            child += skip_len;
            index++;
        }
    }
    return ERR_OK;
}

static int croft_guest_json_collect_visible_paths(const ThatchRegion *region,
                                                  ThatchCursor pos,
                                                  const char *path,
                                                  const char *const *expanded_paths,
                                                  uint32_t expanded_path_count,
                                                  CroftGuestJsonTextBuf *buf)
{
    ThatchCursor cursor = pos;
    uint8_t tag = 0u;
    int rc;

    rc = thatch_read_tag(region, &cursor, &tag);
    if (rc != ERR_OK) {
        return rc;
    }
    if (path && path[0] != '\0') {
        rc = croft_guest_json_buf_put(buf, path, croft_guest_json_strlen(path));
        if (rc == ERR_OK) {
            rc = croft_guest_json_buf_put_ch(buf, '\n');
        }
        if (rc != ERR_OK) {
            return rc;
        }
    }
    if (tag == CROFT_GJ_TAG_OBJECT && croft_guest_json_path_is_expanded(path,
                                                                        expanded_paths,
                                                                        expanded_path_count)) {
        ThatchCursor child = cursor;
        uint32_t skip_len = 0u;
        ThatchCursor end = 0u;

        rc = thatch_read_skip_len(region, &child, &skip_len);
        if (rc != ERR_OK) {
            return rc;
        }
        end = child + skip_len;
        while (child < end) {
            const uint8_t *name = NULL;
            uint32_t name_len = 0u;
            char child_path[256];
            CroftGuestJsonTextBuf path_buf = {child_path, sizeof(child_path), 0u, 0u};

            rc = croft_guest_json_read_string_like(region, &child, CROFT_GJ_TAG_KEY, &name, &name_len);
            if (rc != ERR_OK) {
                return rc;
            }
            child_path[0] = '\0';
            rc = croft_guest_json_append_path(&path_buf, path, name, name_len);
            if (rc != ERR_OK) {
                return rc;
            }
            rc = croft_guest_json_collect_visible_paths(region,
                                                        child,
                                                        child_path,
                                                        expanded_paths,
                                                        expanded_path_count,
                                                        buf);
            if (rc != ERR_OK) {
                return rc;
            }
            rc = croft_guest_json_val_size(region, child, &skip_len);
            if (rc != ERR_OK) {
                return rc;
            }
            child += skip_len;
        }
    } else if (tag == CROFT_GJ_TAG_ARRAY && croft_guest_json_path_is_expanded(path,
                                                                               expanded_paths,
                                                                               expanded_path_count)) {
        ThatchCursor child = cursor;
        uint32_t skip_len = 0u;
        ThatchCursor end = 0u;
        uint32_t index = 0u;

        rc = thatch_read_skip_len(region, &child, &skip_len);
        if (rc != ERR_OK) {
            return rc;
        }
        end = child + skip_len;
        while (child < end) {
            char child_path[256];
            CroftGuestJsonTextBuf path_buf = {child_path, sizeof(child_path), 0u, 0u};

            child_path[0] = '\0';
            if (path && path[0] != '\0') {
                rc = croft_guest_json_buf_put(&path_buf, path, croft_guest_json_strlen(path));
            } else {
                rc = croft_guest_json_buf_put_ch(&path_buf, '.');
            }
            if (rc == ERR_OK) {
                rc = croft_guest_json_buf_put_ch(&path_buf, '[');
            }
            if (rc == ERR_OK) {
                rc = croft_guest_json_buf_put_u32(&path_buf, index);
            }
            if (rc == ERR_OK) {
                rc = croft_guest_json_buf_put_ch(&path_buf, ']');
            }
            if (rc != ERR_OK) {
                return rc;
            }
            rc = croft_guest_json_collect_visible_paths(region,
                                                        child,
                                                        child_path,
                                                        expanded_paths,
                                                        expanded_path_count,
                                                        buf);
            if (rc != ERR_OK) {
                return rc;
            }
            rc = croft_guest_json_val_size(region, child, &skip_len);
            if (rc != ERR_OK) {
                return rc;
            }
            child += skip_len;
            index++;
        }
    }
    return ERR_OK;
}

int croft_guest_json_parse(CroftGuestJsonDocument *doc,
                           uint8_t *storage,
                           uint32_t storage_cap,
                           const uint8_t *json,
                           uint32_t json_len,
                           uint32_t *err_pos_out)
{
    CroftGuestJsonParser parser;
    int rc;

    if (err_pos_out) {
        *err_pos_out = 0u;
    }
    if (!doc || !storage || storage_cap == 0u || (!json && json_len > 0u)) {
        return ERR_INVALID;
    }
    sap_wit_rt_memset(doc, 0, sizeof(*doc));
    sap_wit_guest_region_init_writable(&doc->region, storage, storage_cap);
    parser.src = json;
    parser.pos = 0u;
    parser.len = json_len;
    parser.region = &doc->region;

    rc = croft_guest_json_parse_value(&parser);
    if (rc != ERR_OK) {
        if (err_pos_out) {
            *err_pos_out = parser.pos;
        }
        return rc;
    }
    croft_guest_json_skip_ws(&parser);
    if (parser.pos != parser.len) {
        if (err_pos_out) {
            *err_pos_out = parser.pos;
        }
        return ERR_PARSE;
    }
    doc->root = 0u;
    doc->region.sealed = 1;
    return ERR_OK;
}

int croft_guest_json_render_collapsed_view(const CroftGuestJsonDocument *doc,
                                           const char *const *expanded_paths,
                                           uint32_t expanded_path_count,
                                           char *out,
                                           uint32_t out_cap)
{
    CroftGuestJsonTextBuf buf = {0};
    int rc;

    if (!doc || !out || out_cap == 0u) {
        return ERR_INVALID;
    }
    out[0] = '\0';
    buf.data = out;
    buf.cap = out_cap;
    rc = croft_guest_json_render_value(&doc->region,
                                       doc->root,
                                       ".",
                                       "",
                                       0u,
                                       expanded_paths,
                                       expanded_path_count,
                                       &buf);
    if (rc != ERR_OK || buf.overflowed) {
        return ERR_FULL;
    }
    return ERR_OK;
}

int croft_guest_json_render_cursor_paths(const CroftGuestJsonDocument *doc,
                                         char *out,
                                         uint32_t out_cap)
{
    CroftGuestJsonTextBuf buf = {0};
    int rc;

    if (!doc || !out || out_cap == 0u) {
        return ERR_INVALID;
    }
    out[0] = '\0';
    buf.data = out;
    buf.cap = out_cap;
    rc = croft_guest_json_collect_paths(&doc->region, doc->root, ".", &buf);
    if (rc != ERR_OK || buf.overflowed) {
        return ERR_FULL;
    }
    return ERR_OK;
}

int croft_guest_json_render_visible_paths(const CroftGuestJsonDocument *doc,
                                          const char *const *expanded_paths,
                                          uint32_t expanded_path_count,
                                          char *out,
                                          uint32_t out_cap)
{
    CroftGuestJsonTextBuf buf = {0};
    int rc;

    if (!doc || !out || out_cap == 0u) {
        return ERR_INVALID;
    }
    out[0] = '\0';
    buf.data = out;
    buf.cap = out_cap;
    rc = croft_guest_json_collect_visible_paths(&doc->region,
                                                doc->root,
                                                ".",
                                                expanded_paths,
                                                expanded_path_count,
                                                &buf);
    if (rc != ERR_OK || buf.overflowed) {
        return ERR_FULL;
    }
    return ERR_OK;
}
