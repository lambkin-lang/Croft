/*
 * wit_codegen.c — WIT schema codegen in portable C11
 *
 * Replaces tools/wit_schema_codegen.py entirely.
 * Parses runtime-schema.wit via recursive descent and emits
 * Thatch-native C headers and source files.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 lambkin-lang
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdarg.h>

#define MAX_FIELDS     32
#define MAX_CASES      32
#define MAX_TYPES      64
#define MAX_FUNCS      128
#define MAX_NAME       128
#define MAX_PACKAGE    128
#define MAX_TRACE      512
#define MAX_TYPE_NODES 512
#define MAX_PATH_TEXT  1024

/* ------------------------------------------------------------------ */
/* Type expression AST                                                */
/* ------------------------------------------------------------------ */

typedef enum {
    TYPE_IDENT,   /* bare identifier: s64, worker-id, message-envelope */
    TYPE_OPTION,  /* option<T>                                         */
    TYPE_LIST,    /* list<T>                                           */
    TYPE_TUPLE,   /* tuple<T1, T2, ...>                                */
    TYPE_RESULT,  /* result<Ok, Err>                                   */
    TYPE_BORROW,  /* borrow<T>                                         */
} WitTypeKind;

typedef struct {
    WitTypeKind kind;
    char        ident[MAX_NAME]; /* only for TYPE_IDENT */
    int         params[4];       /* indices into type pool (-1 = omitted "_") */
    int         param_count;
} WitTypeExpr;

static WitTypeExpr g_type_pool[MAX_TYPE_NODES];
static int         g_type_pool_count = 0;

static int type_alloc(void)
{
    if (g_type_pool_count >= MAX_TYPE_NODES) {
        fprintf(stderr, "wit_codegen: type pool exhausted\n");
        return -1;
    }
    int idx = g_type_pool_count++;
    memset(&g_type_pool[idx], 0, sizeof(WitTypeExpr));
    g_type_pool[idx].params[0] = -1;
    g_type_pool[idx].params[1] = -1;
    g_type_pool[idx].params[2] = -1;
    g_type_pool[idx].params[3] = -1;
    return idx;
}

/* Stringify a type expression (for diagnostics and debug). Returns bytes written. */
static int type_to_str(int idx, char *buf, int bufsize)
{
    if (bufsize <= 0) return 0;
    if (idx < 0) {
        int n = snprintf(buf, bufsize, "_");
        return (n < bufsize) ? n : bufsize - 1;
    }
    WitTypeExpr *t = &g_type_pool[idx];
    int n = 0;

    if (t->kind == TYPE_IDENT) {
        n = snprintf(buf, bufsize, "%s", t->ident);
        return (n < bufsize) ? n : bufsize - 1;
    }

    const char *name = NULL;
    switch (t->kind) {
    case TYPE_IDENT:  name = "?";      break; /* handled above */
    case TYPE_OPTION: name = "option"; break;
    case TYPE_LIST:   name = "list";   break;
    case TYPE_TUPLE:  name = "tuple";  break;
    case TYPE_RESULT: name = "result"; break;
    case TYPE_BORROW: name = "borrow"; break;
    }

    n = snprintf(buf, bufsize, "%s<", name);
    for (int i = 0; i < t->param_count; i++) {
        if (i > 0) n += snprintf(buf + n, bufsize - n, ", ");
        n += type_to_str(t->params[i], buf + n, bufsize - n);
    }
    n += snprintf(buf + n, bufsize - n, ">");
    return (n < bufsize) ? n : bufsize - 1;
}

static void codegen_die(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "wit_codegen: ERROR: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static void codegen_die_type(const char *context, int type_idx)
{
    char typebuf[256];
    type_to_str(type_idx, typebuf, (int)sizeof(typebuf));
    codegen_die("%s: %s", context, typebuf);
}

static void appendf(char *out, int n, int *used, const char *fmt, ...)
{
    va_list ap;
    int avail;
    int written;

    if (!out || n <= 0 || !used || *used >= n) {
        return;
    }

    avail = n - *used;
    va_start(ap, fmt);
    written = vsnprintf(out + *used, (size_t)avail, fmt, ap);
    va_end(ap);
    if (written < 0) {
        return;
    }
    if (written >= avail) {
        *used = n - 1;
    } else {
        *used += written;
    }
}

static void split_access_base(const char *access,
                              char **prefix_out,
                              char **field_out)
{
    const char *arrow;
    const char *dot;
    const char *field;
    size_t prefix_len;
    size_t field_len;
    char *prefix;
    char *field_name;

    if (!access || !prefix_out || !field_out) {
        codegen_die("internal: invalid access split inputs");
    }

    arrow = strrchr(access, '>');
    dot = strrchr(access, '.');
    if (arrow && (!dot || arrow > dot)) {
        field = arrow + 1;
    } else if (dot) {
        field = dot + 1;
    } else {
        field = access;
    }

    prefix_len = (size_t)(field - access);
    field_len = strlen(field);
    if (field_len == 0) {
        codegen_die("internal: empty field in access path: %s", access);
    }

    prefix = (char *)malloc(prefix_len + 1u);
    field_name = (char *)malloc(field_len + 1u);
    if (!prefix || !field_name) {
        free(prefix);
        free(field_name);
        codegen_die("out of memory while splitting access path");
    }

    memcpy(prefix, access, prefix_len);
    prefix[prefix_len] = '\0';
    memcpy(field_name, field, field_len + 1u);

    *prefix_out = prefix;
    *field_out = field_name;
}

static void build_option_access_paths(const char *access,
                                      const char *sep,
                                      char **has_access_out,
                                      char **value_access_out)
{
    char *has_access = NULL;
    char *value_access = NULL;

    if (!access || !sep || !has_access_out || !value_access_out) {
        codegen_die("internal: invalid option access path inputs");
    }

    if (strcmp(sep, ".") == 0) {
        size_t has_len = strlen(access) + strlen(".has_v") + 1u;
        size_t value_len = strlen(access) + strlen(".v") + 1u;

        has_access = (char *)malloc(has_len);
        value_access = (char *)malloc(value_len);
        if (!has_access || !value_access) {
            free(has_access);
            free(value_access);
            codegen_die("out of memory while building option access paths");
        }

        snprintf(has_access, has_len, "%s.has_v", access);
        snprintf(value_access, value_len, "%s.v", access);
    } else {
        char *prefix = NULL;
        char *field = NULL;
        size_t has_len;

        split_access_base(access, &prefix, &field);
        has_len = strlen(prefix) + strlen("has_") + strlen(field) + 1u;
        has_access = (char *)malloc(has_len);
        value_access = (char *)malloc(strlen(access) + 1u);
        if (!has_access || !value_access) {
            free(prefix);
            free(field);
            free(has_access);
            free(value_access);
            codegen_die("out of memory while building option access paths");
        }

        snprintf(has_access, has_len, "%shas_%s", prefix, field);
        memcpy(value_access, access, strlen(access) + 1u);
        free(prefix);
        free(field);
    }

    *has_access_out = has_access;
    *value_access_out = value_access;
}

static void build_result_access_paths(const char *access,
                                      const char *sep,
                                      char **is_ok_access_out,
                                      char **ok_access_out,
                                      char **err_access_out)
{
    char *is_ok_access;
    char *ok_access;
    char *err_access;

    if (!access || !sep || !is_ok_access_out || !ok_access_out || !err_access_out) {
        codegen_die("internal: invalid result access path inputs");
    }

    if (strcmp(sep, ".") == 0) {
        size_t is_ok_len = strlen(access) + strlen(".is_v_ok") + 1u;
        size_t ok_len = strlen(access) + strlen(".v_val.ok.v") + 1u;
        size_t err_len = strlen(access) + strlen(".v_val.err.v") + 1u;

        is_ok_access = (char *)malloc(is_ok_len);
        ok_access = (char *)malloc(ok_len);
        err_access = (char *)malloc(err_len);
        if (!is_ok_access || !ok_access || !err_access) {
            free(is_ok_access);
            free(ok_access);
            free(err_access);
            codegen_die("out of memory while building result access paths");
        }

        snprintf(is_ok_access, is_ok_len, "%s.is_v_ok", access);
        snprintf(ok_access, ok_len, "%s.v_val.ok.v", access);
        snprintf(err_access, err_len, "%s.v_val.err.v", access);
    } else {
        char *prefix = NULL;
        char *field = NULL;
        size_t is_ok_len;
        size_t ok_len;
        size_t err_len;

        split_access_base(access, &prefix, &field);
        is_ok_len = strlen(prefix) + strlen("is_") + strlen(field) + strlen("_ok") + 1u;
        ok_len = strlen(prefix) + strlen(field) + strlen("_val.ok.v") + 1u;
        err_len = strlen(prefix) + strlen(field) + strlen("_val.err.v") + 1u;

        is_ok_access = (char *)malloc(is_ok_len);
        ok_access = (char *)malloc(ok_len);
        err_access = (char *)malloc(err_len);
        if (!is_ok_access || !ok_access || !err_access) {
            free(prefix);
            free(field);
            free(is_ok_access);
            free(ok_access);
            free(err_access);
            codegen_die("out of memory while building result access paths");
        }

        snprintf(is_ok_access, is_ok_len, "%sis_%s_ok", prefix, field);
        snprintf(ok_access, ok_len, "%s%s_val.ok.v", prefix, field);
        snprintf(err_access, err_len, "%s%s_val.err.v", prefix, field);
        free(prefix);
        free(field);
    }

    *is_ok_access_out = is_ok_access;
    *ok_access_out = ok_access;
    *err_access_out = err_access;
}

/* ------------------------------------------------------------------ */
/* AST types                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    char name[MAX_NAME];
    int  wit_type; /* index into g_type_pool */
} WitField;

typedef struct {
    char     name[MAX_NAME];
    WitField fields[MAX_FIELDS];
    int      field_count;
} WitRecord;

typedef struct {
    char name[MAX_NAME];
    int  payload_type; /* index into g_type_pool, or -1 */
    char trace[MAX_TRACE];
} WitVariantCase;

typedef struct {
    char           name[MAX_NAME];
    WitVariantCase cases[MAX_CASES];
    int            case_count;
} WitVariant;

typedef struct {
    char name[MAX_NAME];
    char cases[MAX_CASES][MAX_NAME];
    int  case_count;
} WitEnum;

typedef struct {
    char name[MAX_NAME];
    char bits[MAX_CASES][MAX_NAME];
    int  bit_count;
} WitFlags;

typedef struct {
    char name[MAX_NAME];
    int  target; /* index into g_type_pool */
} WitAlias;

typedef enum {
    WIT_FUNC_FREE,
    WIT_FUNC_STATIC,
    WIT_FUNC_METHOD,
    WIT_FUNC_CONSTRUCTOR,
} WitFuncKind;

typedef enum {
    WIT_OWNER_INTERFACE,
    WIT_OWNER_RESOURCE,
} WitOwnerKind;

typedef struct {
    char        name[MAX_NAME];
    char        owner_name[MAX_NAME];
    char        lower_group[MAX_NAME];
    WitFuncKind kind;
    WitOwnerKind owner_kind;
    WitField    params[MAX_FIELDS];
    int         param_count;
    int         result_type; /* index into g_type_pool, or -1 */
    char        trace[MAX_TRACE];
} WitFunc;

typedef struct {
    char name[MAX_NAME];
    int  imported;
    char lower_group[MAX_NAME];
} WitResource;

typedef struct {
    char      package_full[MAX_PACKAGE];
    char      package_namespace[MAX_NAME];
    char      package_name[MAX_NAME];
    char      package_version[MAX_NAME];
    char      package_tail_raw[MAX_NAME];
    char      package_snake[MAX_NAME];
    char      package_upper[MAX_NAME];
    char      package_camel[MAX_NAME];
    WitRecord  records[MAX_TYPES];
    int        record_count;
    WitVariant variants[MAX_TYPES];
    int        variant_count;
    WitEnum    enums[MAX_TYPES];
    int        enum_count;
    WitFlags   flags[MAX_TYPES];
    int        flags_count;
    WitAlias   aliases[MAX_TYPES];
    int        alias_count;
    WitResource resources[MAX_TYPES];
    int         resource_count;
    WitFunc     funcs[MAX_FUNCS];
    int         func_count;
} WitRegistry;

/* ------------------------------------------------------------------ */
/* Scanner (lexical only — no bracket balancing)                      */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *src;
    int         pos;
    int         len;
    int         line;
    int         col;
    char        pending_lower_group[MAX_NAME];
} Scanner;

static void scanner_init(Scanner *s, const char *src, int len)
{
    s->src  = src;
    s->pos  = 0;
    s->len  = len;
    s->line = 1;
    s->col  = 1;
    s->pending_lower_group[0] = '\0';
}

static int scanner_eof(const Scanner *s)
{
    return s->pos >= s->len;
}

static char scanner_peek(const Scanner *s)
{
    if (scanner_eof(s)) return '\0';
    return s->src[s->pos];
}

static char scanner_advance(Scanner *s)
{
    if (scanner_eof(s)) return '\0';
    char ch = s->src[s->pos++];
    if (ch == '\n') { s->line++; s->col = 1; }
    else            { s->col++; }
    return ch;
}

static int scan_ident(Scanner *s, char *buf, int bufsize);

static void skip_balanced_parens(Scanner *s)
{
    int depth = 0;

    if (scanner_peek(s) != '(') {
        return;
    }

    while (!scanner_eof(s)) {
        char ch = scanner_advance(s);
        if (ch == '(') {
            depth++;
        } else if (ch == ')') {
            depth--;
            if (depth <= 0) {
                return;
            }
        }
    }
}

static void scanner_clear_pending_attrs(Scanner *s)
{
    if (!s) {
        return;
    }
    s->pending_lower_group[0] = '\0';
}

static void scanner_take_pending_lower_group(Scanner *s, char *out, int n)
{
    if (!out || n <= 0) {
        return;
    }
    out[0] = '\0';
    if (!s) {
        return;
    }
    if (s->pending_lower_group[0] != '\0') {
        snprintf(out, n, "%s", s->pending_lower_group);
    }
    s->pending_lower_group[0] = '\0';
}

static void skip_attribute(Scanner *s)
{
    char attr_name[MAX_NAME];
    int attr_name_len = 0;

    if (scanner_peek(s) != '@') {
        return;
    }

    scanner_advance(s);
    while (!scanner_eof(s)) {
        char ch = scanner_peek(s);
        if (isalnum((unsigned char)ch) || ch == '-' || ch == '_' || ch == '.') {
            if (attr_name_len < MAX_NAME - 1) {
                attr_name[attr_name_len++] = ch;
            }
            scanner_advance(s);
            continue;
        }
        break;
    }
    attr_name[attr_name_len] = '\0';

    while (!scanner_eof(s)) {
        char ch = scanner_peek(s);
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            scanner_advance(s);
            continue;
        }
        break;
    }

    if (scanner_peek(s) == '(') {
        if (strcmp(attr_name, "croft-lower-group") == 0) {
            scanner_advance(s);
            while (!scanner_eof(s)) {
                char ch = scanner_peek(s);
                if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                    scanner_advance(s);
                    continue;
                }
                break;
            }
            if (!scan_ident(s, s->pending_lower_group, (int)sizeof(s->pending_lower_group))) {
                s->pending_lower_group[0] = '\0';
            }
            while (!scanner_eof(s) && scanner_peek(s) != ')') {
                scanner_advance(s);
            }
            if (scanner_peek(s) == ')') {
                scanner_advance(s);
            }
        } else {
            skip_balanced_parens(s);
        }
    }
}

static void skip_whitespace(Scanner *s)
{
    while (!scanner_eof(s)) {
        char ch = scanner_peek(s);
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            scanner_advance(s);
        } else if (ch == '/' && s->pos + 1 < s->len && s->src[s->pos + 1] == '/') {
            while (!scanner_eof(s) && scanner_peek(s) != '\n')
                scanner_advance(s);
        } else if (ch == '/' && s->pos + 1 < s->len && s->src[s->pos + 1] == '*') {
            scanner_advance(s);
            scanner_advance(s);
            while (!scanner_eof(s)) {
                if (scanner_peek(s) == '*' && s->pos + 1 < s->len && s->src[s->pos + 1] == '/') {
                    scanner_advance(s);
                    scanner_advance(s);
                    break;
                }
                scanner_advance(s);
            }
        } else if (ch == '@') {
            skip_attribute(s);
        } else {
            break;
        }
    }
}

static int match_char(Scanner *s, char expected)
{
    skip_whitespace(s);
    if (scanner_peek(s) != expected) {
        return 0;
    }
    scanner_advance(s);
    return 1;
}

static int match_arrow(Scanner *s)
{
    skip_whitespace(s);
    if (s->pos + 1 >= s->len) {
        return 0;
    }
    if (s->src[s->pos] != '-' || s->src[s->pos + 1] != '>') {
        return 0;
    }
    scanner_advance(s);
    scanner_advance(s);
    return 1;
}

static int scan_ident(Scanner *s, char *buf, int bufsize)
{
    skip_whitespace(s);
    int i = 0;
    while (!scanner_eof(s) && i < bufsize - 1) {
        char ch = scanner_peek(s);
        if (isalnum((unsigned char)ch) || ch == '-' || ch == '_') {
            buf[i++] = scanner_advance(s);
        } else {
            break;
        }
    }
    buf[i] = '\0';
    return i;
}

static int expect_char(Scanner *s, char expected)
{
    skip_whitespace(s);
    if (scanner_peek(s) == expected) {
        scanner_advance(s);
        return 1;
    }
    fprintf(stderr, "wit_codegen: line %d col %d: expected '%c', got '%c'\n",
            s->line, s->col, expected, scanner_peek(s));
    return 0;
}

static int match_keyword(Scanner *s, const char *kw)
{
    skip_whitespace(s);
    int kwlen = (int)strlen(kw);
    if (s->pos + kwlen > s->len) return 0;
    if (memcmp(s->src + s->pos, kw, kwlen) != 0) return 0;
    if (s->pos + kwlen < s->len) {
        char next = s->src[s->pos + kwlen];
        if (isalnum((unsigned char)next) || next == '-' || next == '_')
            return 0;
    }
    for (int i = 0; i < kwlen; i++)
        scanner_advance(s);
    return 1;
}

static int is_c_keyword(const char *ident)
{
    static const char *keywords[] = {
        "auto", "break", "case", "char", "const", "continue", "default",
        "do", "double", "else", "enum", "extern", "float", "for", "goto",
        "if", "inline", "int", "long", "register", "restrict", "return",
        "short", "signed", "sizeof", "static", "struct", "switch",
        "typedef", "union", "unsigned", "void", "volatile", "while",
        "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic",
        "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local",
        "alignas", "alignof", "bool", "complex", "imaginary", "noreturn",
        "static_assert", "thread_local", NULL
    };
    for (const char **kw = keywords; *kw; kw++) {
        if (strcmp(*kw, ident) == 0) return 1;
    }
    return 0;
}

static void wit_name_to_snake_raw(const char *in, char *out, int n)
{
    int i = 0;
    int wrote_sep = 1;

    if (n <= 0) return;
    while (*in && i < n - 1) {
        unsigned char ch = (unsigned char)*in++;
        if (isalnum(ch)) {
            out[i++] = (char)tolower(ch);
            wrote_sep = 0;
        } else if (!wrote_sep) {
            out[i++] = '_';
            wrote_sep = 1;
        }
    }
    while (i > 0 && out[i - 1] == '_') i--;
    out[i] = '\0';
}

static void wit_name_to_upper_raw(const char *in, char *out, int n)
{
    int i = 0;
    int wrote_sep = 1;

    if (n <= 0) return;
    while (*in && i < n - 1) {
        unsigned char ch = (unsigned char)*in++;
        if (isalnum(ch)) {
            out[i++] = (char)toupper(ch);
            wrote_sep = 0;
        } else if (!wrote_sep) {
            out[i++] = '_';
            wrote_sep = 1;
        }
    }
    while (i > 0 && out[i - 1] == '_') i--;
    out[i] = '\0';
}

static void wit_name_to_camel_raw(const char *in, char *out, int n)
{
    int i = 0;
    int cap = 1;

    if (n <= 0) return;
    while (*in && i < n - 1) {
        unsigned char ch = (unsigned char)*in++;
        if (!isalnum(ch)) {
            cap = 1;
            continue;
        }
        if (cap) {
            out[i++] = (char)toupper(ch);
            cap = 0;
        } else {
            out[i++] = (char)tolower(ch);
        }
    }
    out[i] = '\0';
}

static void wit_name_to_snake_ident(const char *in, char *out, int n)
{
    char raw[MAX_NAME];

    wit_name_to_snake_raw(in, raw, (int)sizeof(raw));
    if (raw[0] == '\0') {
        snprintf(out, n, "wit_value");
        return;
    }
    if (isdigit((unsigned char)raw[0]) || is_c_keyword(raw)) {
        snprintf(out, n, "wit_%s", raw);
        return;
    }
    snprintf(out, n, "%s", raw);
}

static void wit_name_to_upper_ident(const char *in, char *out, int n)
{
    char raw[MAX_NAME];

    wit_name_to_upper_raw(in, raw, (int)sizeof(raw));
    if (raw[0] == '\0') {
        snprintf(out, n, "WIT_VALUE");
        return;
    }
    if (isdigit((unsigned char)raw[0])) {
        snprintf(out, n, "WIT_%s", raw);
        return;
    }
    snprintf(out, n, "%s", raw);
}

static void wit_name_to_camel_ident(const char *in, char *out, int n)
{
    char raw[MAX_NAME];

    wit_name_to_camel_raw(in, raw, (int)sizeof(raw));
    if (raw[0] == '\0') {
        snprintf(out, n, "WitValue");
        return;
    }
    if (isdigit((unsigned char)raw[0])) {
        snprintf(out, n, "Wit%s", raw);
        return;
    }
    snprintf(out, n, "%s", raw);
}

static void wit_trim(char *buf)
{
    size_t len;
    size_t start = 0;

    if (!buf) return;
    len = strlen(buf);
    while (start < len && isspace((unsigned char)buf[start])) start++;
    while (len > start && isspace((unsigned char)buf[len - 1])) len--;
    if (start > 0 && len > start) memmove(buf, buf + start, len - start);
    if (start >= len) {
        buf[0] = '\0';
        return;
    }
    buf[len - start] = '\0';
}

static void path_basename(const char *path, char *out, int n)
{
    const char *base = path;

    if (!out || n <= 0) return;
    if (!path || path[0] == '\0') {
        snprintf(out, n, "<unknown>");
        return;
    }

    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    if (!base || base[0] == '\0') base = path;
    snprintf(out, n, "%s", base);
}

static void path_dirname(const char *path, char *out, int n)
{
    const char *last_sep = NULL;
    size_t len;

    if (!out || n <= 0) return;
    out[0] = '\0';
    if (!path || path[0] == '\0') return;

    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') last_sep = p;
    }

    if (!last_sep) {
        snprintf(out, n, ".");
        return;
    }

    len = (size_t)(last_sep - path);
    if (len == 0u) len = 1u; /* Preserve the filesystem root. */
    if (len >= (size_t)n) len = (size_t)n - 1u;
    memcpy(out, path, len);
    out[len] = '\0';
}

static int file_exists(const char *path)
{
    FILE *f;

    if (!path || path[0] == '\0') return 0;
    f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int find_project_root_from_path(const char *path, char *out, int n)
{
    char current[MAX_PATH_TEXT];
    char parent[MAX_PATH_TEXT];
    char candidate[MAX_PATH_TEXT];

    if (!path || !out || n <= 0) return 0;
    path_dirname(path, current, (int)sizeof(current));
    if (current[0] == '\0') return 0;

    while (current[0] != '\0') {
        snprintf(candidate, sizeof(candidate), "%s/%s", current, "CMakeLists.txt");
        if (file_exists(candidate)) {
            snprintf(out, n, "%s", current);
            return 1;
        }

        path_dirname(current, parent, (int)sizeof(parent));
        if (strcmp(parent, current) == 0) break;
        snprintf(current, sizeof(current), "%s", parent);
    }

    return 0;
}

static void path_to_project_relative(const char *path, char *out, int n)
{
    char root[MAX_PATH_TEXT];
    char project_name[MAX_NAME];
    const char *suffix;

    if (!out || n <= 0) return;
    if (!path || path[0] == '\0') {
        snprintf(out, n, "<unknown>");
        return;
    }

    if (!find_project_root_from_path(path, root, (int)sizeof(root))) {
        snprintf(out, n, "%s", path);
        return;
    }

    path_basename(root, project_name, (int)sizeof(project_name));
    suffix = path + strlen(root);
    while (*suffix == '/' || *suffix == '\\') suffix++;

    if (*suffix != '\0') snprintf(out, n, "%s/%s", project_name, suffix);
    else                 snprintf(out, n, "%s", project_name);
}

static void emit_trace_comment(FILE *out, const char *indent, const char *trace)
{
    if (!out || !trace || trace[0] == '\0') return;
    fprintf(out, "%s/* %s */\n", indent ? indent : "", trace);
}

static void format_field_trace(const WitField *field, char *out, int n)
{
    char typebuf[256];

    if (!out || n <= 0) return;
    out[0] = '\0';
    if (!field) return;

    type_to_str(field->wit_type, typebuf, (int)sizeof(typebuf));
    snprintf(out, n, "%s: %s", field->name, typebuf);
}

static void format_func_trace(const WitFunc *func, char *out, int n)
{
    int used = 0;

    if (!out || n <= 0) return;
    out[0] = '\0';
    if (!func) return;

    if (func->kind == WIT_FUNC_CONSTRUCTOR) {
        appendf(out, n, &used, "constructor(");
    } else {
        appendf(out, n, &used, "%s: ", func->name);
        if (func->kind == WIT_FUNC_STATIC) {
            appendf(out, n, &used, "static ");
        }
        appendf(out, n, &used, "func(");
    }

    for (int i = 0; i < func->param_count; i++) {
        char typebuf[256];

        if (i > 0) {
            appendf(out, n, &used, ", ");
        }
        type_to_str(func->params[i].wit_type, typebuf, (int)sizeof(typebuf));
        appendf(out, n, &used, "%s: %s", func->params[i].name, typebuf);
    }
    appendf(out, n, &used, ")");

    if (func->result_type >= 0) {
        char typebuf[256];

        type_to_str(func->result_type, typebuf, (int)sizeof(typebuf));
        appendf(out, n, &used, " -> %s", typebuf);
    }
}

static void format_variant_case_trace(const WitVariantCase *variant_case, char *out, int n)
{
    char typebuf[256];

    if (!out || n <= 0) return;
    out[0] = '\0';
    if (!variant_case) return;
    if (variant_case->trace[0] != '\0') {
        snprintf(out, n, "%s", variant_case->trace);
        return;
    }

    if (variant_case->payload_type >= 0) {
        type_to_str(variant_case->payload_type, typebuf, (int)sizeof(typebuf));
        snprintf(out, n, "%s(%s)", variant_case->name, typebuf);
    } else {
        snprintf(out, n, "%s", variant_case->name);
    }
}

/* ------------------------------------------------------------------ */
/* Recursive descent parser                                           */
/* ------------------------------------------------------------------ */

static int parse_generic_param(Scanner *s, int *param_out);

/*
 * type_expr = ident
 *           | ident '<' type_expr (',' type_expr)* '>'
 *
 * The recursion handles arbitrary nesting (option<list<u8>>) naturally
 * through the call stack — no bracket counting needed.
 */
static int parse_type_expr(Scanner *s)
{
    char name[MAX_NAME];
    if (!scan_ident(s, name, MAX_NAME)) return -1;

    skip_whitespace(s);
    if (scanner_peek(s) != '<') {
        /* bare identifier */
        int idx = type_alloc();
        if (idx < 0) return -1;
        g_type_pool[idx].kind = TYPE_IDENT;
        strncpy(g_type_pool[idx].ident, name, MAX_NAME - 1);
        return idx;
    }

    /* generic type: name<params...> */
    scanner_advance(s); /* consume '<' */

    WitTypeKind kind;
    if      (strcmp(name, "option") == 0) kind = TYPE_OPTION;
    else if (strcmp(name, "list")   == 0) kind = TYPE_LIST;
    else if (strcmp(name, "tuple")  == 0) kind = TYPE_TUPLE;
    else if (strcmp(name, "result") == 0) kind = TYPE_RESULT;
    else if (strcmp(name, "borrow") == 0) kind = TYPE_BORROW;
    else {
        fprintf(stderr, "wit_codegen: line %d: unknown generic '%s'\n",
                s->line, name);
        return -1;
    }

    int idx = type_alloc();
    if (idx < 0) return -1;
    g_type_pool[idx].kind = kind;

    /* parse comma-separated type parameters via recursion */
    while (g_type_pool[idx].param_count < 4) {
        int param = -1;
        if (!parse_generic_param(s, &param)) return -1;
        g_type_pool[idx].params[g_type_pool[idx].param_count++] = param;

        skip_whitespace(s);
        if (scanner_peek(s) == ',') {
            scanner_advance(s);
            continue;
        }
        break;
    }

    if (!expect_char(s, '>')) return -1;
    return idx;
}

static int parse_generic_param(Scanner *s, int *param_out)
{
    skip_whitespace(s);
    if (scanner_peek(s) == '_') {
        scanner_advance(s);
        *param_out = -1;
        return 1;
    }

    *param_out = parse_type_expr(s);
    return *param_out >= 0;
}

static int parse_record(Scanner *s, WitRecord *rec)
{
    if (!scan_ident(s, rec->name, MAX_NAME)) return 0;
    if (!expect_char(s, '{')) return 0;
    rec->field_count = 0;
    while (rec->field_count < MAX_FIELDS) {
        skip_whitespace(s);
        if (scanner_peek(s) == '}') { scanner_advance(s); return 1; }
        WitField *f = &rec->fields[rec->field_count];
        if (!scan_ident(s, f->name, MAX_NAME)) return 0;
        if (!expect_char(s, ':')) return 0;
        f->wit_type = parse_type_expr(s);
        if (f->wit_type < 0) return 0;
        expect_char(s, ',');
        rec->field_count++;
    }
    return expect_char(s, '}');
}

static int parse_variant(Scanner *s, WitVariant *var)
{
    if (!scan_ident(s, var->name, MAX_NAME)) return 0;
    if (!expect_char(s, '{')) return 0;
    var->case_count = 0;
    while (var->case_count < MAX_CASES) {
        skip_whitespace(s);
        if (scanner_peek(s) == '}') { scanner_advance(s); return 1; }
        WitVariantCase *c = &var->cases[var->case_count];
        memset(c, 0, sizeof(*c));
        if (!scan_ident(s, c->name, MAX_NAME)) return 0;
        c->payload_type = -1;
        skip_whitespace(s);
        if (scanner_peek(s) == '(') {
            scanner_advance(s);
            c->payload_type = parse_type_expr(s);
            if (c->payload_type < 0) return 0;
            if (!expect_char(s, ')')) return 0;
        }
        expect_char(s, ',');
        var->case_count++;
    }
    return expect_char(s, '}');
}

static int parse_enum(Scanner *s, WitEnum *en)
{
    if (!scan_ident(s, en->name, MAX_NAME)) return 0;
    if (!expect_char(s, '{')) return 0;
    en->case_count = 0;
    while (en->case_count < MAX_CASES) {
        skip_whitespace(s);
        if (scanner_peek(s) == '}') { scanner_advance(s); return 1; }
        if (!scan_ident(s, en->cases[en->case_count], MAX_NAME)) return 0;
        expect_char(s, ',');
        en->case_count++;
    }
    return expect_char(s, '}');
}

static int parse_flags(Scanner *s, WitFlags *fl)
{
    if (!scan_ident(s, fl->name, MAX_NAME)) return 0;
    if (!expect_char(s, '{')) return 0;
    fl->bit_count = 0;
    while (fl->bit_count < MAX_CASES) {
        skip_whitespace(s);
        if (scanner_peek(s) == '}') { scanner_advance(s); return 1; }
        if (!scan_ident(s, fl->bits[fl->bit_count], MAX_NAME)) return 0;
        expect_char(s, ',');
        fl->bit_count++;
    }
    return expect_char(s, '}');
}

static int parse_alias(Scanner *s, WitAlias *alias)
{
    if (!scan_ident(s, alias->name, MAX_NAME)) return 0;
    if (!expect_char(s, '=')) return 0;
    alias->target = parse_type_expr(s);
    if (alias->target < 0) return 0;
    expect_char(s, ';');
    return 1;
}

typedef enum {
    PARSE_SCOPE_TOP,
    PARSE_SCOPE_INTERFACE,
    PARSE_SCOPE_RESOURCE,
} ParseScope;

static int parse_scope_items(Scanner *s,
                             WitRegistry *reg,
                             ParseScope scope,
                             const char *scope_name);

static int registry_add_resource(WitRegistry *reg, const char *name, int imported)
{
    if (!reg || !name || name[0] == '\0') {
        return 0;
    }

    for (int i = 0; i < reg->resource_count; i++) {
        if (strcmp(reg->resources[i].name, name) == 0) {
            if (imported) {
                reg->resources[i].imported = 1;
            }
            return 1;
        }
    }

    if (reg->resource_count >= MAX_TYPES) {
        fprintf(stderr, "wit_codegen: too many resources\n");
        return 0;
    }

    memset(&reg->resources[reg->resource_count], 0, sizeof(reg->resources[reg->resource_count]));
    snprintf(reg->resources[reg->resource_count].name,
             sizeof(reg->resources[reg->resource_count].name),
             "%s",
             name);
    reg->resources[reg->resource_count].imported = imported ? 1 : 0;
    reg->resource_count++;
    return 1;
}

static int registry_add_func(WitRegistry *reg, const WitFunc *func)
{
    if (!reg || !func) {
        return 0;
    }
    if (reg->func_count >= MAX_FUNCS) {
        fprintf(stderr, "wit_codegen: too many funcs\n");
        return 0;
    }
    reg->funcs[reg->func_count++] = *func;
    return 1;
}

static int parse_param_list(Scanner *s, WitField *params, int *param_count_out, int max_params)
{
    int count = 0;

    if (!expect_char(s, '(')) return 0;
    while (count < max_params) {
        WitField *param;

        skip_whitespace(s);
        if (match_char(s, ')')) {
            *param_count_out = count;
            return 1;
        }

        param = &params[count];
        memset(param, 0, sizeof(*param));
        if (!scan_ident(s, param->name, MAX_NAME)) return 0;
        if (!expect_char(s, ':')) return 0;
        param->wit_type = parse_type_expr(s);
        if (param->wit_type < 0) return 0;
        count++;

        skip_whitespace(s);
        if (match_char(s, ',')) {
            continue;
        }
        if (!expect_char(s, ')')) return 0;
        *param_count_out = count;
        return 1;
    }

    return 0;
}

static int parse_named_func(Scanner *s,
                            WitRegistry *reg,
                            ParseScope scope,
                            const char *scope_name)
{
    WitFunc func;

    memset(&func, 0, sizeof(func));
    if (!scan_ident(s, func.name, MAX_NAME)) return 0;
    if (!expect_char(s, ':')) return 0;

    if (scope == PARSE_SCOPE_RESOURCE && match_keyword(s, "static")) {
        func.kind = WIT_FUNC_STATIC;
    } else {
        func.kind = (scope == PARSE_SCOPE_RESOURCE) ? WIT_FUNC_METHOD : WIT_FUNC_FREE;
    }

    if (!match_keyword(s, "func")) return 0;
    if (!parse_param_list(s, func.params, &func.param_count, MAX_FIELDS)) return 0;

    func.result_type = -1;
    if (match_arrow(s)) {
        func.result_type = parse_type_expr(s);
        if (func.result_type < 0) return 0;
    } else if (scope_name && scope_name[0] != '\0') {
        func.result_type = type_alloc();
        if (func.result_type < 0) return 0;
        g_type_pool[func.result_type].kind = TYPE_IDENT;
        snprintf(g_type_pool[func.result_type].ident,
                 sizeof(g_type_pool[func.result_type].ident),
                 "%s",
                 scope_name);
    }

    if (!expect_char(s, ';')) return 0;

    snprintf(func.owner_name, sizeof(func.owner_name), "%s", scope_name ? scope_name : "");
    scanner_take_pending_lower_group(s, func.lower_group, (int)sizeof(func.lower_group));
    func.owner_kind = (scope == PARSE_SCOPE_RESOURCE) ? WIT_OWNER_RESOURCE : WIT_OWNER_INTERFACE;
    format_func_trace(&func, func.trace, (int)sizeof(func.trace));
    return registry_add_func(reg, &func);
}

static int try_parse_named_func(Scanner *s,
                                WitRegistry *reg,
                                ParseScope scope,
                                const char *scope_name)
{
    Scanner probe = *s;
    char name[MAX_NAME];

    if (!scan_ident(&probe, name, MAX_NAME)) {
        return 0;
    }
    if (!match_char(&probe, ':')) {
        return 0;
    }
    if (scope == PARSE_SCOPE_RESOURCE) {
        (void)match_keyword(&probe, "static");
    }
    if (!match_keyword(&probe, "func")) {
        return 0;
    }
    return parse_named_func(s, reg, scope, scope_name);
}

static int parse_constructor_func(Scanner *s,
                                  WitRegistry *reg,
                                  const char *scope_name)
{
    WitFunc func;

    memset(&func, 0, sizeof(func));
    snprintf(func.name, sizeof(func.name), "constructor");
    func.kind = WIT_FUNC_CONSTRUCTOR;
    func.owner_kind = WIT_OWNER_RESOURCE;
    snprintf(func.owner_name, sizeof(func.owner_name), "%s", scope_name ? scope_name : "");

    if (!match_keyword(s, "constructor")) return 0;
    if (!parse_param_list(s, func.params, &func.param_count, MAX_FIELDS)) return 0;

    func.result_type = -1;
    if (match_arrow(s)) {
        func.result_type = parse_type_expr(s);
        if (func.result_type < 0) return 0;
    }

    if (!expect_char(s, ';')) return 0;
    scanner_clear_pending_attrs(s);
    format_func_trace(&func, func.trace, (int)sizeof(func.trace));
    return registry_add_func(reg, &func);
}

static int try_parse_constructor_func(Scanner *s,
                                      WitRegistry *reg,
                                      const char *scope_name)
{
    Scanner probe = *s;
    if (!match_keyword(&probe, "constructor")) {
        return 0;
    }
    return parse_constructor_func(s, reg, scope_name);
}

static int parse_use_decl(Scanner *s, WitRegistry *reg)
{
    char raw[512];
    int i = 0;
    char *open_brace;
    char *close_brace;
    char *cursor;

    skip_whitespace(s);
    while (!scanner_eof(s) && scanner_peek(s) != ';' && i < (int)sizeof(raw) - 1) {
        raw[i++] = scanner_advance(s);
    }
    raw[i] = '\0';
    if (!expect_char(s, ';')) return 0;

    open_brace = strchr(raw, '{');
    close_brace = strrchr(raw, '}');
    if (!open_brace || !close_brace || close_brace <= open_brace) {
        return 1;
    }

    *close_brace = '\0';
    cursor = open_brace + 1;
    while (*cursor) {
        char name[MAX_NAME];
        int j = 0;

        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r' || *cursor == ',') {
            cursor++;
        }
        while (*cursor && *cursor != ',' && *cursor != ' ' && *cursor != '\t'
                && *cursor != '\n' && *cursor != '\r' && j < MAX_NAME - 1) {
            name[j++] = *cursor++;
        }
        name[j] = '\0';
        if (name[0] != '\0' && !registry_add_resource(reg, name, 1)) {
            return 0;
        }
        while (*cursor && *cursor != ',') {
            cursor++;
        }
        if (*cursor == ',') {
            cursor++;
        }
    }

    return 1;
}

static void skip_braced_block(Scanner *s)
{
    int depth = 0;

    skip_whitespace(s);
    if (scanner_peek(s) != '{') {
        return;
    }

    while (!scanner_eof(s)) {
        char ch = scanner_advance(s);
        if (ch == '{') {
            depth++;
        } else if (ch == '}') {
            depth--;
            if (depth <= 0) {
                return;
            }
        }
    }
}

static int parse_interface_decl(Scanner *s, WitRegistry *reg)
{
    char name[MAX_NAME];

    if (!scan_ident(s, name, MAX_NAME)) return 0;
    if (!expect_char(s, '{')) return 0;
    return parse_scope_items(s, reg, PARSE_SCOPE_INTERFACE, name);
}

static int parse_resource_decl(Scanner *s, WitRegistry *reg)
{
    char name[MAX_NAME];
    char lower_group[MAX_NAME];
    int resource_index = -1;

    if (!scan_ident(s, name, MAX_NAME)) return 0;
    scanner_take_pending_lower_group(s, lower_group, (int)sizeof(lower_group));
    if (!registry_add_resource(reg, name, 0)) return 0;
    for (int i = 0; i < reg->resource_count; i++) {
        if (strcmp(reg->resources[i].name, name) == 0) {
            resource_index = i;
            break;
        }
    }
    if (resource_index >= 0 && lower_group[0] != '\0') {
        snprintf(reg->resources[resource_index].lower_group,
                 sizeof(reg->resources[resource_index].lower_group),
                 "%s",
                 lower_group);
    }

    skip_whitespace(s);
    if (match_char(s, ';')) {
        return 1;
    }
    if (!expect_char(s, '{')) return 0;
    if (!parse_scope_items(s, reg, PARSE_SCOPE_RESOURCE, name)) return 0;
    (void)match_char(s, ';');
    return 1;
}

static int parse_package_decl(Scanner *s, WitRegistry *reg)
{
    char raw[MAX_PACKAGE];
    int i = 0;
    const char *cursor;
    const char *colon;
    const char *at;

    skip_whitespace(s);
    while (!scanner_eof(s) && scanner_peek(s) != ';' && i < MAX_PACKAGE - 1) {
        raw[i++] = scanner_advance(s);
    }
    raw[i] = '\0';
    if (!expect_char(s, ';')) return 0;

    wit_trim(raw);
    if (raw[0] == '\0') {
        codegen_die("package declaration is empty");
    }

    snprintf(reg->package_full, sizeof(reg->package_full), "%s", raw);

    cursor = raw;
    colon = strchr(cursor, ':');
    at = strrchr(cursor, '@');
    if (colon) {
        size_t namespace_len = (size_t)(colon - cursor);
        if (namespace_len >= sizeof(reg->package_namespace))
            namespace_len = sizeof(reg->package_namespace) - 1u;
        memcpy(reg->package_namespace, cursor, namespace_len);
        reg->package_namespace[namespace_len] = '\0';
        cursor = colon + 1;
    }

    if (at && at > cursor) {
        size_t name_len = (size_t)(at - cursor);
        if (name_len >= sizeof(reg->package_name))
            name_len = sizeof(reg->package_name) - 1u;
        memcpy(reg->package_name, cursor, name_len);
        reg->package_name[name_len] = '\0';
        snprintf(reg->package_version, sizeof(reg->package_version), "%s", at + 1);
    } else {
        snprintf(reg->package_name, sizeof(reg->package_name), "%s", cursor);
    }

    return 1;
}

static void finalize_package_info(WitRegistry *reg, const char *wit_path)
{
    const char *base = wit_path;
    const char *tail = NULL;
    char fallback[MAX_NAME];
    const char *dot;

    if (!reg) return;

    if (reg->package_name[0] == '\0') {
        if (wit_path) {
            for (const char *p = wit_path; *p; p++) {
                if (*p == '/' || *p == '\\') base = p + 1;
            }
            snprintf(fallback, sizeof(fallback), "%s", base);
            dot = strrchr(fallback, '.');
            if (dot) {
                fallback[dot - fallback] = '\0';
            }
        } else {
            snprintf(fallback, sizeof(fallback), "anonymous-schema");
        }
        snprintf(reg->package_name, sizeof(reg->package_name), "%s", fallback);
        snprintf(reg->package_full, sizeof(reg->package_full), "%s", reg->package_name);
    }

    tail = strrchr(reg->package_name, '-');
    if (tail && tail[1] != '\0') {
        snprintf(reg->package_tail_raw, sizeof(reg->package_tail_raw), "%s", tail + 1);
    } else {
        snprintf(reg->package_tail_raw, sizeof(reg->package_tail_raw), "%s", reg->package_name);
    }

    wit_name_to_snake_ident(reg->package_name, reg->package_snake,
                            (int)sizeof(reg->package_snake));
    wit_name_to_upper_ident(reg->package_name, reg->package_upper,
                            (int)sizeof(reg->package_upper));
    wit_name_to_camel_ident(reg->package_name, reg->package_camel,
                            (int)sizeof(reg->package_camel));
}

static int parse_scope_items(Scanner *s,
                             WitRegistry *reg,
                             ParseScope scope,
                             const char *scope_name)
{
    while (!scanner_eof(s)) {
        skip_whitespace(s);
        if (scanner_eof(s)) break;
        if (scope != PARSE_SCOPE_TOP && scanner_peek(s) == '}') {
            scanner_advance(s);
            return 1;
        }

        if (scope == PARSE_SCOPE_RESOURCE && try_parse_constructor_func(s, reg, scope_name)) {
            continue;
        } else if ((scope == PARSE_SCOPE_INTERFACE || scope == PARSE_SCOPE_RESOURCE)
                && try_parse_named_func(s, reg, scope, scope_name)) {
            continue;
        } else if (match_keyword(s, "package")) {
            scanner_clear_pending_attrs(s);
            if (!parse_package_decl(s, reg)) return 0;
        } else if (match_keyword(s, "use")) {
            scanner_clear_pending_attrs(s);
            if (!parse_use_decl(s, reg)) return 0;
        } else if (match_keyword(s, "world")) {
            char world_name[MAX_NAME];

            scanner_clear_pending_attrs(s);
            if (!scan_ident(s, world_name, MAX_NAME)) return 0;
            skip_whitespace(s);
            if (match_char(s, ';')) {
                continue;
            }
            skip_braced_block(s);
        } else if (match_keyword(s, "interface")) {
            scanner_clear_pending_attrs(s);
            if (!parse_interface_decl(s, reg)) return 0;
        } else if (match_keyword(s, "export") || match_keyword(s, "import")) {
            scanner_clear_pending_attrs(s);
            while (!scanner_eof(s) && scanner_peek(s) != '{' && scanner_peek(s) != ';')
                scanner_advance(s);
            if (match_char(s, ';')) {
                continue;
            }
            skip_braced_block(s);
        } else if (match_keyword(s, "record")) {
            scanner_clear_pending_attrs(s);
            if (reg->record_count >= MAX_TYPES) {
                fprintf(stderr, "wit_codegen: too many records\n"); return 0;
            }
            if (!parse_record(s, &reg->records[reg->record_count])) return 0;
            reg->record_count++;
        } else if (match_keyword(s, "variant")) {
            scanner_clear_pending_attrs(s);
            if (reg->variant_count >= MAX_TYPES) {
                fprintf(stderr, "wit_codegen: too many variants\n"); return 0;
            }
            if (!parse_variant(s, &reg->variants[reg->variant_count])) return 0;
            reg->variant_count++;
        } else if (match_keyword(s, "enum")) {
            scanner_clear_pending_attrs(s);
            if (reg->enum_count >= MAX_TYPES) {
                fprintf(stderr, "wit_codegen: too many enums\n"); return 0;
            }
            if (!parse_enum(s, &reg->enums[reg->enum_count])) return 0;
            reg->enum_count++;
        } else if (match_keyword(s, "flags")) {
            scanner_clear_pending_attrs(s);
            if (reg->flags_count >= MAX_TYPES) {
                fprintf(stderr, "wit_codegen: too many flags\n"); return 0;
            }
            if (!parse_flags(s, &reg->flags[reg->flags_count])) return 0;
            reg->flags_count++;
        } else if (match_keyword(s, "type")) {
            scanner_clear_pending_attrs(s);
            if (reg->alias_count >= MAX_TYPES) {
                fprintf(stderr, "wit_codegen: too many aliases\n"); return 0;
            }
            if (!parse_alias(s, &reg->aliases[reg->alias_count])) return 0;
            reg->alias_count++;
        } else if (match_keyword(s, "resource")) {
            if (!parse_resource_decl(s, reg)) return 0;
        } else {
            scanner_clear_pending_attrs(s);
            scanner_advance(s);
        }
    }
    return scope == PARSE_SCOPE_TOP;
}

static int parse_wit(Scanner *s, WitRegistry *reg)
{
    memset(reg, 0, sizeof(*reg));
    return parse_scope_items(s, reg, PARSE_SCOPE_TOP, NULL);
}

static const WitAlias *find_alias(const WitRegistry *reg, const char *name);
static const WitRecord *find_record(const WitRegistry *reg, const char *name);
static const WitVariant *find_variant(const WitRegistry *reg, const char *name);
static const WitEnum *find_enum(const WitRegistry *reg, const char *name);
static const WitFlags *find_flags(const WitRegistry *reg, const char *name);
static const WitResource *find_resource(const WitRegistry *reg, const char *name);
static int resolve_type(const WitRegistry *reg, int type_idx);
static int unwrap_borrow_type(const WitRegistry *reg, int type_idx);
static const char *prim_c_type(const char *name);

static int same_type_shape(const WitRegistry *reg, int a, int b)
{
    char a_buf[256];
    char b_buf[256];
    int a_resolved = unwrap_borrow_type(reg, a);
    int b_resolved = unwrap_borrow_type(reg, b);

    type_to_str(a_resolved, a_buf, (int)sizeof(a_buf));
    type_to_str(b_resolved, b_buf, (int)sizeof(b_buf));
    return strcmp(a_buf, b_buf) == 0;
}

static void lowered_command_name(const char *group_name,
                                 char *out,
                                 int n)
{
    if (strcmp(group_name, "types") == 0 || strcmp(group_name, "command") == 0) {
        snprintf(out, n, "command");
        return;
    }
    snprintf(out, n, "%s-command", group_name);
}

static void lowered_reply_name(const char *group_name,
                               char *out,
                               int n)
{
    if (strcmp(group_name, "types") == 0 || strcmp(group_name, "command") == 0) {
        snprintf(out, n, "reply");
        return;
    }
    snprintf(out, n, "%s-reply", group_name);
}

static void lowered_request_record_name(const WitFunc *func,
                                        char *out,
                                        int n)
{
    if (!func) {
        if (out && n > 0) out[0] = '\0';
        return;
    }
    if (func->owner_kind == WIT_OWNER_INTERFACE) {
        snprintf(out, n, "%s", func->name);
        return;
    }
    snprintf(out, n, "%s-%s", func->owner_name, func->name);
}

static void lowered_receiver_field_name(const WitFunc *func,
                                        char *out,
                                        int n)
{
    if (!out || n <= 0) return;
    if (func && strcmp(func->name, "clone") == 0) {
        snprintf(out, n, "source");
        return;
    }
    if (func && func->owner_name[0] != '\0') {
        snprintf(out, n, "%s", func->owner_name);
        return;
    }
    snprintf(out, n, "self");
}

static int alias_result_case_name(const WitRegistry *reg,
                                  const char *alias_name,
                                  char *out,
                                  int n)
{
    char stem[MAX_NAME];
    const char *suffix = NULL;
    const char *segment = NULL;
    size_t stem_len;

    if (!reg || !alias_name || !out || n <= 0) return 0;

    if (strlen(alias_name) > 7u && strcmp(alias_name + strlen(alias_name) - 7u, "-result") == 0) {
        suffix = alias_name + strlen(alias_name) - 7u;
    } else if (strlen(alias_name) > 7u && strcmp(alias_name + strlen(alias_name) - 7u, "_result") == 0) {
        suffix = alias_name + strlen(alias_name) - 7u;
    } else {
        return 0;
    }

    stem_len = (size_t)(suffix - alias_name);
    if (stem_len >= sizeof(stem)) {
        stem_len = sizeof(stem) - 1u;
    }
    memcpy(stem, alias_name, stem_len);
    stem[stem_len] = '\0';

    if (reg->package_name[0] != '\0'
            && strncmp(stem, reg->package_name, strlen(reg->package_name)) == 0
            && (stem[strlen(reg->package_name)] == '-' || stem[strlen(reg->package_name)] == '_')) {
        memmove(stem,
                stem + strlen(reg->package_name) + 1u,
                strlen(stem + strlen(reg->package_name) + 1u) + 1u);
    } else if (reg->package_tail_raw[0] != '\0'
            && strncmp(stem, reg->package_tail_raw, strlen(reg->package_tail_raw)) == 0
            && (stem[strlen(reg->package_tail_raw)] == '-' || stem[strlen(reg->package_tail_raw)] == '_')) {
        memmove(stem,
                stem + strlen(reg->package_tail_raw) + 1u,
                strlen(stem + strlen(reg->package_tail_raw) + 1u) + 1u);
    }

    segment = strrchr(stem, '-');
    if (!segment) {
        segment = strrchr(stem, '_');
    }
    if (segment && segment[1] != '\0') {
        snprintf(out, n, "%s", segment + 1);
    } else {
        snprintf(out, n, "%s", stem);
    }
    return out[0] != '\0';
}

static int can_passthrough_record_payload(const WitRegistry *reg,
                                          const WitFunc *func)
{
    int resolved;
    WitTypeExpr *type_expr;

    if (!reg || !func) return 0;
    if (func->kind == WIT_FUNC_METHOD) return 0;
    if (func->param_count != 1) return 0;

    resolved = unwrap_borrow_type(reg, func->params[0].wit_type);
    if (resolved < 0) return 0;
    type_expr = &g_type_pool[resolved];
    return type_expr->kind == TYPE_IDENT && find_record(reg, type_expr->ident);
}

static const char *wit_func_lower_group(const WitRegistry *reg, const WitFunc *func)
{
    const WitResource *resource;

    if (!func) return "";
    if (func->lower_group[0] != '\0') {
        return func->lower_group;
    }
    if (func->owner_kind == WIT_OWNER_RESOURCE) {
        resource = find_resource(reg, func->owner_name);
        if (resource && resource->lower_group[0] != '\0') {
            return resource->lower_group;
        }
    }
    return func->owner_name;
}

static void lowered_reply_case_name(const WitRegistry *reg,
                                    const WitFunc *func,
                                    char *out,
                                    int n)
{
    const char *alias_name = NULL;
    int resolved;
    WitTypeExpr *result;
    int ok_type;
    int ok_resolved;
    WitTypeExpr *ok_expr;

    if (!func || !out || n <= 0) return;
    if (func->result_type < 0) {
        snprintf(out, n, "%s", func->name);
        return;
    }

    if (func->result_type >= 0 && g_type_pool[func->result_type].kind == TYPE_IDENT) {
        const WitAlias *result_alias = find_alias(reg, g_type_pool[func->result_type].ident);
        if (result_alias) {
            alias_name = result_alias->name;
        }
    }

    resolved = unwrap_borrow_type(reg, func->result_type);
    if (resolved < 0) {
        snprintf(out, n, "%s", func->name);
        return;
    }

    result = &g_type_pool[resolved];
    if (result->kind != TYPE_RESULT) {
        snprintf(out, n, "%s", func->name);
        return;
    }

    ok_type = (result->param_count > 0) ? result->params[0] : -1;
    if (ok_type < 0) {
        snprintf(out, n, "status");
        return;
    }

    ok_resolved = unwrap_borrow_type(reg, ok_type);
    if (ok_resolved >= 0) {
        ok_expr = &g_type_pool[ok_resolved];
        if (ok_expr->kind == TYPE_IDENT && find_resource(reg, ok_expr->ident)) {
            snprintf(out, n, "%s", ok_expr->ident);
            return;
        }
    }

    if (alias_name && alias_result_case_name(reg, alias_name, out, n)) {
        return;
    }

    snprintf(out, n, "%s", func->name);
}

static int append_lowered_record(WitRegistry *reg, const WitRecord *record)
{
    if (!reg || !record) return 0;
    if (find_record(reg, record->name) || find_variant(reg, record->name)
            || find_enum(reg, record->name) || find_flags(reg, record->name)
            || find_alias(reg, record->name) || find_resource(reg, record->name)) {
        fprintf(stderr, "wit_codegen: lowered record name conflicts with existing type: %s\n",
                record->name);
        return 0;
    }
    if (reg->record_count >= MAX_TYPES) {
        fprintf(stderr, "wit_codegen: too many records\n");
        return 0;
    }
    reg->records[reg->record_count++] = *record;
    return 1;
}

static int append_lowered_variant(WitRegistry *reg, const WitVariant *variant)
{
    if (!reg || !variant) return 0;
    if (find_record(reg, variant->name) || find_variant(reg, variant->name)
            || find_enum(reg, variant->name) || find_flags(reg, variant->name)
            || find_alias(reg, variant->name) || find_resource(reg, variant->name)) {
        fprintf(stderr, "wit_codegen: lowered variant name conflicts with existing type: %s\n",
                variant->name);
        return 0;
    }
    if (reg->variant_count >= MAX_TYPES) {
        fprintf(stderr, "wit_codegen: too many variants\n");
        return 0;
    }
    reg->variants[reg->variant_count++] = *variant;
    return 1;
}

static int lower_operation_group(WitRegistry *reg, const char *group_name)
{
    WitVariant command_variant;
    WitVariant reply_variant;
    char command_name[MAX_NAME];
    char reply_name[MAX_NAME];

    memset(&command_variant, 0, sizeof(command_variant));
    memset(&reply_variant, 0, sizeof(reply_variant));
    lowered_command_name(group_name, command_name, (int)sizeof(command_name));
    lowered_reply_name(group_name, reply_name, (int)sizeof(reply_name));
    snprintf(command_variant.name, sizeof(command_variant.name), "%s", command_name);
    snprintf(reply_variant.name, sizeof(reply_variant.name), "%s", reply_name);

    for (int i = 0; i < reg->func_count; i++) {
        const WitFunc *func = &reg->funcs[i];
        WitRecord request_record;
        char record_name[MAX_NAME];
        char reply_case_name[MAX_NAME];
        int needs_request_record = 0;
        int passthrough_record_payload = 0;

        if (strcmp(wit_func_lower_group(reg, func), group_name) != 0) {
            continue;
        }

        memset(&request_record, 0, sizeof(request_record));
        if (command_variant.case_count >= MAX_CASES) {
            fprintf(stderr, "wit_codegen: too many lowered command cases in %s\n", group_name);
            return 0;
        }

        passthrough_record_payload = can_passthrough_record_payload(reg, func);
        needs_request_record = ((func->param_count > 0 || func->kind == WIT_FUNC_METHOD)
                                && !passthrough_record_payload);
        if (needs_request_record) {
            int field_count = 0;

            lowered_request_record_name(func, record_name, (int)sizeof(record_name));
            snprintf(request_record.name, sizeof(request_record.name), "%s", record_name);

            if (func->kind == WIT_FUNC_METHOD) {
                char receiver_field_name[MAX_NAME];

                lowered_receiver_field_name(func,
                                            receiver_field_name,
                                            (int)sizeof(receiver_field_name));
                snprintf(request_record.fields[field_count].name,
                         sizeof(request_record.fields[field_count].name),
                         "%s",
                         receiver_field_name);
                request_record.fields[field_count].wit_type = type_alloc();
                if (request_record.fields[field_count].wit_type < 0) {
                    return 0;
                }
                g_type_pool[request_record.fields[field_count].wit_type].kind = TYPE_IDENT;
                snprintf(g_type_pool[request_record.fields[field_count].wit_type].ident,
                         sizeof(g_type_pool[request_record.fields[field_count].wit_type].ident),
                         "%s",
                         func->owner_name);
                field_count++;
            }

            for (int j = 0; j < func->param_count; j++) {
                request_record.fields[field_count++] = func->params[j];
            }
            request_record.field_count = field_count;

            if (!append_lowered_record(reg, &request_record)) {
                return 0;
            }
        }

        snprintf(command_variant.cases[command_variant.case_count].name,
                 sizeof(command_variant.cases[command_variant.case_count].name),
                 "%s",
                 func->name);
        snprintf(command_variant.cases[command_variant.case_count].trace,
                 sizeof(command_variant.cases[command_variant.case_count].trace),
                 "%s",
                 func->trace);
        if (needs_request_record) {
            int type_idx = type_alloc();
            if (type_idx < 0) {
                return 0;
            }
            g_type_pool[type_idx].kind = TYPE_IDENT;
            snprintf(g_type_pool[type_idx].ident, sizeof(g_type_pool[type_idx].ident), "%s", record_name);
            command_variant.cases[command_variant.case_count].payload_type = type_idx;
        } else if (passthrough_record_payload) {
            command_variant.cases[command_variant.case_count].payload_type = func->params[0].wit_type;
        } else {
            command_variant.cases[command_variant.case_count].payload_type = -1;
        }
        command_variant.case_count++;

        lowered_reply_case_name(reg, func, reply_case_name, (int)sizeof(reply_case_name));
        for (int j = 0; j < reply_variant.case_count; j++) {
            if (strcmp(reply_variant.cases[j].name, reply_case_name) == 0) {
                if (!same_type_shape(reg, reply_variant.cases[j].payload_type, func->result_type)) {
                    fprintf(stderr,
                            "wit_codegen: lowered reply case conflict in %s for case %s\n",
                            group_name,
                            reply_case_name);
                    return 0;
                }
                goto reply_case_done;
            }
        }

        if (reply_variant.case_count >= MAX_CASES) {
            fprintf(stderr, "wit_codegen: too many lowered reply cases in %s\n", group_name);
            return 0;
        }

        snprintf(reply_variant.cases[reply_variant.case_count].name,
                 sizeof(reply_variant.cases[reply_variant.case_count].name),
                 "%s",
                 reply_case_name);
        reply_variant.cases[reply_variant.case_count].payload_type = func->result_type;
        reply_variant.case_count++;

reply_case_done:
        ;
    }

    if (command_variant.case_count == 0) {
        return 1;
    }

    if (!append_lowered_variant(reg, &command_variant)) {
        return 0;
    }
    if (!append_lowered_variant(reg, &reply_variant)) {
        return 0;
    }
    return 1;
}

static int lower_operations(WitRegistry *reg)
{
    for (int i = 0; i < reg->func_count; i++) {
        int seen = 0;

        for (int j = 0; j < i; j++) {
            if (strcmp(wit_func_lower_group(reg, &reg->funcs[i]),
                       wit_func_lower_group(reg, &reg->funcs[j])) == 0) {
                seen = 1;
                break;
            }
        }
        if (seen) {
            continue;
        }
        if (!lower_operation_group(reg, wit_func_lower_group(reg, &reg->funcs[i]))) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Registry queries                                                   */
/* ------------------------------------------------------------------ */

static const WitAlias *find_alias(const WitRegistry *reg, const char *name)
{
    for (int i = 0; i < reg->alias_count; i++)
        if (strcmp(reg->aliases[i].name, name) == 0)
            return &reg->aliases[i];
    return NULL;
}

static const WitRecord *find_record(const WitRegistry *reg, const char *name)
{
    for (int i = 0; i < reg->record_count; i++)
        if (strcmp(reg->records[i].name, name) == 0)
            return &reg->records[i];
    return NULL;
}

static const WitVariant *find_variant(const WitRegistry *reg, const char *name)
{
    for (int i = 0; i < reg->variant_count; i++)
        if (strcmp(reg->variants[i].name, name) == 0)
            return &reg->variants[i];
    return NULL;
}

static const WitEnum *find_enum(const WitRegistry *reg, const char *name)
{
    for (int i = 0; i < reg->enum_count; i++)
        if (strcmp(reg->enums[i].name, name) == 0)
            return &reg->enums[i];
    return NULL;
}

static const WitFlags *find_flags(const WitRegistry *reg, const char *name)
{
    for (int i = 0; i < reg->flags_count; i++)
        if (strcmp(reg->flags[i].name, name) == 0)
            return &reg->flags[i];
    return NULL;
}

static const WitResource *find_resource(const WitRegistry *reg, const char *name)
{
    for (int i = 0; i < reg->resource_count; i++)
        if (strcmp(reg->resources[i].name, name) == 0)
            return &reg->resources[i];
    return NULL;
}

/* Resolve a type-expression index through aliases.
 * If the root is an ident that names an alias, follow the chain. */
static int resolve_type(const WitRegistry *reg, int type_idx)
{
    for (int depth = 0; depth < 16; depth++) {
        if (type_idx < 0) return type_idx;
        WitTypeExpr *t = &g_type_pool[type_idx];
        if (t->kind != TYPE_IDENT) return type_idx;
        const WitAlias *a = find_alias(reg, t->ident);
        if (!a) return type_idx;
        type_idx = a->target;
    }
    return type_idx;
}

static int unwrap_borrow_type(const WitRegistry *reg, int type_idx)
{
    for (int depth = 0; depth < 16; depth++) {
        int resolved = resolve_type(reg, type_idx);
        if (resolved < 0) return resolved;
        if (g_type_pool[resolved].kind != TYPE_BORROW) {
            return resolved;
        }
        if (g_type_pool[resolved].param_count < 1 || g_type_pool[resolved].params[0] < 0) {
            return resolved;
        }
        type_idx = g_type_pool[resolved].params[0];
    }
    return resolve_type(reg, type_idx);
}

/* Check if a type expression is a WIT primitive. */
static int is_primitive(const char *name)
{
    static const char *prims[] = {
        "s8","u8","s16","u16","s32","u32","s64","u64",
        "f32","f64","bool","char","string", NULL
    };
    for (const char **p = prims; *p; p++)
        if (strcmp(name, *p) == 0) return 1;
    return 0;
}

/* Check if a resolved type has fixed Thatch size (no skip pointer needed).
 * Used by writer emission to decide whether to emit skip pointers. */
/* Currently unused — all records get skip pointers for uniform skip.
 * Retained for potential future optimization of fixed-size records. */
__attribute__((unused))
static int is_fixed_size(const WitRegistry *reg, int type_idx)
{
    if (type_idx < 0) return 0;
    int resolved = unwrap_borrow_type(reg, type_idx);
    if (resolved < 0) return 0;
    WitTypeExpr *t = &g_type_pool[resolved];

    switch (t->kind) {
    case TYPE_IDENT:
        if (strcmp(t->ident, "string") == 0) return 0;
        if (is_primitive(t->ident)) return 1;
        if (find_resource(reg, t->ident)) return 1;
        /* check named compound types */
        if (find_enum(reg, t->ident))  return 1;
        if (find_flags(reg, t->ident)) return 1;
        if (find_variant(reg, t->ident)) return 0;
        {
            const WitRecord *rec = find_record(reg, t->ident);
            if (rec) {
                for (int i = 0; i < rec->field_count; i++)
                    if (!is_fixed_size(reg, rec->fields[i].wit_type)) return 0;
                return 1;
            }
        }
        return 0;

    case TYPE_OPTION:
    case TYPE_LIST:
    case TYPE_RESULT:
        return 0;

    case TYPE_BORROW:
        if (t->param_count < 1 || t->params[0] < 0) return 0;
        return is_fixed_size(reg, t->params[0]);

    case TYPE_TUPLE:
        for (int i = 0; i < t->param_count; i++)
            if (!is_fixed_size(reg, t->params[i])) return 0;
        return 1;
    }
    return 0;
}

static int is_list_u8(const WitRegistry *reg, const WitTypeExpr *t)
{
    if (!reg || !t) return 0;
    if (t->kind != TYPE_LIST) return 0;
    if (t->param_count != 1 || t->params[0] < 0) return 0;
    int elem = unwrap_borrow_type(reg, t->params[0]);
    if (elem < 0) return 0;
    WitTypeExpr *et = &g_type_pool[elem];
    return et->kind == TYPE_IDENT && strcmp(et->ident, "u8") == 0;
}

/* ------------------------------------------------------------------ */
/* Name conversion helpers                                            */
/* ------------------------------------------------------------------ */

static void wit_trim_leading_package_tail(const WitRegistry *reg,
                                          const char *wit_name,
                                          char *out,
                                          int n)
{
    const char *prefixes[2];
    int prefix_count = 0;
    int i;

    if (!out || n <= 0) return;
    out[0] = '\0';
    if (!wit_name) return;
    if (!reg) {
        snprintf(out, n, "%s", wit_name);
        return;
    }

    if (reg->package_name[0] != '\0') {
        prefixes[prefix_count++] = reg->package_name;
    }
    if (reg->package_tail_raw[0] != '\0'
            && (prefix_count == 0 || strcmp(reg->package_tail_raw, prefixes[0]) != 0)) {
        prefixes[prefix_count++] = reg->package_tail_raw;
    }

    for (i = 0; i < prefix_count; i++) {
        size_t prefix_len = strlen(prefixes[i]);
        const char *suffix;

        if (prefix_len == 0u || strncmp(wit_name, prefixes[i], prefix_len) != 0) {
            continue;
        }

        suffix = wit_name + prefix_len;
        if (*suffix == '\0') {
            return;
        }
        if (*suffix == '-' || *suffix == '_') {
            snprintf(out, n, "%s", suffix + 1);
            return;
        }
    }

    snprintf(out, n, "%s", wit_name);
}

static void wit_type_c_typename(const WitRegistry *reg, const char *wit_name, char *out, int n)
{
    char normalized[MAX_NAME];
    char camel[MAX_NAME];

    wit_trim_leading_package_tail(reg, wit_name, normalized, (int)sizeof(normalized));
    if (normalized[0] != '\0') {
        wit_name_to_camel_ident(normalized, camel, (int)sizeof(camel));
    } else {
        camel[0] = '\0';
    }
    if (reg->package_camel[0] != '\0' && camel[0] != '\0')
        snprintf(out, n, "SapWit%s%s", reg->package_camel, camel);
    else if (reg->package_camel[0] != '\0')
        snprintf(out, n, "SapWit%s", reg->package_camel);
    else
        snprintf(out, n, "SapWit%s", camel);
}

static void wit_resource_c_typename(const WitRegistry *reg, const char *resource_name,
                                    char *out, int n)
{
    char base[MAX_NAME];

    wit_type_c_typename(reg, resource_name, base, (int)sizeof(base));
    snprintf(out, n, "%sResource", base);
}

static void wit_macro_name(const WitRegistry *reg, const char *wit_name, char *out, int n)
{
    char normalized[MAX_NAME];
    char upper[MAX_NAME];

    wit_trim_leading_package_tail(reg, wit_name, normalized, (int)sizeof(normalized));
    if (normalized[0] != '\0') {
        wit_name_to_upper_ident(normalized, upper, (int)sizeof(upper));
    } else {
        upper[0] = '\0';
    }
    if (reg->package_upper[0] != '\0' && upper[0] != '\0')
        snprintf(out, n, "SAP_WIT_%s_%s", reg->package_upper, upper);
    else if (reg->package_upper[0] != '\0')
        snprintf(out, n, "SAP_WIT_%s", reg->package_upper);
    else
        snprintf(out, n, "SAP_WIT_%s", upper);
}

static void wit_function_suffix(const WitRegistry *reg, const char *wit_name, char *out, int n)
{
    char normalized[MAX_NAME];
    char snake[MAX_NAME];

    wit_trim_leading_package_tail(reg, wit_name, normalized, (int)sizeof(normalized));
    if (normalized[0] != '\0') {
        wit_name_to_snake_ident(normalized, snake, (int)sizeof(snake));
    } else {
        snake[0] = '\0';
    }
    if (reg->package_snake[0] != '\0' && snake[0] != '\0')
        snprintf(out, n, "%s_%s", reg->package_snake, snake);
    else if (reg->package_snake[0] != '\0')
        snprintf(out, n, "%s", reg->package_snake);
    else
        snprintf(out, n, "%s", snake);
}

static void wit_writer_name(const WitRegistry *reg, const char *wit_name, char *out, int n)
{
    char suffix[MAX_NAME * 2];

    wit_function_suffix(reg, wit_name, suffix, (int)sizeof(suffix));
    snprintf(out, n, "sap_wit_write_%s", suffix);
}

static void wit_reader_name(const WitRegistry *reg, const char *wit_name, char *out, int n)
{
    char suffix[MAX_NAME * 2];

    wit_function_suffix(reg, wit_name, suffix, (int)sizeof(suffix));
    snprintf(out, n, "sap_wit_read_%s", suffix);
}

static void wit_validator_name(const WitRegistry *reg, const char *wit_name, char *out, int n)
{
    char suffix[MAX_NAME * 2];

    wit_function_suffix(reg, wit_name, suffix, (int)sizeof(suffix));
    snprintf(out, n, "sap_wit_validate_%s", suffix);
}

static int wit_is_reply_variant(const char *wit_name)
{
    size_t len;

    if (!wit_name) return 0;
    if (strcmp(wit_name, "reply") == 0) return 1;
    len = strlen(wit_name);
    return (len > 6u && strcmp(wit_name + len - 6u, "-reply") == 0)
        || (len > 6u && strcmp(wit_name + len - 6u, "_reply") == 0);
}

static void wit_zero_name(const WitRegistry *reg, const char *wit_name, char *out, int n)
{
    char suffix[MAX_NAME * 2];

    wit_function_suffix(reg, wit_name, suffix, (int)sizeof(suffix));
    snprintf(out, n, "sap_wit_zero_%s", suffix);
}

static void wit_dispose_name(const WitRegistry *reg, const char *wit_name, char *out, int n)
{
    char suffix[MAX_NAME * 2];

    wit_function_suffix(reg, wit_name, suffix, (int)sizeof(suffix));
    snprintf(out, n, "sap_wit_dispose_%s", suffix);
}

static int wit_is_command_variant(const char *wit_name)
{
    size_t len;

    if (!wit_name) return 0;
    if (strcmp(wit_name, "command") == 0) return 1;
    len = strlen(wit_name);
    return (len > 8u && strcmp(wit_name + len - 8u, "-command") == 0)
        || (len > 8u && strcmp(wit_name + len - 8u, "_command") == 0);
}

static int wit_command_reply_name(const char *command_name, char *out, int n)
{
    size_t len;

    if (!command_name || !out || n <= 0) return 0;
    if (strcmp(command_name, "command") == 0) {
        snprintf(out, n, "reply");
        return 1;
    }

    len = strlen(command_name);
    if (len > 8u && strcmp(command_name + len - 8u, "-command") == 0) {
        snprintf(out, n, "%.*s-reply", (int)(len - 8u), command_name);
        return 1;
    }
    if (len > 8u && strcmp(command_name + len - 8u, "_command") == 0) {
        snprintf(out, n, "%.*s_reply", (int)(len - 8u), command_name);
        return 1;
    }
    return 0;
}

static const WitVariant *find_paired_reply_variant(const WitRegistry *reg,
                                                   const WitVariant *command_variant)
{
    char reply_name[MAX_NAME];

    if (!reg || !command_variant || !wit_is_command_variant(command_variant->name)) {
        return NULL;
    }
    if (!wit_command_reply_name(command_variant->name, reply_name, (int)sizeof(reply_name))) {
        return NULL;
    }
    return find_variant(reg, reply_name);
}

static void trim_suffix_in_place(char *text, const char *suffix)
{
    size_t text_len;
    size_t suffix_len;

    if (!text || !suffix) return;
    text_len = strlen(text);
    suffix_len = strlen(suffix);
    if (text_len >= suffix_len && strcmp(text + text_len - suffix_len, suffix) == 0) {
        text[text_len - suffix_len] = '\0';
    }
}

static void wit_dispatch_ops_typename(const WitRegistry *reg,
                                      const char *command_name,
                                      char *out,
                                      int n)
{
    char type_name[MAX_NAME * 2];

    wit_type_c_typename(reg, command_name, type_name, (int)sizeof(type_name));
    trim_suffix_in_place(type_name, "Command");
    snprintf(out, n, "%sDispatchOps", type_name);
}

static void wit_dispatch_name(const WitRegistry *reg, const char *command_name, char *out, int n)
{
    char suffix[MAX_NAME * 2];

    wit_function_suffix(reg, command_name, suffix, (int)sizeof(suffix));
    trim_suffix_in_place(suffix, "_command");
    snprintf(out, n, "sap_wit_dispatch_%s", suffix);
}

static void wit_payload_c_typename(const WitRegistry *reg, int payload_type, char *out, int n)
{
    int resolved;
    WitTypeExpr *type_expr;
    const char *ctype;

    if (!out || n <= 0) return;
    out[0] = '\0';
    if (!reg || payload_type < 0) return;

    resolved = unwrap_borrow_type(reg, payload_type);
    if (resolved < 0) {
        codegen_die_type("unsupported dispatch payload type", payload_type);
    }

    type_expr = &g_type_pool[resolved];
    if (type_expr->kind == TYPE_IDENT) {
        ctype = prim_c_type(type_expr->ident);
        if (ctype) {
            snprintf(out, n, "%s", ctype);
            return;
        }
        if (find_resource(reg, type_expr->ident)) {
            wit_resource_c_typename(reg, type_expr->ident, out, n);
            return;
        }
        if (find_record(reg, type_expr->ident) || find_variant(reg, type_expr->ident)) {
            wit_type_c_typename(reg, type_expr->ident, out, n);
            return;
        }
        if (find_enum(reg, type_expr->ident)) {
            snprintf(out, n, "uint8_t");
            return;
        }
        if (find_flags(reg, type_expr->ident)) {
            snprintf(out, n, "uint32_t");
            return;
        }
    }

    codegen_die_type("unsupported dispatch payload type", payload_type);
}

static int reply_case_owns_heap_ok(const WitRegistry *reg, int payload_type)
{
    int resolved;
    WitTypeExpr *payload_expr;
    int ok_type;
    int ok_resolved;
    WitTypeExpr *ok_expr;

    if (!reg || payload_type < 0) return 0;
    resolved = unwrap_borrow_type(reg, payload_type);
    if (resolved < 0) return 0;

    payload_expr = &g_type_pool[resolved];
    if (payload_expr->kind != TYPE_RESULT || payload_expr->param_count < 1) {
        return 0;
    }

    ok_type = payload_expr->params[0];
    if (ok_type < 0) return 0;
    ok_resolved = unwrap_borrow_type(reg, ok_type);
    if (ok_resolved < 0) return 0;
    ok_expr = &g_type_pool[ok_resolved];

    if (ok_expr->kind == TYPE_IDENT && strcmp(ok_expr->ident, "string") == 0) {
        return 1;
    }
    if (ok_expr->kind == TYPE_LIST && is_list_u8(reg, ok_expr)) {
        return 1;
    }
    if (ok_expr->kind == TYPE_OPTION && ok_expr->param_count > 0 && ok_expr->params[0] >= 0) {
        int inner_resolved = unwrap_borrow_type(reg, ok_expr->params[0]);
        WitTypeExpr *inner_expr;

        if (inner_resolved < 0) return 0;
        inner_expr = &g_type_pool[inner_resolved];
        if (inner_expr->kind == TYPE_IDENT && strcmp(inner_expr->ident, "string") == 0) {
            return 2;
        }
        if (inner_expr->kind == TYPE_LIST && is_list_u8(reg, inner_expr)) {
            return 2;
        }
    }

    return 0;
}

static void wit_dbi_schema_symbol(const WitRegistry *reg, char *out, int n)
{
    if (reg->package_snake[0] != '\0')
        snprintf(out, n, "sap_wit_%s_dbi_schema", reg->package_snake);
    else
        snprintf(out, n, "sap_wit_dbi_schema");
}

static void wit_dbi_schema_count_symbol(const WitRegistry *reg, char *out, int n)
{
    if (reg->package_snake[0] != '\0')
        snprintf(out, n, "sap_wit_%s_dbi_schema_count", reg->package_snake);
    else
        snprintf(out, n, "sap_wit_dbi_schema_count");
}

/* ------------------------------------------------------------------ */
/* DBI entry extraction                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    int  dbi;
    char name[MAX_NAME];
    char key_rec[MAX_NAME];
    char val_rec[MAX_NAME];
} DbiEntry;

static int extract_dbis(const WitRegistry *reg, DbiEntry *out, int max)
{
    int count = 0;
    for (int i = 0; i < reg->record_count; i++) {
        const char *rn = reg->records[i].name;
        if (strncmp(rn, "dbi", 3) != 0) continue;

        const char *p = rn + 3;
        int dbi = 0;
        while (*p >= '0' && *p <= '9') { dbi = dbi * 10 + (*p - '0'); p++; }
        if (*p != '-') continue;
        p++;

        int len = (int)strlen(p);
        int is_key = -1;
        int name_len = 0;
        if (len > 4 && strcmp(p + len - 4, "-key") == 0) {
            is_key = 1; name_len = len - 4;
        } else if (len > 6 && strcmp(p + len - 6, "-value") == 0) {
            is_key = 0; name_len = len - 6;
        } else continue;

        int slot = -1;
        for (int j = 0; j < count; j++)
            if (out[j].dbi == dbi) { slot = j; break; }
        if (slot < 0) {
            if (count >= max) return -1;
            slot = count++;
            out[slot].dbi = dbi;
            memcpy(out[slot].name, p, name_len);
            out[slot].name[name_len] = '\0';
            out[slot].key_rec[0] = '\0';
            out[slot].val_rec[0] = '\0';
        }
        if (is_key) strncpy(out[slot].key_rec, rn, MAX_NAME - 1);
        else        strncpy(out[slot].val_rec, rn, MAX_NAME - 1);
    }
    /* Sort by DBI number */
    for (int i = 0; i < count - 1; i++)
        for (int j = i + 1; j < count; j++)
            if (out[i].dbi > out[j].dbi) {
                DbiEntry tmp = out[i]; out[i] = out[j]; out[j] = tmp;
            }
    return count;
}

/* ------------------------------------------------------------------ */
/* Topological sort for struct emission ordering                      */
/* ------------------------------------------------------------------ */

static void collect_struct_deps(const WitRegistry *reg, int type_idx,
                                const char **deps, int *ndeps, int max_deps)
{
    if (type_idx < 0) return;
    int resolved = unwrap_borrow_type(reg, type_idx);
    if (resolved < 0) return;
    WitTypeExpr *t = &g_type_pool[resolved];

    switch (t->kind) {
    case TYPE_IDENT:
        if (find_record(reg, t->ident) || find_variant(reg, t->ident)) {
            for (int i = 0; i < *ndeps; i++)
                if (strcmp(deps[i], t->ident) == 0) return;
            if (*ndeps < max_deps) deps[(*ndeps)++] = t->ident;
        }
        break;
    case TYPE_OPTION:
    case TYPE_LIST:
        if (t->params[0] >= 0)
            collect_struct_deps(reg, t->params[0], deps, ndeps, max_deps);
        break;
    case TYPE_BORROW:
        if (t->param_count > 0 && t->params[0] >= 0)
            collect_struct_deps(reg, t->params[0], deps, ndeps, max_deps);
        break;
    case TYPE_TUPLE:
    case TYPE_RESULT:
        for (int i = 0; i < t->param_count; i++)
            collect_struct_deps(reg, t->params[i], deps, ndeps, max_deps);
        break;
    }
}

static int topo_sort_types(const WitRegistry *reg, const char **order, int max_order)
{
    const char *names[MAX_TYPES * 2];
    const char *dep_buf[MAX_TYPES * 2][MAX_TYPES];
    int dep_cnt[MAX_TYPES * 2];
    int n = 0;

    for (int i = 0; i < reg->record_count; i++) {
        names[n] = reg->records[i].name;
        dep_cnt[n] = 0;
        for (int j = 0; j < reg->records[i].field_count; j++)
            collect_struct_deps(reg, reg->records[i].fields[j].wit_type,
                               dep_buf[n], &dep_cnt[n], MAX_TYPES);
        n++;
    }
    for (int i = 0; i < reg->variant_count; i++) {
        names[n] = reg->variants[i].name;
        dep_cnt[n] = 0;
        for (int j = 0; j < reg->variants[i].case_count; j++)
            if (reg->variants[i].cases[j].payload_type >= 0)
                collect_struct_deps(reg, reg->variants[i].cases[j].payload_type,
                                   dep_buf[n], &dep_cnt[n], MAX_TYPES);
        n++;
    }

    /* Kahn's algorithm */
    int in_deg[MAX_TYPES * 2];
    for (int i = 0; i < n; i++) {
        in_deg[i] = 0;
        for (int d = 0; d < dep_cnt[i]; d++)
            for (int j = 0; j < n; j++)
                if (strcmp(names[j], dep_buf[i][d]) == 0) { in_deg[i]++; break; }
    }

    int done[MAX_TYPES * 2] = {0};
    int count = 0;
    while (count < n) {
        int found = -1;
        for (int i = 0; i < n; i++)
            if (!done[i] && in_deg[i] == 0) { found = i; break; }
        if (found < 0) {
            fprintf(stderr, "wit_codegen: cycle in type dependencies\n");
            return -1;
        }
        done[found] = 1;
        if (count < max_order) order[count] = names[found];
        count++;
        for (int i = 0; i < n; i++) {
            if (done[i]) continue;
            for (int d = 0; d < dep_cnt[i]; d++)
                if (strcmp(dep_buf[i][d], names[found]) == 0) { in_deg[i]--; break; }
        }
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* C type mapping                                                     */
/* ------------------------------------------------------------------ */

static const char *prim_c_type(const char *name)
{
    if (strcmp(name, "s8")   == 0) return "int8_t";
    if (strcmp(name, "u8")   == 0) return "uint8_t";
    if (strcmp(name, "s16")  == 0) return "int16_t";
    if (strcmp(name, "u16")  == 0) return "uint16_t";
    if (strcmp(name, "s32")  == 0) return "int32_t";
    if (strcmp(name, "u32")  == 0) return "uint32_t";
    if (strcmp(name, "s64")  == 0) return "int64_t";
    if (strcmp(name, "u64")  == 0) return "uint64_t";
    if (strcmp(name, "f32")  == 0) return "float";
    if (strcmp(name, "f64")  == 0) return "double";
    if (strcmp(name, "bool") == 0) return "uint8_t";
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Struct field emission                                              */
/* ------------------------------------------------------------------ */

static void emit_c_fields(FILE *out, const WitRegistry *reg,
                           int type_idx, const char *name, const char *indent)
{
    if (type_idx < 0) codegen_die("internal: negative type index in emit_c_fields");
    int resolved = unwrap_borrow_type(reg, type_idx);
    if (resolved < 0) codegen_die("internal: unresolved type index %d in emit_c_fields", type_idx);
    WitTypeExpr *t = &g_type_pool[resolved];

    switch (t->kind) {
    case TYPE_IDENT: {
        if (strcmp(t->ident, "string") == 0) {
            fprintf(out, "%sconst uint8_t *%s_data;\n", indent, name);
            fprintf(out, "%suint32_t %s_len;\n", indent, name);
            return;
        }
        const char *ctype = prim_c_type(t->ident);
        if (ctype) {
            fprintf(out, "%s%s %s;\n", indent, ctype, name);
            return;
        }
        if (find_resource(reg, t->ident)) {
            char resource_type[MAX_NAME];
            wit_resource_c_typename(reg, t->ident, resource_type, (int)sizeof(resource_type));
            fprintf(out, "%s%s %s;\n", indent, resource_type, name);
            return;
        }
        if (find_enum(reg, t->ident)) {
            fprintf(out, "%suint8_t %s;\n", indent, name);
            return;
        }
        if (find_flags(reg, t->ident)) {
            fprintf(out, "%suint32_t %s;\n", indent, name);
            return;
        }
        /* Named record or variant — by value */
        if (find_record(reg, t->ident) || find_variant(reg, t->ident)) {
            char type_name[MAX_NAME * 2];
            wit_type_c_typename(reg, t->ident, type_name, (int)sizeof(type_name));
            fprintf(out, "%s%s %s;\n", indent, type_name, name);
            return;
        }
        codegen_die("unsupported identifier in C field emission: %s", t->ident);
        return;
    }
    case TYPE_LIST:
        fprintf(out, "%sconst uint8_t *%s_data;\n", indent, name);
        fprintf(out, "%suint32_t %s_len;\n", indent, name);
        if (!is_list_u8(reg, t)) {
            /* For generic list<T>, data points to encoded element bytes. */
            fprintf(out, "%suint32_t %s_byte_len;\n", indent, name);
        }
        return;
    case TYPE_OPTION:
        if (t->param_count < 1 || t->params[0] < 0) {
            codegen_die_type("option<T> missing inner type", resolved);
        }
        fprintf(out, "%suint8_t has_%s;\n", indent, name);
        emit_c_fields(out, reg, t->params[0], name, indent);
        return;
    case TYPE_TUPLE:
        for (int i = 0; i < t->param_count; i++) {
            char sub[MAX_NAME];
            snprintf(sub, MAX_NAME, "%s_%d", name, i);
            emit_c_fields(out, reg, t->params[i], sub, indent);
        }
        return;
    case TYPE_BORROW:
        if (t->param_count < 1 || t->params[0] < 0) {
            codegen_die_type("borrow<T> missing inner type", resolved);
        }
        emit_c_fields(out, reg, t->params[0], name, indent);
        return;
    case TYPE_RESULT: {
        fprintf(out, "%suint8_t is_%s_ok;\n", indent, name);
        int has_ok = (t->param_count > 0 && t->params[0] >= 0);
        int has_err = (t->param_count > 1 && t->params[1] >= 0);
        if (has_ok || has_err) {
            fprintf(out, "%sunion {\n", indent);
            if (has_ok) {
                char deeper[64];
                snprintf(deeper, sizeof(deeper), "%s    ", indent);
                fprintf(out, "%s    struct {\n", indent);
                emit_c_fields(out, reg, t->params[0], "v", deeper);
                fprintf(out, "%s    } ok;\n", indent);
            }
            if (has_err) {
                char deeper[64];
                snprintf(deeper, sizeof(deeper), "%s    ", indent);
                fprintf(out, "%s    struct {\n", indent);
                emit_c_fields(out, reg, t->params[1], "v", deeper);
                fprintf(out, "%s    } err;\n", indent);
            }
            fprintf(out, "%s} %s_val;\n", indent, name);
        }
        return;
    }
    }
    codegen_die("internal: unhandled WitTypeKind in emit_c_fields");
}

/* Emit a single variant union member for a case payload. */
static void emit_variant_payload(FILE *out, const WitRegistry *reg,
                                  int type_idx, const char *case_name)
{
    if (type_idx < 0) codegen_die("internal: negative type index in emit_variant_payload");
    int resolved = unwrap_borrow_type(reg, type_idx);
    if (resolved < 0) codegen_die("internal: unresolved type index %d in emit_variant_payload", type_idx);
    WitTypeExpr *t = &g_type_pool[resolved];

    if (t->kind == TYPE_IDENT) {
        if (strcmp(t->ident, "string") == 0) {
            fprintf(out, "        struct { const uint8_t *data; uint32_t len; } %s;\n", case_name);
            return;
        }
        const char *ctype = prim_c_type(t->ident);
        if (ctype) {
            fprintf(out, "        %s %s;\n", ctype, case_name);
            return;
        }
        if (find_resource(reg, t->ident)) {
            char resource_type[MAX_NAME];
            wit_resource_c_typename(reg, t->ident, resource_type, (int)sizeof(resource_type));
            fprintf(out, "        %s %s;\n", resource_type, case_name);
            return;
        }
        if (find_record(reg, t->ident) || find_variant(reg, t->ident)) {
            char type_name[MAX_NAME * 2];
            wit_type_c_typename(reg, t->ident, type_name, (int)sizeof(type_name));
            fprintf(out, "        %s %s;\n", type_name, case_name);
            return;
        }
        if (find_enum(reg, t->ident)) {
            fprintf(out, "        uint8_t %s;\n", case_name);
            return;
        }
        if (find_flags(reg, t->ident)) {
            fprintf(out, "        uint32_t %s;\n", case_name);
            return;
        }
    }
    if (t->kind == TYPE_LIST) {
        if (is_list_u8(reg, t)) {
            fprintf(out, "        struct { const uint8_t *data; uint32_t len; } %s;\n", case_name);
        } else {
            fprintf(out, "        struct { const uint8_t *data; uint32_t len; uint32_t byte_len; } %s;\n",
                    case_name);
        }
        return;
    }
    if (t->kind == TYPE_BORROW) {
        if (t->param_count < 1 || t->params[0] < 0) {
            codegen_die_type("borrow<T> variant payload missing inner type", resolved);
        }
        emit_variant_payload(out, reg, t->params[0], case_name);
        return;
    }
    if (t->kind == TYPE_OPTION || t->kind == TYPE_TUPLE || t->kind == TYPE_RESULT) {
        /* Complex nested type — wrap in a struct and use emit_c_fields */
        fprintf(out, "        struct {\n");
        emit_c_fields(out, reg, type_idx, "v", "            ");
        fprintf(out, "        } %s;\n", case_name);
        return;
    }
    codegen_die_type("unsupported variant payload type", resolved);
}

/* ------------------------------------------------------------------ */
/* Header emission                                                    */
/* ------------------------------------------------------------------ */

static void emit_header(FILE *out, const WitRegistry *reg,
                        const DbiEntry *dbis, int ndbi,
                        const char *wit_path,
                        const char *header_path)
{
    char upper[MAX_NAME];
    char macro_name[MAX_NAME * 2];
    char type_name[MAX_NAME * 2];
    char fn_name[MAX_NAME * 2];
    char dbi_schema_symbol[MAX_NAME * 2];
    char dbi_schema_count_symbol[MAX_NAME * 2];
    char wit_path_display[MAX_PATH_TEXT];

    /* Derive include guard from header filename (basename, uppercased). */
    char guard[MAX_NAME];
    const char *base = header_path;
    const char *p;
    for (p = header_path; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    size_t gi = 0;
    for (p = base; *p && gi < MAX_NAME - 1; p++) {
        if ((*p >= 'a' && *p <= 'z')) guard[gi++] = (char)(*p - 'a' + 'A');
        else if ((*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')) guard[gi++] = *p;
        else guard[gi++] = '_';
    }
    guard[gi] = '\0';

    path_to_project_relative(wit_path, wit_path_display, (int)sizeof(wit_path_display));

    fprintf(out, "/* Auto-generated by tools/wit_codegen; DO NOT EDIT.\n");
    fprintf(out, " * Source WIT: %s\n", wit_path_display);
    fprintf(out, " * WIT package: %s\n", reg->package_full[0] ? reg->package_full : "<none>");
    fprintf(out, " * Generated qualifier: camel=%s snake=%s upper=%s\n",
            reg->package_camel[0] ? reg->package_camel : "<none>",
            reg->package_snake[0] ? reg->package_snake : "<none>",
            reg->package_upper[0] ? reg->package_upper : "<none>");
    fprintf(out, " */\n");
    fprintf(out, "#ifndef %s\n", guard);
    fprintf(out, "#define %s\n\n", guard);
    fprintf(out, "#include <stdint.h>\n");
    fprintf(out, "#include <stddef.h>\n");
    fprintf(out, "#include \"croft/wit_wire.h\"\n");
    fprintf(out, "#include \"sapling/thatch.h\"\n");
    fprintf(out, "#include \"sapling/err.h\"\n\n");

    fprintf(out, "/* Wire format version shared across all generated WIT bindings. */\n");
    fprintf(out, "#define SAP_WIT_WIRE_VERSION 1u\n\n");

    fprintf(out, "/*\n");
    fprintf(out, " * Thatch WIT wire format (version 1):\n");
    fprintf(out, " *\n");
    fprintf(out, " *   record:      [TAG_RECORD  0x10][skip_len: 4 LE][...fields...]\n");
    fprintf(out, " *   variant:     [TAG_VARIANT 0x11][skip_len: 4 LE][case_tag: 1][...payload...]\n");
    fprintf(out, " *   enum:        [TAG_ENUM    0x12][value: 1]\n");
    fprintf(out, " *   flags:       [TAG_FLAGS   0x13][bits: 4 LE]\n");
    fprintf(out, " *   option none: [TAG_OPTION_NONE 0x14]\n");
    fprintf(out, " *   option some: [TAG_OPTION_SOME 0x15][...inner...]\n");
    fprintf(out, " *   tuple:       [TAG_TUPLE   0x16][skip_len: 4 LE][...elements...]\n");
    fprintf(out, " *   list<u8>:    [TAG_BYTES   0x2C][len: 4 LE][data: len bytes]\n");
    fprintf(out, " *   list<T!=u8>: [TAG_LIST    0x17][skip_len: 4 LE][count: 4 LE][encoded elements]\n");
    fprintf(out, " *   result ok:   [TAG_RESULT_OK  0x18][...ok value...]\n");
    fprintf(out, " *   result err:  [TAG_RESULT_ERR 0x19][...err value...]\n");
    fprintf(out, " *   sN/uN:       [TAG_SN/UN  0x20-0x27][data: N/8 bytes LE]\n");
    fprintf(out, " *   fN:          [TAG_FN     0x28-0x29][data: N/8 bytes LE]\n");
    fprintf(out, " *   bool:        [TAG_BOOL_FALSE 0x2A] or [TAG_BOOL_TRUE 0x2B]\n");
    fprintf(out, " *   bytes:       [TAG_BYTES  0x2C][len: 4 LE][data: len bytes]\n");
    fprintf(out, " *   string:      [TAG_STRING 0x2D][len: 4 LE][data: len bytes]\n");
    fprintf(out, " *   resource:    [TAG_RESOURCE 0x2E][handle: 4 LE]\n");
    fprintf(out, " *\n");
    fprintf(out, " *   skip_len: byte count of everything after the skip_len field\n");
    fprintf(out, " *             itself, up to (but not including) the next sibling.\n");
    fprintf(out, " */\n\n");

    for (int i = 0; i < ndbi; i++) {
        char dbi_upper[MAX_NAME];
        wit_name_to_upper_ident(dbis[i].name, dbi_upper, (int)sizeof(dbi_upper));
        fprintf(out, "/* WIT DBI slot %d (%s). */\n", dbis[i].dbi, dbis[i].name);
        fprintf(out, "#define SAP_WIT_%s_DBI_%s %du\n", reg->package_upper, dbi_upper, dbis[i].dbi);
    }
    if (ndbi > 0) fprintf(out, "\n");

    if (reg->resource_count > 0) {
        fprintf(out, "/* Resource handle typedefs traced back to WIT `resource` items. */\n");
        for (int i = 0; i < reg->resource_count; i++) {
            wit_macro_name(reg, reg->resources[i].name, macro_name, (int)sizeof(macro_name));
            wit_resource_c_typename(reg, reg->resources[i].name, type_name, (int)sizeof(type_name));
            fprintf(out, "/* WIT resource %s. */\n", reg->resources[i].name);
            fprintf(out, "typedef uint32_t %s;\n", type_name);
            fprintf(out, "#define %s_RESOURCE_INVALID ((%s)0u)\n",
                    macro_name, type_name);
        }
        fprintf(out, "\n");
    }
    if (ndbi > 0) {
        fprintf(out, "/* Shared DBI schema metadata shape used by runtime-schema packages. */\n");
        fprintf(out, "typedef struct {\n");
        fprintf(out, "    uint32_t dbi;\n");
        fprintf(out, "    const char *name;\n");
        fprintf(out, "    const char *key_wit_record;\n");
        fprintf(out, "    const char *value_wit_record;\n");
        fprintf(out, "} SapWitDbiSchema;\n\n");
    }
    for (int i = 0; i < reg->enum_count; i++) {
        const WitEnum *en = &reg->enums[i];
        wit_macro_name(reg, en->name, macro_name, (int)sizeof(macro_name));
        fprintf(out, "/* WIT enum %s. */\n", en->name);
        for (int j = 0; j < en->case_count; j++) {
            char case_upper[MAX_NAME];
            wit_name_to_upper_ident(en->cases[j], case_upper, (int)sizeof(case_upper));
            emit_trace_comment(out, "", en->cases[j]);
            fprintf(out, "#define %s_%s %d\n", macro_name, case_upper, j);
        }
        fprintf(out, "\n");
    }
    for (int i = 0; i < reg->flags_count; i++) {
        const WitFlags *fl = &reg->flags[i];
        wit_macro_name(reg, fl->name, macro_name, (int)sizeof(macro_name));
        fprintf(out, "/* WIT flags %s. */\n", fl->name);
        for (int j = 0; j < fl->bit_count; j++) {
            char bit_upper[MAX_NAME];
            wit_name_to_upper_ident(fl->bits[j], bit_upper, (int)sizeof(bit_upper));
            emit_trace_comment(out, "", fl->bits[j]);
            fprintf(out, "#define %s_%s (1u << %d)\n", macro_name, bit_upper, j);
        }
        fprintf(out, "\n");
    }
    for (int i = 0; i < reg->variant_count; i++) {
        const WitVariant *var = &reg->variants[i];
        wit_macro_name(reg, var->name, macro_name, (int)sizeof(macro_name));
        fprintf(out, "/* WIT variant %s case tags. */\n", var->name);
        for (int j = 0; j < var->case_count; j++) {
            char case_upper[MAX_NAME];
            char case_trace[256];
            wit_name_to_upper_ident(var->cases[j].name, case_upper, (int)sizeof(case_upper));
            format_variant_case_trace(&var->cases[j], case_trace, (int)sizeof(case_trace));
            emit_trace_comment(out, "", case_trace);
            fprintf(out, "#define %s_%s %d\n", macro_name, case_upper, j);
        }
        fprintf(out, "\n");
    }
    const char *order[MAX_TYPES * 2];
    int norder = topo_sort_types(reg, order, MAX_TYPES * 2);
    if (norder < 0) return;

    for (int idx = 0; idx < norder; idx++) {
        const char *tname = order[idx];
        const WitRecord *rec = find_record(reg, tname);
        if (rec) {
            wit_type_c_typename(reg, tname, type_name, (int)sizeof(type_name));
            fprintf(out, "/* WIT record %s -> %s. */\n", tname, type_name);
            fprintf(out, "typedef struct {\n");
            for (int j = 0; j < rec->field_count; j++) {
                char field_name[MAX_NAME];
                char field_trace[256];
                wit_name_to_snake_ident(rec->fields[j].name, field_name, (int)sizeof(field_name));
                format_field_trace(&rec->fields[j], field_trace, (int)sizeof(field_trace));
                emit_trace_comment(out, "    ", field_trace);
                emit_c_fields(out, reg, rec->fields[j].wit_type, field_name, "    ");
            }
            fprintf(out, "} %s;\n\n", type_name);
            continue;
        }
        const WitVariant *var = find_variant(reg, tname);
        if (var) {
            wit_type_c_typename(reg, tname, type_name, (int)sizeof(type_name));
            fprintf(out, "/* WIT variant %s -> %s. */\n", tname, type_name);
            fprintf(out, "typedef struct {\n");
            fprintf(out, "    uint8_t case_tag;\n");
            int has_payload = 0;
            for (int j = 0; j < var->case_count; j++)
                if (var->cases[j].payload_type >= 0) { has_payload = 1; break; }
            if (has_payload) {
                fprintf(out, "    union {\n");
                for (int j = 0; j < var->case_count; j++) {
                    if (var->cases[j].payload_type < 0) continue;
                    char case_name[MAX_NAME];
                    char case_trace[256];
                    wit_name_to_snake_ident(var->cases[j].name, case_name, (int)sizeof(case_name));
                    format_variant_case_trace(&var->cases[j], case_trace, (int)sizeof(case_trace));
                    emit_trace_comment(out, "        ", case_trace);
                    emit_variant_payload(out, reg, var->cases[j].payload_type, case_name);
                }
                fprintf(out, "    } val;\n");
            }
            fprintf(out, "} %s;\n\n", type_name);
        }
    }
    fprintf(out, "/* Writer functions keyed by WIT package-qualified names. */\n");
    for (int idx = 0; idx < norder; idx++) {
        wit_type_c_typename(reg, order[idx], type_name, (int)sizeof(type_name));
        wit_writer_name(reg, order[idx], fn_name, (int)sizeof(fn_name));
        fprintf(out, "/* WIT %s writer. */\n", order[idx]);
        fprintf(out, "int %s(ThatchRegion *region, const %s *val);\n", fn_name, type_name);
    }
    fprintf(out, "\n");

    fprintf(out, "/* Reader functions keyed by WIT package-qualified names. */\n");
    for (int idx = 0; idx < norder; idx++) {
        wit_type_c_typename(reg, order[idx], type_name, (int)sizeof(type_name));
        wit_reader_name(reg, order[idx], fn_name, (int)sizeof(fn_name));
        fprintf(out, "/* WIT %s reader. */\n", order[idx]);
        fprintf(out, "int %s(const ThatchRegion *region, ThatchCursor *cursor, %s *out);\n",
                fn_name, type_name);
    }
    fprintf(out, "\n");

    fprintf(out, "/* Generated reply lifecycle helpers. */\n");
    for (int idx = 0; idx < norder; idx++) {
        const WitVariant *var = find_variant(reg, order[idx]);
        char zero_name[MAX_NAME * 2];
        char dispose_name[MAX_NAME * 2];

        if (!var || !wit_is_reply_variant(var->name)) continue;
        wit_type_c_typename(reg, var->name, type_name, (int)sizeof(type_name));
        wit_zero_name(reg, var->name, zero_name, (int)sizeof(zero_name));
        wit_dispose_name(reg, var->name, dispose_name, (int)sizeof(dispose_name));
        fprintf(out, "void %s(%s *out);\n", zero_name, type_name);
        fprintf(out, "void %s(%s *out);\n", dispose_name, type_name);
    }
    fprintf(out, "\n");

    fprintf(out, "/* Generated command dispatch adapters. */\n");
    for (int idx = 0; idx < norder; idx++) {
        const WitVariant *command_var = find_variant(reg, order[idx]);
        const WitVariant *reply_var;
        char command_type[MAX_NAME * 2];
        char reply_type[MAX_NAME * 2];
        char ops_name[MAX_NAME * 2];
        char dispatch_name[MAX_NAME * 2];

        if (!command_var || !wit_is_command_variant(command_var->name)) continue;
        reply_var = find_paired_reply_variant(reg, command_var);
        if (!reply_var) continue;

        wit_type_c_typename(reg, command_var->name, command_type, (int)sizeof(command_type));
        wit_type_c_typename(reg, reply_var->name, reply_type, (int)sizeof(reply_type));
        wit_dispatch_ops_typename(reg, command_var->name, ops_name, (int)sizeof(ops_name));
        wit_dispatch_name(reg, command_var->name, dispatch_name, (int)sizeof(dispatch_name));

        fprintf(out, "/* WIT %s dispatcher ops. */\n", command_var->name);
        fprintf(out, "typedef struct {\n");
        for (int j = 0; j < command_var->case_count; j++) {
            char case_name[MAX_NAME];

            wit_name_to_snake_ident(command_var->cases[j].name, case_name, (int)sizeof(case_name));
            if (command_var->cases[j].payload_type >= 0) {
                char payload_type_name[MAX_NAME * 2];

                wit_payload_c_typename(reg,
                                       command_var->cases[j].payload_type,
                                       payload_type_name,
                                       (int)sizeof(payload_type_name));
                fprintf(out,
                        "    int32_t (*%s)(void *ctx, const %s *payload, %s *reply_out);\n",
                        case_name,
                        payload_type_name,
                        reply_type);
            } else {
                fprintf(out,
                        "    int32_t (*%s)(void *ctx, %s *reply_out);\n",
                        case_name,
                        reply_type);
            }
        }
        fprintf(out, "} %s;\n", ops_name);
        fprintf(out,
                "int32_t %s(void *ctx, const %s *ops, const %s *command, %s *reply_out);\n",
                dispatch_name,
                ops_name,
                command_type,
                reply_type);
    }
    fprintf(out, "\n");

    fprintf(out, "/* Shared universal skip routine for generated readers. */\n");
    fprintf(out, "int sap_wit_skip_value(const ThatchRegion *region, ThatchCursor *cursor);\n\n");

    fprintf(out, "/* DBI blob validators. */\n");
    for (int i = 0; i < ndbi; i++) {
        wit_validator_name(reg, dbis[i].val_rec, fn_name, (int)sizeof(fn_name));
        fprintf(out, "/* WIT validator for %s. */\n", dbis[i].val_rec);
        fprintf(out, "int %s(const void *data, uint32_t len);\n", fn_name);
    }
    fprintf(out, "\n");

    if (ndbi > 0) {
        wit_dbi_schema_symbol(reg, dbi_schema_symbol, (int)sizeof(dbi_schema_symbol));
        wit_dbi_schema_count_symbol(reg, dbi_schema_count_symbol, (int)sizeof(dbi_schema_count_symbol));
        fprintf(out, "extern const SapWitDbiSchema %s[];\n", dbi_schema_symbol);
        fprintf(out, "extern const uint32_t %s;\n\n", dbi_schema_count_symbol);
    }
    fprintf(out, "#endif /* %s */\n", guard);
}

static void emit_manifest(FILE *out, const WitRegistry *reg,
                          const DbiEntry *dbis, int ndbi,
                          const char *wit_path)
{
    char c_name[MAX_NAME * 2];
    char macro_name[MAX_NAME * 2];
    char fn_name[MAX_NAME * 2];
    char normalized[MAX_NAME];
    char wit_path_display[MAX_PATH_TEXT];

    if (!out || !reg) {
        return;
    }

    path_to_project_relative(wit_path, wit_path_display, (int)sizeof(wit_path_display));

    fprintf(out, "# Auto-generated by tools/wit_codegen; DO NOT EDIT.\n");
    fprintf(out, "# Source WIT: %s\n", wit_path_display);
    fprintf(out, "# WIT package: %s\n", reg->package_full[0] ? reg->package_full : "<none>");
    fprintf(out, "# Generated qualifier: camel=%s snake=%s upper=%s\n",
            reg->package_camel[0] ? reg->package_camel : "<none>",
            reg->package_snake[0] ? reg->package_snake : "<none>",
            reg->package_upper[0] ? reg->package_upper : "<none>");
    fprintf(out, "# Columns: kind\twit-name\tnormalized\tc-name\tmacro-or-function\n");

    for (int i = 0; i < reg->resource_count; i++) {
        wit_trim_leading_package_tail(reg, reg->resources[i].name, normalized, (int)sizeof(normalized));
        wit_resource_c_typename(reg, reg->resources[i].name, c_name, (int)sizeof(c_name));
        wit_macro_name(reg, reg->resources[i].name, macro_name, (int)sizeof(macro_name));
        fprintf(out, "resource\t%s\t%s\t%s\t%s_RESOURCE_INVALID\n",
                reg->resources[i].name,
                normalized[0] ? normalized : "<exact-package>",
                c_name,
                macro_name);
    }

    for (int i = 0; i < reg->record_count; i++) {
        wit_trim_leading_package_tail(reg, reg->records[i].name, normalized, (int)sizeof(normalized));
        wit_type_c_typename(reg, reg->records[i].name, c_name, (int)sizeof(c_name));
        wit_writer_name(reg, reg->records[i].name, fn_name, (int)sizeof(fn_name));
        fprintf(out, "record\t%s\t%s\t%s\t%s\n",
                reg->records[i].name,
                normalized[0] ? normalized : "<exact-package>",
                c_name,
                fn_name);
    }

    for (int i = 0; i < reg->variant_count; i++) {
        wit_trim_leading_package_tail(reg, reg->variants[i].name, normalized, (int)sizeof(normalized));
        wit_type_c_typename(reg, reg->variants[i].name, c_name, (int)sizeof(c_name));
        wit_macro_name(reg, reg->variants[i].name, macro_name, (int)sizeof(macro_name));
        fprintf(out, "variant\t%s\t%s\t%s\t%s\n",
                reg->variants[i].name,
                normalized[0] ? normalized : "<exact-package>",
                c_name,
                macro_name);
    }

    for (int i = 0; i < reg->enum_count; i++) {
        wit_trim_leading_package_tail(reg, reg->enums[i].name, normalized, (int)sizeof(normalized));
        wit_type_c_typename(reg, reg->enums[i].name, c_name, (int)sizeof(c_name));
        wit_macro_name(reg, reg->enums[i].name, macro_name, (int)sizeof(macro_name));
        fprintf(out, "enum\t%s\t%s\t%s\t%s\n",
                reg->enums[i].name,
                normalized[0] ? normalized : "<exact-package>",
                c_name,
                macro_name);
    }

    for (int i = 0; i < reg->flags_count; i++) {
        wit_trim_leading_package_tail(reg, reg->flags[i].name, normalized, (int)sizeof(normalized));
        wit_type_c_typename(reg, reg->flags[i].name, c_name, (int)sizeof(c_name));
        wit_macro_name(reg, reg->flags[i].name, macro_name, (int)sizeof(macro_name));
        fprintf(out, "flags\t%s\t%s\t%s\t%s\n",
                reg->flags[i].name,
                normalized[0] ? normalized : "<exact-package>",
                c_name,
                macro_name);
    }

    for (int i = 0; i < ndbi; i++) {
        wit_trim_leading_package_tail(reg, dbis[i].val_rec, normalized, (int)sizeof(normalized));
        wit_validator_name(reg, dbis[i].val_rec, fn_name, (int)sizeof(fn_name));
        fprintf(out, "validator\t%s\t%s\t<none>\t%s\n",
                dbis[i].val_rec,
                normalized[0] ? normalized : "<exact-package>",
                fn_name);
    }
}

/* ------------------------------------------------------------------ */
/* Writer emission helpers                                            */
/* ------------------------------------------------------------------ */

/*
 * emit_write_type_expr — recursively emit thatch_write_* calls for a
 * type expression.  `access` is the C expression to read the value from
 * (e.g., "val->payload" or "val->kind").
 *
 * For blob types (string/bytes/list) the access is the base name
 * and we append sep+"data" / sep+"len".  For record fields sep="_"
 * (flat fields like val->X_data); for variant payloads sep="."
 * (struct members like val->val.X.data).
 */
static void emit_write_type_expr(FILE *out, const WitRegistry *reg,
                                 int type_idx, const char *access,
                                 const char *sep, const char *indent)
{
    if (type_idx < 0) codegen_die("internal: negative type index in emit_write_type_expr");
    int resolved = unwrap_borrow_type(reg, type_idx);
    if (resolved < 0) codegen_die("internal: unresolved type index %d in emit_write_type_expr", type_idx);
    WitTypeExpr *t = &g_type_pool[resolved];

    switch (t->kind) {
    case TYPE_IDENT: {
        /* string / bytes -> TAG_STRING + len + data */
        if (strcmp(t->ident, "string") == 0) {
            fprintf(out, "%sSAP_WIT_CHECK(thatch_write_tag(region, SAP_WIT_TAG_STRING));\n", indent);
            fprintf(out, "%sSAP_WIT_CHECK(thatch_write_data(region, &%s%slen, 4));\n", indent, access, sep);
            fprintf(out, "%sSAP_WIT_CHECK(thatch_write_data(region, %s%sdata, %s%slen));\n", indent, access, sep, access, sep);
            return;
        }
        /* bool -> TAG_BOOL_TRUE or TAG_BOOL_FALSE (no payload) */
        if (strcmp(t->ident, "bool") == 0) {
            fprintf(out, "%sSAP_WIT_CHECK(thatch_write_tag(region, %s ? SAP_WIT_TAG_BOOL_TRUE : SAP_WIT_TAG_BOOL_FALSE));\n", indent, access);
            return;
        }
        /* numeric primitives: tag + raw data */
        const char *ctype = prim_c_type(t->ident);
        if (ctype) {
            const char *tag = NULL;
            int size = 0;
            if      (strcmp(t->ident, "s8")  == 0) { tag = "SAP_WIT_TAG_S8";  size = 1; }
            else if (strcmp(t->ident, "u8")  == 0) { tag = "SAP_WIT_TAG_U8";  size = 1; }
            else if (strcmp(t->ident, "s16") == 0) { tag = "SAP_WIT_TAG_S16"; size = 2; }
            else if (strcmp(t->ident, "u16") == 0) { tag = "SAP_WIT_TAG_U16"; size = 2; }
            else if (strcmp(t->ident, "s32") == 0) { tag = "SAP_WIT_TAG_S32"; size = 4; }
            else if (strcmp(t->ident, "u32") == 0) { tag = "SAP_WIT_TAG_U32"; size = 4; }
            else if (strcmp(t->ident, "s64") == 0) { tag = "SAP_WIT_TAG_S64"; size = 8; }
            else if (strcmp(t->ident, "u64") == 0) { tag = "SAP_WIT_TAG_U64"; size = 8; }
            else if (strcmp(t->ident, "f32") == 0) { tag = "SAP_WIT_TAG_F32"; size = 4; }
            else if (strcmp(t->ident, "f64") == 0) { tag = "SAP_WIT_TAG_F64"; size = 8; }
            if (tag) {
                fprintf(out, "%sSAP_WIT_CHECK(thatch_write_tag(region, %s));\n", indent, tag);
                fprintf(out, "%sSAP_WIT_CHECK(thatch_write_data(region, &%s, %d));\n", indent, access, size);
            }
            return;
        }
        if (find_resource(reg, t->ident)) {
            fprintf(out, "%sSAP_WIT_CHECK(thatch_write_tag(region, SAP_WIT_TAG_RESOURCE));\n", indent);
            fprintf(out, "%sSAP_WIT_CHECK(thatch_write_data(region, &%s, 4));\n", indent, access);
            return;
        }
        /* enum -> TAG_ENUM + 1 byte */
        if (find_enum(reg, t->ident)) {
            fprintf(out, "%sSAP_WIT_CHECK(thatch_write_tag(region, SAP_WIT_TAG_ENUM));\n", indent);
            fprintf(out, "%sSAP_WIT_CHECK(thatch_write_data(region, &%s, 1));\n", indent, access);
            return;
        }
        /* flags -> TAG_FLAGS + 4 bytes */
        if (find_flags(reg, t->ident)) {
            fprintf(out, "%sSAP_WIT_CHECK(thatch_write_tag(region, SAP_WIT_TAG_FLAGS));\n", indent);
            fprintf(out, "%sSAP_WIT_CHECK(thatch_write_data(region, &%s, 4));\n", indent, access);
            return;
        }
        /* named record or variant — delegate to its writer */
        if (find_record(reg, t->ident) || find_variant(reg, t->ident)) {
            char fn_name[MAX_NAME * 2];
            wit_writer_name(reg, t->ident, fn_name, (int)sizeof(fn_name));
            fprintf(out, "%sSAP_WIT_CHECK(%s(region, &%s));\n", indent, fn_name, access);
            return;
        }
        codegen_die("unsupported identifier in writer emission: %s", t->ident);
        return;
    }
    case TYPE_LIST:
        if (is_list_u8(reg, t)) {
            /* list<u8> stays compact and wire-compatible with bytes. */
            fprintf(out, "%sSAP_WIT_CHECK(thatch_write_tag(region, SAP_WIT_TAG_BYTES));\n", indent);
            fprintf(out, "%sSAP_WIT_CHECK(thatch_write_data(region, &%s%slen, 4));\n", indent, access, sep);
            fprintf(out, "%sSAP_WIT_CHECK(thatch_write_data(region, %s%sdata, %s%slen));\n", indent, access, sep, access, sep);
            return;
        }
        /*
         * Generic list<T> payloads are provided as encoded element bytes plus a count.
         * We structurally validate count-vs-bytes using sap_wit_skip_value before writing.
         */
        fprintf(out, "%s{\n", indent);
        fprintf(out, "%s    ThatchRegion _list_view;\n", indent);
        fprintf(out, "%s    ThatchCursor _list_cur = 0;\n", indent);
        fprintf(out, "%s    if (%s%sbyte_len > 0 && %s%sdata == NULL) return ERR_INVALID;\n", indent,
                access, sep, access, sep);
        fprintf(out, "%s    SAP_WIT_CHECK(thatch_region_init_readonly(&_list_view, %s%sdata, %s%sbyte_len));\n",
                indent, access, sep, access, sep);
        fprintf(out, "%s    for (uint32_t _i = 0; _i < %s%slen; _i++) {\n", indent, access, sep);
        fprintf(out, "%s        SAP_WIT_CHECK(sap_wit_skip_value(&_list_view, &_list_cur));\n", indent);
        fprintf(out, "%s    }\n", indent);
        fprintf(out, "%s    if (_list_cur != %s%sbyte_len) return ERR_TYPE;\n", indent, access, sep);
        fprintf(out, "%s    SAP_WIT_CHECK(thatch_write_tag(region, SAP_WIT_TAG_LIST));\n", indent);
        fprintf(out, "%s    ThatchCursor _list_skip_loc;\n", indent);
        fprintf(out, "%s    SAP_WIT_CHECK(thatch_reserve_skip(region, &_list_skip_loc));\n", indent);
        fprintf(out, "%s    SAP_WIT_CHECK(thatch_write_data(region, &%s%slen, 4));\n", indent, access, sep);
        fprintf(out, "%s    SAP_WIT_CHECK(thatch_write_data(region, %s%sdata, %s%sbyte_len));\n", indent,
                access, sep, access, sep);
        fprintf(out, "%s    SAP_WIT_CHECK(thatch_commit_skip(region, _list_skip_loc));\n", indent);
        fprintf(out, "%s}\n", indent);
        return;
    case TYPE_OPTION: {
        if (t->param_count < 1 || t->params[0] < 0) {
            codegen_die_type("option<T> writer missing inner type", resolved);
        }
        /* When called from emit_write_record, options are handled inline there.
         * This path handles option<T> in nested positions (e.g. inside tuples). */
        char *has_access = NULL;
        char *value_access = NULL;
        build_option_access_paths(access, sep, &has_access, &value_access);
        char inner_indent[64];
        snprintf(inner_indent, sizeof(inner_indent), "%s    ", indent);
        fprintf(out, "%sif (%s) {\n", indent, has_access);
        fprintf(out, "%sSAP_WIT_CHECK(thatch_write_tag(region, SAP_WIT_TAG_OPTION_SOME));\n", inner_indent);
        emit_write_type_expr(out, reg, t->params[0], value_access, sep, inner_indent);
        fprintf(out, "%s} else {\n", indent);
        fprintf(out, "%sSAP_WIT_CHECK(thatch_write_tag(region, SAP_WIT_TAG_OPTION_NONE));\n", inner_indent);
        fprintf(out, "%s}\n", indent);
        free(has_access);
        free(value_access);
        return;
    }
    case TYPE_BORROW:
        if (t->param_count < 1 || t->params[0] < 0) {
            codegen_die_type("borrow<T> writer missing inner type", resolved);
        }
        emit_write_type_expr(out, reg, t->params[0], access, sep, indent);
        return;
    case TYPE_TUPLE: {
        char inner[64];
        snprintf(inner, sizeof(inner), "%s    ", indent);
        fprintf(out, "%s{\n", indent);
        fprintf(out, "%sSAP_WIT_CHECK(thatch_write_tag(region, SAP_WIT_TAG_TUPLE));\n", inner);
        fprintf(out, "%suint32_t _tuple_skip_loc;\n", inner);
        fprintf(out, "%sSAP_WIT_CHECK(thatch_reserve_skip(region, &_tuple_skip_loc));\n", inner);
        for (int i = 0; i < t->param_count; i++) {
            char sub[256];
            snprintf(sub, sizeof(sub), "%s_%d", access, i);
            emit_write_type_expr(out, reg, t->params[i], sub, "_", inner);
        }
        fprintf(out, "%sSAP_WIT_CHECK(thatch_commit_skip(region, _tuple_skip_loc));\n", inner);
        fprintf(out, "%s}\n", indent);
        return;
    }
    case TYPE_RESULT: {
        char *is_ok_access = NULL;
        char *ok_access = NULL;
        char *err_access = NULL;
        build_result_access_paths(access, sep, &is_ok_access, &ok_access, &err_access);

        char inner[64];
        snprintf(inner, sizeof(inner), "%s    ", indent);
        fprintf(out, "%sif (%s) {\n", indent, is_ok_access);
        fprintf(out, "%sSAP_WIT_CHECK(thatch_write_tag(region, SAP_WIT_TAG_RESULT_OK));\n", inner);
        if (t->param_count > 0 && t->params[0] >= 0)
            emit_write_type_expr(out, reg, t->params[0], ok_access, "_", inner);
        fprintf(out, "%s} else {\n", indent);
        fprintf(out, "%sSAP_WIT_CHECK(thatch_write_tag(region, SAP_WIT_TAG_RESULT_ERR));\n", inner);
        if (t->param_count > 1 && t->params[1] >= 0)
            emit_write_type_expr(out, reg, t->params[1], err_access, "_", inner);
        fprintf(out, "%s}\n", indent);
        free(is_ok_access);
        free(ok_access);
        free(err_access);
        return;
    }
    }
    codegen_die("internal: unhandled WitTypeKind in emit_write_type_expr");
}

/* Emit a complete writer function for a record. */
static void emit_write_record(FILE *out, const WitRegistry *reg,
                               const WitRecord *rec)
{
    char fn_name[MAX_NAME * 2];
    char type_name[MAX_NAME * 2];

    wit_writer_name(reg, rec->name, fn_name, (int)sizeof(fn_name));
    wit_type_c_typename(reg, rec->name, type_name, (int)sizeof(type_name));

    fprintf(out, "/* WIT record writer for %s. */\n", rec->name);
    fprintf(out, "int %s(ThatchRegion *region, const %s *val)\n{\n", fn_name, type_name);

    /* All records get skip pointers so sap_wit_skip_value works uniformly. */
    fprintf(out, "    SAP_WIT_CHECK(thatch_write_tag(region, SAP_WIT_TAG_RECORD));\n");
    fprintf(out, "    ThatchCursor skip_loc;\n");
    fprintf(out, "    SAP_WIT_CHECK(thatch_reserve_skip(region, &skip_loc));\n");

    for (int i = 0; i < rec->field_count; i++) {
        char fname[MAX_NAME], access[256];
        wit_name_to_snake_ident(rec->fields[i].name, fname, (int)sizeof(fname));

        /* For option fields, the guard is has_X and we pass that as the condition */
        int res = unwrap_borrow_type(reg, rec->fields[i].wit_type);
        WitTypeExpr *ft = (res >= 0) ? &g_type_pool[res] : NULL;

        if (ft && ft->kind == TYPE_OPTION) {
            snprintf(access, sizeof(access), "val->has_%s", fname);
            fprintf(out, "    if (%s) {\n", access);
            fprintf(out, "        SAP_WIT_CHECK(thatch_write_tag(region, SAP_WIT_TAG_OPTION_SOME));\n");
            /* Write the inner value */
            char inner_access[256];
            snprintf(inner_access, sizeof(inner_access), "val->%s", fname);
            emit_write_type_expr(out, reg, ft->params[0], inner_access, "_", "        ");
            fprintf(out, "    } else {\n");
            fprintf(out, "        SAP_WIT_CHECK(thatch_write_tag(region, SAP_WIT_TAG_OPTION_NONE));\n");
            fprintf(out, "    }\n");
        } else {
            snprintf(access, sizeof(access), "val->%s", fname);
            emit_write_type_expr(out, reg, rec->fields[i].wit_type, access, "_", "    ");
        }
    }

    fprintf(out, "    SAP_WIT_CHECK(thatch_commit_skip(region, skip_loc));\n");
    fprintf(out, "    return ERR_OK;\n}\n\n");
}

/* Emit a complete writer function for a variant. */
static void emit_write_variant(FILE *out, const WitRegistry *reg,
                                const WitVariant *var)
{
    char fn_name[MAX_NAME * 2];
    char type_name[MAX_NAME * 2];
    char macro_name[MAX_NAME * 2];

    wit_writer_name(reg, var->name, fn_name, (int)sizeof(fn_name));
    wit_type_c_typename(reg, var->name, type_name, (int)sizeof(type_name));
    wit_macro_name(reg, var->name, macro_name, (int)sizeof(macro_name));

    fprintf(out, "/* WIT variant writer for %s. */\n", var->name);
    fprintf(out, "int %s(ThatchRegion *region, const %s *val)\n{\n", fn_name, type_name);
    fprintf(out, "    SAP_WIT_CHECK(thatch_write_tag(region, SAP_WIT_TAG_VARIANT));\n");
    fprintf(out, "    ThatchCursor skip_loc;\n");
    fprintf(out, "    SAP_WIT_CHECK(thatch_reserve_skip(region, &skip_loc));\n");
    fprintf(out, "    SAP_WIT_CHECK(thatch_write_data(region, &val->case_tag, 1));\n");
    fprintf(out, "    switch (val->case_tag) {\n");

    for (int j = 0; j < var->case_count; j++) {
        char cu[MAX_NAME], cs[MAX_NAME];
        int resolved;
        WitTypeExpr *payload_type;
        wit_name_to_upper_ident(var->cases[j].name, cu, (int)sizeof(cu));
        wit_name_to_snake_ident(var->cases[j].name, cs, (int)sizeof(cs));
        fprintf(out, "    case %s_%s:\n", macro_name, cu);
        if (var->cases[j].payload_type >= 0) {
            char access[256];
            resolved = unwrap_borrow_type(reg, var->cases[j].payload_type);
            payload_type = (resolved >= 0) ? &g_type_pool[resolved] : NULL;
            if (payload_type &&
                (payload_type->kind == TYPE_OPTION
                 || payload_type->kind == TYPE_TUPLE
                 || payload_type->kind == TYPE_RESULT)) {
                snprintf(access, sizeof(access), "val->val.%s.v", cs);
                emit_write_type_expr(out, reg, var->cases[j].payload_type, access, "_", "        ");
            } else {
                snprintf(access, sizeof(access), "val->val.%s", cs);
                emit_write_type_expr(out, reg, var->cases[j].payload_type, access, ".", "        ");
            }
        }
        fprintf(out, "        break;\n");
    }

    fprintf(out, "    default: return ERR_TYPE;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    SAP_WIT_CHECK(thatch_commit_skip(region, skip_loc));\n");
    fprintf(out, "    return ERR_OK;\n}\n\n");
}

/* ------------------------------------------------------------------ */
/* Reader emission helpers                                            */
/* ------------------------------------------------------------------ */

/*
 * emit_read_type_expr — recursively emit thatch_read_* calls for a
 * type expression.  `access` is the C lvalue to write the value to
 * (e.g., "out->payload").  `sep` works like in writers: "_" for record
 * fields (out->X_data), "." for variant payloads (out->val.X.data).
 */
static void emit_read_type_expr(FILE *out, const WitRegistry *reg,
                                int type_idx, const char *access,
                                const char *sep, const char *indent)
{
    if (type_idx < 0) codegen_die("internal: negative type index in emit_read_type_expr");
    int resolved = unwrap_borrow_type(reg, type_idx);
    if (resolved < 0) codegen_die("internal: unresolved type index %d in emit_read_type_expr", type_idx);
    WitTypeExpr *t = &g_type_pool[resolved];

    switch (t->kind) {
    case TYPE_IDENT: {
        /* string / bytes -> read tag, read len, read_ptr for data */
        if (strcmp(t->ident, "string") == 0) {
            fprintf(out, "%s{ uint8_t tag; SAP_WIT_CHECK(thatch_read_tag(region, cursor, &tag));\n", indent);
            fprintf(out, "%s  if (tag != SAP_WIT_TAG_STRING && tag != SAP_WIT_TAG_BYTES) return ERR_TYPE; }\n", indent);
            fprintf(out, "%sSAP_WIT_CHECK(thatch_read_data(region, cursor, 4, &%s%slen));\n", indent, access, sep);
            fprintf(out, "%s{ const void *p; SAP_WIT_CHECK(thatch_read_ptr(region, cursor, %s%slen, &p));\n", indent, access, sep);
            fprintf(out, "%s  %s%sdata = (const uint8_t *)p; }\n", indent, access, sep);
            return;
        }
        /* bool -> read tag, check TRUE/FALSE */
        if (strcmp(t->ident, "bool") == 0) {
            fprintf(out, "%s{ uint8_t tag; SAP_WIT_CHECK(thatch_read_tag(region, cursor, &tag));\n", indent);
            fprintf(out, "%s  if (tag == SAP_WIT_TAG_BOOL_TRUE) %s = 1;\n", indent, access);
            fprintf(out, "%s  else if (tag == SAP_WIT_TAG_BOOL_FALSE) %s = 0;\n", indent, access);
            fprintf(out, "%s  else return ERR_TYPE; }\n", indent);
            return;
        }
        /* numeric primitives */
        const char *ctype = prim_c_type(t->ident);
        if (ctype) {
            const char *tag = NULL;
            int size = 0;
            if      (strcmp(t->ident, "s8")  == 0) { tag = "SAP_WIT_TAG_S8";  size = 1; }
            else if (strcmp(t->ident, "u8")  == 0) { tag = "SAP_WIT_TAG_U8";  size = 1; }
            else if (strcmp(t->ident, "s16") == 0) { tag = "SAP_WIT_TAG_S16"; size = 2; }
            else if (strcmp(t->ident, "u16") == 0) { tag = "SAP_WIT_TAG_U16"; size = 2; }
            else if (strcmp(t->ident, "s32") == 0) { tag = "SAP_WIT_TAG_S32"; size = 4; }
            else if (strcmp(t->ident, "u32") == 0) { tag = "SAP_WIT_TAG_U32"; size = 4; }
            else if (strcmp(t->ident, "s64") == 0) { tag = "SAP_WIT_TAG_S64"; size = 8; }
            else if (strcmp(t->ident, "u64") == 0) { tag = "SAP_WIT_TAG_U64"; size = 8; }
            else if (strcmp(t->ident, "f32") == 0) { tag = "SAP_WIT_TAG_F32"; size = 4; }
            else if (strcmp(t->ident, "f64") == 0) { tag = "SAP_WIT_TAG_F64"; size = 8; }
            if (tag) {
                fprintf(out, "%s{ uint8_t tag; SAP_WIT_CHECK(thatch_read_tag(region, cursor, &tag));\n", indent);
                fprintf(out, "%s  if (tag != %s) return ERR_TYPE; }\n", indent, tag);
                fprintf(out, "%sSAP_WIT_CHECK(thatch_read_data(region, cursor, %d, &%s));\n", indent, size, access);
            }
            return;
        }
        if (find_resource(reg, t->ident)) {
            fprintf(out, "%s{ uint8_t tag; SAP_WIT_CHECK(thatch_read_tag(region, cursor, &tag));\n", indent);
            fprintf(out, "%s  if (tag != SAP_WIT_TAG_RESOURCE) return ERR_TYPE; }\n", indent);
            fprintf(out, "%sSAP_WIT_CHECK(thatch_read_data(region, cursor, 4, &%s));\n", indent, access);
            return;
        }
        /* enum -> read tag + 1 byte */
        if (find_enum(reg, t->ident)) {
            fprintf(out, "%s{ uint8_t tag; SAP_WIT_CHECK(thatch_read_tag(region, cursor, &tag));\n", indent);
            fprintf(out, "%s  if (tag != SAP_WIT_TAG_ENUM) return ERR_TYPE; }\n", indent);
            fprintf(out, "%sSAP_WIT_CHECK(thatch_read_data(region, cursor, 1, &%s));\n", indent, access);
            return;
        }
        /* flags -> read tag + 4 bytes */
        if (find_flags(reg, t->ident)) {
            fprintf(out, "%s{ uint8_t tag; SAP_WIT_CHECK(thatch_read_tag(region, cursor, &tag));\n", indent);
            fprintf(out, "%s  if (tag != SAP_WIT_TAG_FLAGS) return ERR_TYPE; }\n", indent);
            fprintf(out, "%sSAP_WIT_CHECK(thatch_read_data(region, cursor, 4, &%s));\n", indent, access);
            return;
        }
        /* named record or variant — delegate to its reader */
        if (find_record(reg, t->ident) || find_variant(reg, t->ident)) {
            char fn_name[MAX_NAME * 2];
            wit_reader_name(reg, t->ident, fn_name, (int)sizeof(fn_name));
            fprintf(out, "%sSAP_WIT_CHECK(%s(region, cursor, &%s));\n", indent, fn_name, access);
            return;
        }
        codegen_die("unsupported identifier in reader emission: %s", t->ident);
        return;
    }
    case TYPE_LIST:
        if (is_list_u8(reg, t)) {
            /* list<u8> same as bytes */
            fprintf(out, "%s{ uint8_t tag; SAP_WIT_CHECK(thatch_read_tag(region, cursor, &tag));\n", indent);
            fprintf(out, "%s  if (tag != SAP_WIT_TAG_BYTES && tag != SAP_WIT_TAG_STRING) return ERR_TYPE; }\n", indent);
            fprintf(out, "%sSAP_WIT_CHECK(thatch_read_data(region, cursor, 4, &%s%slen));\n", indent, access, sep);
            fprintf(out, "%s{ const void *p; SAP_WIT_CHECK(thatch_read_ptr(region, cursor, %s%slen, &p));\n", indent, access, sep);
            fprintf(out, "%s  %s%sdata = (const uint8_t *)p; }\n", indent, access, sep);
            return;
        }
        fprintf(out, "%s{\n", indent);
        fprintf(out, "%s    uint8_t _list_tag;\n", indent);
        fprintf(out, "%s    uint32_t _list_skip_len;\n", indent);
        fprintf(out, "%s    uint32_t _list_remaining;\n", indent);
        fprintf(out, "%s    ThatchCursor _list_segment_end;\n", indent);
        fprintf(out, "%s    ThatchRegion _list_view;\n", indent);
        fprintf(out, "%s    ThatchCursor _list_cur = 0;\n", indent);
        fprintf(out, "%s    SAP_WIT_CHECK(thatch_read_tag(region, cursor, &_list_tag));\n", indent);
        fprintf(out, "%s    if (_list_tag != SAP_WIT_TAG_LIST) return ERR_TYPE;\n", indent);
        fprintf(out, "%s    SAP_WIT_CHECK(thatch_read_skip_len(region, cursor, &_list_skip_len));\n", indent);
        fprintf(out, "%s    _list_remaining = thatch_region_used(region) - *cursor;\n", indent);
        fprintf(out, "%s    if (_list_skip_len > _list_remaining) return ERR_RANGE;\n", indent);
        fprintf(out, "%s    _list_segment_end = *cursor + _list_skip_len;\n", indent);
        fprintf(out, "%s    SAP_WIT_CHECK(thatch_read_data(region, cursor, 4, &%s%slen));\n", indent,
                access, sep);
        fprintf(out, "%s    %s%sbyte_len = _list_segment_end - *cursor;\n", indent, access, sep);
        fprintf(out, "%s    { const void *p;\n", indent);
        fprintf(out, "%s      SAP_WIT_CHECK(thatch_read_ptr(region, cursor, %s%sbyte_len, &p));\n",
                indent, access, sep);
        fprintf(out, "%s      %s%sdata = (const uint8_t *)p; }\n", indent, access, sep);
        fprintf(out, "%s    SAP_WIT_CHECK(thatch_region_init_readonly(&_list_view, %s%sdata, %s%sbyte_len));\n",
                indent, access, sep, access, sep);
        fprintf(out, "%s    for (uint32_t _i = 0; _i < %s%slen; _i++) {\n", indent, access, sep);
        fprintf(out, "%s        SAP_WIT_CHECK(sap_wit_skip_value(&_list_view, &_list_cur));\n", indent);
        fprintf(out, "%s    }\n", indent);
        fprintf(out, "%s    if (_list_cur != %s%sbyte_len) return ERR_TYPE;\n", indent, access, sep);
        fprintf(out, "%s    if (*cursor != _list_segment_end) return ERR_TYPE;\n", indent);
        fprintf(out, "%s}\n", indent);
        return;
    case TYPE_OPTION: {
        if (t->param_count < 1 || t->params[0] < 0) {
            codegen_die_type("option<T> reader missing inner type", resolved);
        }
        char *has_access = NULL;
        char *value_access = NULL;
        build_option_access_paths(access, sep, &has_access, &value_access);
        char inner_indent[64];
        snprintf(inner_indent, sizeof(inner_indent), "%s    ", indent);
        fprintf(out, "%s{ uint8_t tag; SAP_WIT_CHECK(thatch_read_tag(region, cursor, &tag));\n", indent);
        fprintf(out, "%s  if (tag == SAP_WIT_TAG_OPTION_SOME) {\n", indent);
        fprintf(out, "%s    %s = 1;\n", indent, has_access);
        emit_read_type_expr(out, reg, t->params[0], value_access, sep, inner_indent);
        fprintf(out, "%s  } else if (tag == SAP_WIT_TAG_OPTION_NONE) {\n", indent);
        fprintf(out, "%s    %s = 0;\n", indent, has_access);
        fprintf(out, "%s  } else return ERR_TYPE; }\n", indent);
        free(has_access);
        free(value_access);
        return;
    }
    case TYPE_BORROW:
        if (t->param_count < 1 || t->params[0] < 0) {
            codegen_die_type("borrow<T> reader missing inner type", resolved);
        }
        emit_read_type_expr(out, reg, t->params[0], access, sep, indent);
        return;
    case TYPE_TUPLE: {
        char inner[64];
        snprintf(inner, sizeof(inner), "%s    ", indent);
        fprintf(out, "%s{\n", indent);
        fprintf(out, "%s{ uint8_t tag; SAP_WIT_CHECK(thatch_read_tag(region, cursor, &tag));\n", inner);
        fprintf(out, "%s  if (tag != SAP_WIT_TAG_TUPLE) return ERR_TYPE; }\n", inner);
        fprintf(out, "%suint32_t _skip_len;\n", inner);
        fprintf(out, "%sSAP_WIT_CHECK(thatch_read_skip_len(region, cursor, &_skip_len));\n", inner);
        fprintf(out, "%suint32_t _remaining = thatch_region_used(region) - *cursor;\n", inner);
        fprintf(out, "%sif (_skip_len > _remaining) return ERR_RANGE;\n", inner);
        fprintf(out, "%sThatchCursor _segment_end = *cursor + _skip_len;\n", inner);
        for (int i = 0; i < t->param_count; i++) {
            char sub[256];
            snprintf(sub, sizeof(sub), "%s_%d", access, i);
            emit_read_type_expr(out, reg, t->params[i], sub, "_", inner);
        }
        fprintf(out, "%sif (*cursor != _segment_end) return ERR_TYPE;\n", inner);
        fprintf(out, "%s}\n", indent);
        return;
    }
    case TYPE_RESULT: {
        char *is_ok_access = NULL;
        char *ok_access = NULL;
        char *err_access = NULL;
        build_result_access_paths(access, sep, &is_ok_access, &ok_access, &err_access);

        char inner[64];
        snprintf(inner, sizeof(inner), "%s    ", indent);
        fprintf(out, "%s{ uint8_t tag; SAP_WIT_CHECK(thatch_read_tag(region, cursor, &tag));\n", indent);
        fprintf(out, "%s  if (tag == SAP_WIT_TAG_RESULT_OK) {\n", indent);
        fprintf(out, "%s%s = 1;\n", inner, is_ok_access);
        if (t->param_count > 0 && t->params[0] >= 0)
            emit_read_type_expr(out, reg, t->params[0], ok_access, "_", inner);
        fprintf(out, "%s  } else if (tag == SAP_WIT_TAG_RESULT_ERR) {\n", indent);
        fprintf(out, "%s%s = 0;\n", inner, is_ok_access);
        if (t->param_count > 1 && t->params[1] >= 0)
            emit_read_type_expr(out, reg, t->params[1], err_access, "_", inner);
        fprintf(out, "%s  } else return ERR_TYPE; }\n", indent);
        free(is_ok_access);
        free(ok_access);
        free(err_access);
        return;
    }
    }
    codegen_die("internal: unhandled WitTypeKind in emit_read_type_expr");
}

/* Emit a complete reader function for a record. */
static void emit_read_record(FILE *out, const WitRegistry *reg,
                              const WitRecord *rec)
{
    char fn_name[MAX_NAME * 2];
    char type_name[MAX_NAME * 2];

    wit_reader_name(reg, rec->name, fn_name, (int)sizeof(fn_name));
    wit_type_c_typename(reg, rec->name, type_name, (int)sizeof(type_name));

    fprintf(out, "/* WIT record reader for %s. */\n", rec->name);
    fprintf(out, "int %s(const ThatchRegion *region, ThatchCursor *cursor, %s *out)\n{\n",
            fn_name, type_name);

    /* All records have skip pointers (uniform encoding).
     * Read skip_len and enforce segment-end: cursor must equal
     * segment_end after all fields are decoded. */
    fprintf(out, "    { uint8_t tag; SAP_WIT_CHECK(thatch_read_tag(region, cursor, &tag));\n");
    fprintf(out, "      if (tag != SAP_WIT_TAG_RECORD) return ERR_TYPE; }\n");
    fprintf(out, "    uint32_t _skip_len;\n");
    fprintf(out, "    SAP_WIT_CHECK(thatch_read_skip_len(region, cursor, &_skip_len));\n");
    fprintf(out, "    /* overflow-safe: reject if skip_len exceeds remaining bytes */\n");
    fprintf(out, "    uint32_t _remaining = thatch_region_used(region) - *cursor;\n");
    fprintf(out, "    if (_skip_len > _remaining) return ERR_RANGE;\n");
    fprintf(out, "    ThatchCursor _segment_end = *cursor + _skip_len;\n");

    for (int i = 0; i < rec->field_count; i++) {
        char fname[MAX_NAME], access[256];
        wit_name_to_snake_ident(rec->fields[i].name, fname, (int)sizeof(fname));

        int res = unwrap_borrow_type(reg, rec->fields[i].wit_type);
        WitTypeExpr *ft = (res >= 0) ? &g_type_pool[res] : NULL;

        if (ft && ft->kind == TYPE_OPTION) {
            /* Option fields: read tag, branch on SOME/NONE */
            fprintf(out, "    { uint8_t tag; SAP_WIT_CHECK(thatch_read_tag(region, cursor, &tag));\n");
            fprintf(out, "      if (tag == SAP_WIT_TAG_OPTION_SOME) {\n");
            fprintf(out, "        out->has_%s = 1;\n", fname);
            snprintf(access, sizeof(access), "out->%s", fname);
            emit_read_type_expr(out, reg, ft->params[0], access, "_", "        ");
            fprintf(out, "      } else if (tag == SAP_WIT_TAG_OPTION_NONE) {\n");
            fprintf(out, "        out->has_%s = 0;\n", fname);
            fprintf(out, "      } else return ERR_TYPE; }\n");
        } else {
            snprintf(access, sizeof(access), "out->%s", fname);
            emit_read_type_expr(out, reg, rec->fields[i].wit_type, access, "_", "    ");
        }
    }

    fprintf(out, "    if (*cursor != _segment_end) return ERR_TYPE;\n");
    fprintf(out, "    return ERR_OK;\n}\n\n");
}

/* Emit a complete reader function for a variant. */
static void emit_read_variant(FILE *out, const WitRegistry *reg,
                               const WitVariant *var)
{
    char fn_name[MAX_NAME * 2];
    char type_name[MAX_NAME * 2];
    char macro_name[MAX_NAME * 2];

    wit_reader_name(reg, var->name, fn_name, (int)sizeof(fn_name));
    wit_type_c_typename(reg, var->name, type_name, (int)sizeof(type_name));
    wit_macro_name(reg, var->name, macro_name, (int)sizeof(macro_name));

    fprintf(out, "/* WIT variant reader for %s. */\n", var->name);
    fprintf(out, "int %s(const ThatchRegion *region, ThatchCursor *cursor, %s *out)\n{\n",
            fn_name, type_name);
    fprintf(out, "    { uint8_t tag; SAP_WIT_CHECK(thatch_read_tag(region, cursor, &tag));\n");
    fprintf(out, "      if (tag != SAP_WIT_TAG_VARIANT) return ERR_TYPE; }\n");
    fprintf(out, "    uint32_t _skip_len;\n");
    fprintf(out, "    SAP_WIT_CHECK(thatch_read_skip_len(region, cursor, &_skip_len));\n");
    fprintf(out, "    uint32_t _remaining = thatch_region_used(region) - *cursor;\n");
    fprintf(out, "    if (_skip_len > _remaining) return ERR_RANGE;\n");
    fprintf(out, "    ThatchCursor _segment_end = *cursor + _skip_len;\n");
    fprintf(out, "    SAP_WIT_CHECK(thatch_read_data(region, cursor, 1, &out->case_tag));\n");
    fprintf(out, "    switch (out->case_tag) {\n");

    for (int j = 0; j < var->case_count; j++) {
        char cu[MAX_NAME], cs[MAX_NAME];
        int resolved;
        WitTypeExpr *payload_type;
        wit_name_to_upper_ident(var->cases[j].name, cu, (int)sizeof(cu));
        wit_name_to_snake_ident(var->cases[j].name, cs, (int)sizeof(cs));
        fprintf(out, "    case %s_%s:\n", macro_name, cu);
        if (var->cases[j].payload_type >= 0) {
            char access[256];
            resolved = unwrap_borrow_type(reg, var->cases[j].payload_type);
            payload_type = (resolved >= 0) ? &g_type_pool[resolved] : NULL;
            if (payload_type &&
                (payload_type->kind == TYPE_OPTION
                 || payload_type->kind == TYPE_TUPLE
                 || payload_type->kind == TYPE_RESULT)) {
                snprintf(access, sizeof(access), "out->val.%s.v", cs);
                emit_read_type_expr(out, reg, var->cases[j].payload_type, access, "_", "        ");
            } else {
                snprintf(access, sizeof(access), "out->val.%s", cs);
                emit_read_type_expr(out, reg, var->cases[j].payload_type, access, ".", "        ");
            }
        }
        fprintf(out, "        break;\n");
    }

    fprintf(out, "    default: return ERR_TYPE;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    if (*cursor != _segment_end) return ERR_TYPE;\n");
    fprintf(out, "    return ERR_OK;\n}\n\n");
}

/* ------------------------------------------------------------------ */
/* Universal skip emission                                            */
/* ------------------------------------------------------------------ */

__attribute__((unused))
static void emit_skip_function(FILE *out)
{
    fprintf(out, "int sap_wit_skip_value(const ThatchRegion *region, ThatchCursor *cursor)\n{\n");
    fprintf(out, "    uint8_t tag;\n");
    fprintf(out, "    SAP_WIT_CHECK(thatch_read_tag(region, cursor, &tag));\n");
    fprintf(out, "    switch (tag) {\n");

    /* Fixed-size primitives */
    fprintf(out, "    case SAP_WIT_TAG_S8:  case SAP_WIT_TAG_U8:\n");
    fprintf(out, "        return thatch_advance_cursor(region, cursor, 1);\n");
    fprintf(out, "    case SAP_WIT_TAG_S16: case SAP_WIT_TAG_U16:\n");
    fprintf(out, "        return thatch_advance_cursor(region, cursor, 2);\n");
    fprintf(out, "    case SAP_WIT_TAG_S32: case SAP_WIT_TAG_U32: case SAP_WIT_TAG_F32:\n");
    fprintf(out, "        return thatch_advance_cursor(region, cursor, 4);\n");
    fprintf(out, "    case SAP_WIT_TAG_S64: case SAP_WIT_TAG_U64: case SAP_WIT_TAG_F64:\n");
    fprintf(out, "        return thatch_advance_cursor(region, cursor, 8);\n");

    /* Bool: no payload */
    fprintf(out, "    case SAP_WIT_TAG_BOOL_TRUE: case SAP_WIT_TAG_BOOL_FALSE:\n");
    fprintf(out, "        return ERR_OK;\n");

    /* Enum: 1 byte payload */
    fprintf(out, "    case SAP_WIT_TAG_ENUM:\n");
    fprintf(out, "        return thatch_advance_cursor(region, cursor, 1);\n");

    /* Flags: 4 byte payload */
    fprintf(out, "    case SAP_WIT_TAG_FLAGS:\n");
    fprintf(out, "        return thatch_advance_cursor(region, cursor, 4);\n");

    /* String/bytes: len + data */
    fprintf(out, "    case SAP_WIT_TAG_STRING: case SAP_WIT_TAG_BYTES: {\n");
    fprintf(out, "        uint32_t len;\n");
    fprintf(out, "        SAP_WIT_CHECK(thatch_read_data(region, cursor, 4, &len));\n");
    fprintf(out, "        return thatch_advance_cursor(region, cursor, len);\n");
    fprintf(out, "    }\n");

    fprintf(out, "    case SAP_WIT_TAG_RESOURCE:\n");
    fprintf(out, "        return thatch_advance_cursor(region, cursor, 4);\n");

    /* Record/tuple/list/variant with skip pointer: read skip len, advance */
    fprintf(out, "    case SAP_WIT_TAG_RECORD: case SAP_WIT_TAG_TUPLE:\n");
    fprintf(out, "    case SAP_WIT_TAG_LIST: case SAP_WIT_TAG_VARIANT: {\n");
    fprintf(out, "        uint32_t skip;\n");
    fprintf(out, "        SAP_WIT_CHECK(thatch_read_skip_len(region, cursor, &skip));\n");
    fprintf(out, "        return thatch_advance_cursor(region, cursor, skip);\n");
    fprintf(out, "    }\n");

    /* Option SOME: recursively skip inner */
    fprintf(out, "    case SAP_WIT_TAG_OPTION_SOME:\n");
    fprintf(out, "        return sap_wit_skip_value(region, cursor);\n");

    /* Option NONE: done */
    fprintf(out, "    case SAP_WIT_TAG_OPTION_NONE:\n");
    fprintf(out, "        return ERR_OK;\n");

    /* Result OK/ERR: recursively skip payload */
    fprintf(out, "    case SAP_WIT_TAG_RESULT_OK: case SAP_WIT_TAG_RESULT_ERR:\n");
    fprintf(out, "        return sap_wit_skip_value(region, cursor);\n");

    fprintf(out, "    default: return ERR_TYPE;\n");
    fprintf(out, "    }\n");
    fprintf(out, "}\n\n");
}

static void emit_dispatch_helper_function(FILE *out,
                                          const WitRegistry *reg,
                                          const WitVariant *command_var,
                                          const WitVariant *reply_var)
{
    char command_type[MAX_NAME * 2];
    char reply_type[MAX_NAME * 2];
    char ops_name[MAX_NAME * 2];
    char dispatch_name[MAX_NAME * 2];
    char macro_name[MAX_NAME * 2];

    if (!out || !reg || !command_var || !reply_var || !wit_is_command_variant(command_var->name)) {
        return;
    }

    wit_type_c_typename(reg, command_var->name, command_type, (int)sizeof(command_type));
    wit_type_c_typename(reg, reply_var->name, reply_type, (int)sizeof(reply_type));
    wit_dispatch_ops_typename(reg, command_var->name, ops_name, (int)sizeof(ops_name));
    wit_dispatch_name(reg, command_var->name, dispatch_name, (int)sizeof(dispatch_name));
    wit_macro_name(reg, command_var->name, macro_name, (int)sizeof(macro_name));

    fprintf(out, "/* Generated dispatcher for %s. */\n", command_var->name);
    fprintf(out,
            "int32_t %s(void *ctx, const %s *ops, const %s *command, %s *reply_out)\n{\n",
            dispatch_name,
            ops_name,
            command_type,
            reply_type);
    fprintf(out, "    if (!ops || !command || !reply_out) {\n");
    fprintf(out, "        return -1;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    switch (command->case_tag) {\n");
    for (int i = 0; i < command_var->case_count; i++) {
        char case_upper[MAX_NAME];
        char case_snake[MAX_NAME];

        wit_name_to_upper_ident(command_var->cases[i].name, case_upper, (int)sizeof(case_upper));
        wit_name_to_snake_ident(command_var->cases[i].name, case_snake, (int)sizeof(case_snake));
        fprintf(out, "    case %s_%s:\n", macro_name, case_upper);
        fprintf(out, "        if (!ops->%s) {\n", case_snake);
        fprintf(out, "            return -1;\n");
        fprintf(out, "        }\n");
        if (command_var->cases[i].payload_type >= 0) {
            fprintf(out,
                    "        return ops->%s(ctx, &command->val.%s, reply_out);\n",
                    case_snake,
                    case_snake);
        } else {
            fprintf(out, "        return ops->%s(ctx, reply_out);\n", case_snake);
        }
    }
    fprintf(out, "    default:\n");
    fprintf(out, "        return -1;\n");
    fprintf(out, "    }\n");
    fprintf(out, "}\n\n");
}

static void emit_reply_helper_functions(FILE *out, const WitRegistry *reg,
                                        const WitVariant *var)
{
    char type_name[MAX_NAME * 2];
    char zero_name[MAX_NAME * 2];
    char dispose_name[MAX_NAME * 2];
    char macro_name[MAX_NAME * 2];

    if (!out || !reg || !var || !wit_is_reply_variant(var->name)) {
        return;
    }

    wit_type_c_typename(reg, var->name, type_name, (int)sizeof(type_name));
    wit_zero_name(reg, var->name, zero_name, (int)sizeof(zero_name));
    wit_dispose_name(reg, var->name, dispose_name, (int)sizeof(dispose_name));
    wit_macro_name(reg, var->name, macro_name, (int)sizeof(macro_name));

    fprintf(out, "/* Generated zero helper for %s. */\n", var->name);
    fprintf(out, "void %s(%s *out)\n{\n", zero_name, type_name);
    fprintf(out, "    if (!out) {\n");
    fprintf(out, "        return;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    memset(out, 0, sizeof(*out));\n");
    fprintf(out, "}\n\n");

    fprintf(out, "/* Generated dispose helper for %s. */\n", var->name);
    fprintf(out, "void %s(%s *out)\n{\n", dispose_name, type_name);
    fprintf(out, "    if (!out) {\n");
    fprintf(out, "        return;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    switch (out->case_tag) {\n");
    for (int i = 0; i < var->case_count; i++) {
        char case_upper[MAX_NAME];
        char case_snake[MAX_NAME];
        int ownership_kind;

        wit_name_to_upper_ident(var->cases[i].name, case_upper, (int)sizeof(case_upper));
        wit_name_to_snake_ident(var->cases[i].name, case_snake, (int)sizeof(case_snake));
        ownership_kind = reply_case_owns_heap_ok(reg, var->cases[i].payload_type);
        fprintf(out, "    case %s_%s:\n", macro_name, case_upper);
        if (ownership_kind == 1) {
            fprintf(out, "        if (out->val.%s.is_v_ok && out->val.%s.v_val.ok.v_data) {\n",
                    case_snake, case_snake);
            fprintf(out, "            free((void*)out->val.%s.v_val.ok.v_data);\n", case_snake);
            fprintf(out, "        }\n");
        } else if (ownership_kind == 2) {
            fprintf(out, "        if (out->val.%s.is_v_ok\n", case_snake);
            fprintf(out, "                && out->val.%s.v_val.ok.has_v\n", case_snake);
            fprintf(out, "                && out->val.%s.v_val.ok.v_data) {\n", case_snake);
            fprintf(out, "            free((void*)out->val.%s.v_val.ok.v_data);\n", case_snake);
            fprintf(out, "        }\n");
        }
        fprintf(out, "        break;\n");
    }
    fprintf(out, "    default:\n");
    fprintf(out, "        break;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    memset(out, 0, sizeof(*out));\n");
    fprintf(out, "}\n\n");
}

/* ------------------------------------------------------------------ */
/* Source emission                                                    */
/* ------------------------------------------------------------------ */

static void emit_source(FILE *out, const WitRegistry *reg,
                        const DbiEntry *dbis, int ndbi,
                        const char *wit_path,
                        const char *header_path)
{
    char dbi_schema_symbol[MAX_NAME * 2];
    char dbi_schema_count_symbol[MAX_NAME * 2];
    char fn_name[MAX_NAME * 2];
    char type_name[MAX_NAME * 2];
    char wit_path_display[MAX_PATH_TEXT];
    char header_include[MAX_PATH_TEXT];

    path_to_project_relative(wit_path, wit_path_display, (int)sizeof(wit_path_display));
    path_basename(header_path, header_include, (int)sizeof(header_include));

    fprintf(out, "/* Auto-generated by tools/wit_codegen; DO NOT EDIT.\n");
    fprintf(out, " * Source WIT: %s\n", wit_path_display);
    fprintf(out, " * WIT package: %s\n", reg->package_full[0] ? reg->package_full : "<none>");
    fprintf(out, " */\n");
    fprintf(out, "#include \"%s\"\n", header_include);
    fprintf(out, "#include <stdlib.h>\n");
    fprintf(out, "#include <string.h>\n\n");

    fprintf(out, "#define SAP_WIT_CHECK(rc) do { if ((rc) != ERR_OK) return (rc); } while (0)\n\n");

    if (ndbi > 0) {
        wit_dbi_schema_symbol(reg, dbi_schema_symbol, (int)sizeof(dbi_schema_symbol));
        wit_dbi_schema_count_symbol(reg, dbi_schema_count_symbol,
                                    (int)sizeof(dbi_schema_count_symbol));
        fprintf(out, "/* WIT DBI schema table for package %s. */\n", reg->package_full);
        fprintf(out, "const SapWitDbiSchema %s[] = {\n", dbi_schema_symbol);
        for (int i = 0; i < ndbi; i++) {
            char dbi_snake[MAX_NAME];
            wit_name_to_snake_ident(dbis[i].name, dbi_snake, (int)sizeof(dbi_snake));
            fprintf(out, "    {%du, \"%s\", \"%s\", \"%s\"},\n",
                    dbis[i].dbi, dbi_snake, dbis[i].key_rec, dbis[i].val_rec);
        }
        fprintf(out, "};\n\n");
        fprintf(out, "const uint32_t %s =\n", dbi_schema_count_symbol);
        fprintf(out, "    (uint32_t)(sizeof(%s) / sizeof(%s[0]));\n\n",
                dbi_schema_symbol, dbi_schema_symbol);
    }

    const char *order[MAX_TYPES * 2];
    int norder = topo_sort_types(reg, order, MAX_TYPES * 2);
    if (norder < 0) return;

    fprintf(out, "/* ---- Writer functions ---- */\n\n");
    for (int idx = 0; idx < norder; idx++) {
        const WitRecord *rec = find_record(reg, order[idx]);
        if (rec) { emit_write_record(out, reg, rec); continue; }
        const WitVariant *var = find_variant(reg, order[idx]);
        if (var) { emit_write_variant(out, reg, var); continue; }
    }

    fprintf(out, "/* ---- Reader functions ---- */\n\n");
    for (int idx = 0; idx < norder; idx++) {
        const WitRecord *rec = find_record(reg, order[idx]);
        if (rec) { emit_read_record(out, reg, rec); continue; }
        const WitVariant *var = find_variant(reg, order[idx]);
        if (var) { emit_read_variant(out, reg, var); continue; }
    }

    fprintf(out, "/* ---- Generated command dispatch helpers ---- */\n\n");
    for (int idx = 0; idx < norder; idx++) {
        const WitVariant *command_var = find_variant(reg, order[idx]);
        const WitVariant *reply_var;

        if (!command_var || !wit_is_command_variant(command_var->name)) continue;
        reply_var = find_paired_reply_variant(reg, command_var);
        if (!reply_var) continue;
        emit_dispatch_helper_function(out, reg, command_var, reply_var);
    }

    fprintf(out, "/* ---- Generated reply helpers ---- */\n\n");
    for (int idx = 0; idx < norder; idx++) {
        const WitVariant *var = find_variant(reg, order[idx]);
        if (var) { emit_reply_helper_functions(out, reg, var); }
    }

    fprintf(out, "/* ---- DBI blob validators ---- */\n\n");
    for (int i = 0; i < ndbi; i++) {
        wit_validator_name(reg, dbis[i].val_rec, fn_name, (int)sizeof(fn_name));
        wit_type_c_typename(reg, dbis[i].val_rec, type_name, (int)sizeof(type_name));
        fprintf(out, "/* WIT validator for %s. */\n", dbis[i].val_rec);
        fprintf(out, "int %s(const void *data, uint32_t len)\n{\n", fn_name);
        fprintf(out, "    if (!data && !len) return 0;\n");
        fprintf(out, "    if (!data || !len) return -1;\n");
        fprintf(out, "    ThatchRegion view;\n");
        fprintf(out, "    if (thatch_region_init_readonly(&view, data, len) != ERR_OK) return -1;\n");
        fprintf(out, "    ThatchCursor cur = 0;\n");
        fprintf(out, "    %s scratch;\n", type_name);
        fprintf(out, "    memset(&scratch, 0, sizeof(scratch));\n");
        wit_reader_name(reg, dbis[i].val_rec, type_name, (int)sizeof(type_name));
        fprintf(out, "    int rc = %s(&view, &cur, &scratch);\n", type_name);
        fprintf(out, "    if (rc != ERR_OK) return -1;\n");
        fprintf(out, "    if (cur != len) return -1;\n");
        fprintf(out, "    return 0;\n");
        fprintf(out, "}\n\n");
        wit_type_c_typename(reg, dbis[i].val_rec, type_name, (int)sizeof(type_name));
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    const char *wit_path = NULL;
    const char *header_path = NULL;
    const char *source_path = NULL;
    const char *manifest_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--wit") == 0 && i + 1 < argc)
            wit_path = argv[++i];
        else if (strcmp(argv[i], "--header") == 0 && i + 1 < argc)
            header_path = argv[++i];
        else if (strcmp(argv[i], "--source") == 0 && i + 1 < argc)
            source_path = argv[++i];
        else if (strcmp(argv[i], "--manifest") == 0 && i + 1 < argc)
            manifest_path = argv[++i];
        else if (argv[i][0] != '-' && !wit_path)
            wit_path = argv[i];
    }

    if (!wit_path) {
        fprintf(stderr,
            "usage: wit_codegen [--wit] <schema.wit> "
            "[--header <path>] [--source <path>] [--manifest <path>]\n");
        return 1;
    }

    FILE *f = fopen(wit_path, "rb");
    if (!f) {
        fprintf(stderr, "wit_codegen: cannot open %s\n", wit_path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = malloc(fsize + 1);
    if (!src) { fclose(f); return 1; }
    fread(src, 1, fsize, f);
    src[fsize] = '\0';
    fclose(f);

    Scanner scanner;
    scanner_init(&scanner, src, (int)fsize);

    WitRegistry reg;
    if (!parse_wit(&scanner, &reg)) {
        fprintf(stderr, "wit_codegen: parse failed at line %d col %d\n",
                scanner.line, scanner.col);
        free(src);
        return 1;
    }
    if (!lower_operations(&reg)) {
        free(src);
        return 1;
    }
    finalize_package_info(&reg, wit_path);

    DbiEntry dbis[MAX_TYPES];
    int ndbi = extract_dbis(&reg, dbis, MAX_TYPES);
    if (ndbi < 0) {
        fprintf(stderr, "wit_codegen: DBI extraction failed\n");
        free(src);
        return 1;
    }
    if (header_path) {
        FILE *hdr = fopen(header_path, "w");
        if (!hdr) {
            fprintf(stderr, "wit_codegen: cannot create %s\n", header_path);
            free(src); return 1;
        }
        emit_header(hdr, &reg, dbis, ndbi, wit_path, header_path);
        fclose(hdr);
    }

    if (source_path && header_path) {
        FILE *csrc = fopen(source_path, "w");
        if (!csrc) {
            fprintf(stderr, "wit_codegen: cannot create %s\n", source_path);
            free(src); return 1;
        }
        emit_source(csrc, &reg, dbis, ndbi, wit_path, header_path);
        fclose(csrc);
    }

    if (manifest_path) {
        FILE *manifest = fopen(manifest_path, "w");
        if (!manifest) {
            fprintf(stderr, "wit_codegen: cannot create %s\n", manifest_path);
            free(src);
            return 1;
        }
        emit_manifest(manifest, &reg, dbis, ndbi, wit_path);
        fclose(manifest);
    }

    printf("wit_codegen: PASS (records=%d variants=%d enums=%d flags=%d "
           "aliases=%d resources=%d dbis=%d)\n",
           reg.record_count, reg.variant_count, reg.enum_count,
           reg.flags_count, reg.alias_count, reg.resource_count, ndbi);

    free(src);
    return 0;
}
