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
#include <dirent.h>

#define MAX_FIELDS        64
#define MAX_CASES         128
#define MAX_TYPES         256
#define MAX_FUNCS         256
#define MAX_WORLDS        128
#define MAX_WORLD_ITEMS   512
#define MAX_NAME          128
#define MAX_PACKAGE       128
#define MAX_TRACE         512
#define MAX_METADATA      512
#define MAX_TYPE_PARAMS   16
#define MAX_TYPE_NODES    2048
#define MAX_PATH_TEXT     1024
#define MAX_SYMBOL        384
#define MAX_USE_BINDINGS  512
#define MAX_LOADED_FILES  256

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
    int         params[MAX_TYPE_PARAMS]; /* indices into type pool (-1 = omitted "_") */
    int         param_count;
} WitTypeExpr;

static WitTypeExpr g_type_pool[MAX_TYPE_NODES];
static int         g_type_pool_count = 0;

typedef struct WitRegistry WitRegistry;
static int wit_symbol_has_scope(const char *name);
static void wit_trim(char *buf);
static void wit_symbol_display_name(const WitRegistry *reg,
                                    const char *scope_package_full,
                                    const char *scope_interface_name,
                                    const char *symbol_name,
                                    char *out,
                                    int n);

static int type_alloc(void)
{
    if (g_type_pool_count >= MAX_TYPE_NODES) {
        fprintf(stderr, "wit_codegen: type pool exhausted\n");
        return -1;
    }
    int idx = g_type_pool_count++;
    memset(&g_type_pool[idx], 0, sizeof(WitTypeExpr));
    for (int i = 0; i < MAX_TYPE_PARAMS; i++) {
        g_type_pool[idx].params[i] = -1;
    }
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

static int type_to_display_str(const WitRegistry *reg,
                               const char *scope_package_full,
                               const char *scope_interface_name,
                               int idx,
                               char *buf,
                               int bufsize)
{
    if (bufsize <= 0) return 0;
    if (idx < 0) {
        int n = snprintf(buf, bufsize, "_");
        return (n < bufsize) ? n : bufsize - 1;
    }

    WitTypeExpr *t = &g_type_pool[idx];
    if (t->kind == TYPE_IDENT) {
        if (wit_symbol_has_scope(t->ident)) {
            wit_symbol_display_name(reg,
                                    scope_package_full,
                                    scope_interface_name,
                                    t->ident,
                                    buf,
                                    bufsize);
            return (int)strlen(buf);
        }

        {
            int n = snprintf(buf, bufsize, "%s", t->ident);
            return (n < bufsize) ? n : bufsize - 1;
        }
    }

    {
        const char *name = NULL;
        int n = 0;

        switch (t->kind) {
        case TYPE_IDENT:  name = "?";      break;
        case TYPE_OPTION: name = "option"; break;
        case TYPE_LIST:   name = "list";   break;
        case TYPE_TUPLE:  name = "tuple";  break;
        case TYPE_RESULT: name = "result"; break;
        case TYPE_BORROW: name = "borrow"; break;
        }

        n = snprintf(buf, bufsize, "%s<", name);
        for (int i = 0; i < t->param_count; i++) {
            if (i > 0) n += snprintf(buf + n, bufsize - n, ", ");
            n += type_to_display_str(reg,
                                     scope_package_full,
                                     scope_interface_name,
                                     t->params[i],
                                     buf + n,
                                     bufsize - n);
        }
        n += snprintf(buf + n, bufsize - n, ">");
        return (n < bufsize) ? n : bufsize - 1;
    }
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
    char     package_full[MAX_PACKAGE];
    char     metadata[MAX_METADATA];
    char     interface_name[MAX_NAME];
    char     symbol_name[MAX_SYMBOL];
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
    char           package_full[MAX_PACKAGE];
    char           metadata[MAX_METADATA];
    char           interface_name[MAX_NAME];
    char           symbol_name[MAX_SYMBOL];
    WitVariantCase cases[MAX_CASES];
    int            case_count;
} WitVariant;

typedef struct {
    char name[MAX_NAME];
    char package_full[MAX_PACKAGE];
    char metadata[MAX_METADATA];
    char interface_name[MAX_NAME];
    char symbol_name[MAX_SYMBOL];
    char cases[MAX_CASES][MAX_NAME];
    int  case_count;
} WitEnum;

typedef struct {
    char name[MAX_NAME];
    char package_full[MAX_PACKAGE];
    char metadata[MAX_METADATA];
    char interface_name[MAX_NAME];
    char symbol_name[MAX_SYMBOL];
    char bits[MAX_CASES][MAX_NAME];
    int  bit_count;
} WitFlags;

typedef struct {
    char name[MAX_NAME];
    char package_full[MAX_PACKAGE];
    char metadata[MAX_METADATA];
    char interface_name[MAX_NAME];
    char symbol_name[MAX_SYMBOL];
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
    char        package_full[MAX_PACKAGE];
    char        owner_name[MAX_NAME];
    char        owner_symbol_name[MAX_SYMBOL];
    char        interface_name[MAX_NAME];
    char        metadata[MAX_METADATA];
    WitFuncKind kind;
    WitOwnerKind owner_kind;
    WitField    params[MAX_FIELDS];
    int         param_count;
    int         result_type; /* index into g_type_pool, or -1 */
    char        trace[MAX_TRACE];
} WitFunc;

typedef struct {
    char name[MAX_NAME];
    char package_full[MAX_PACKAGE];
    char metadata[MAX_METADATA];
    char interface_name[MAX_NAME];
    char symbol_name[MAX_SYMBOL];
    int  imported;
} WitResource;

typedef struct {
    char name[MAX_NAME];
    char package_full[MAX_PACKAGE];
    char metadata[MAX_METADATA];
    char origin_world_name[MAX_NAME];
    char origin_item_name[MAX_NAME];
    int  imported;
} WitInterface;

typedef struct {
    char name[MAX_NAME];
    char package_full[MAX_PACKAGE];
    char metadata[MAX_METADATA];
    int  imported;
} WitWorld;

typedef enum {
    WIT_WORLD_ITEM_INCLUDE,
    WIT_WORLD_ITEM_IMPORT,
    WIT_WORLD_ITEM_EXPORT,
} WitWorldItemKind;

typedef enum {
    WIT_WORLD_TARGET_UNKNOWN,
    WIT_WORLD_TARGET_INTERFACE,
    WIT_WORLD_TARGET_WORLD,
    WIT_WORLD_TARGET_FUNCTION,
} WitWorldTargetKind;

typedef struct {
    WitWorldItemKind   kind;
    WitWorldTargetKind target_kind;
    char               package_full[MAX_PACKAGE];
    char               world_name[MAX_NAME];
    char               name[MAX_NAME];
    char               target_package_full[MAX_PACKAGE];
    char               target_name[MAX_NAME];
    char               lowered_target_package_full[MAX_PACKAGE];
    char               lowered_target_name[MAX_NAME];
    char               metadata[MAX_METADATA];
    int                imported;
} WitWorldItem;

typedef struct {
    char package_full[MAX_PACKAGE];
    char interface_name[MAX_NAME];
    char local_name[MAX_NAME];
    char target_package_full[MAX_PACKAGE];
    char target_interface_name[MAX_NAME];
    char target_name[MAX_NAME];
    char target_symbol_name[MAX_SYMBOL];
} WitUseBinding;

struct WitRegistry {
    char      package_full[MAX_PACKAGE];
    char      package_namespace[MAX_NAME];
    char      package_name[MAX_NAME];
    char      package_version[MAX_NAME];
    char      package_tail_raw[MAX_NAME];
    char      package_snake[MAX_NAME];
    char      package_upper[MAX_NAME];
    char      package_camel[MAX_NAME];
    WitInterface interfaces[MAX_TYPES];
    int          interface_count;
    WitWorld     worlds[MAX_WORLDS];
    int          world_count;
    WitWorldItem world_items[MAX_WORLD_ITEMS];
    int          world_item_count;
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
    WitUseBinding use_bindings[MAX_USE_BINDINGS];
    int           use_binding_count;
    WitFunc     funcs[MAX_FUNCS];
    int         func_count;
    char        source_path[MAX_PATH_TEXT];
    char        loaded_paths[MAX_LOADED_FILES][MAX_PATH_TEXT];
    int         loaded_path_count;
};

/* ------------------------------------------------------------------ */
/* Scanner (lexical only — no bracket balancing)                      */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *src;
    int         pos;
    int         len;
    int         line;
    int         col;
} Scanner;

static void scanner_init(Scanner *s, const char *src, int len)
{
    s->src  = src;
    s->pos  = 0;
    s->len  = len;
    s->line = 1;
    s->col  = 1;
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
        (void)attr_name;
        skip_balanced_parens(s);
    }
}

static void append_metadata_fragment(char *out, int n, const char *fragment)
{
    size_t out_len;

    if (!out || n <= 0 || !fragment || fragment[0] == '\0') {
        return;
    }

    out_len = strlen(out);
    if (out_len > 0u && out_len < (size_t)n - 1u) {
        out[out_len++] = ' ';
        out[out_len] = '\0';
    }
    if (out_len < (size_t)n - 1u) {
        snprintf(out + out_len, (size_t)n - out_len, "%s", fragment);
    }
}

static int consume_attribute_text(Scanner *s, char *out, int n)
{
    int start;
    int end;
    int len;

    if (!s || !out || n <= 0) return 0;
    out[0] = '\0';

    if (scanner_peek(s) != '@') {
        return 0;
    }

    start = s->pos;
    scanner_advance(s);
    while (!scanner_eof(s)) {
        char ch = scanner_peek(s);
        if (isalnum((unsigned char)ch) || ch == '-' || ch == '_' || ch == '.') {
            scanner_advance(s);
            continue;
        }
        break;
    }
    while (!scanner_eof(s)) {
        char ch = scanner_peek(s);
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            scanner_advance(s);
            continue;
        }
        break;
    }
    if (scanner_peek(s) == '(') {
        skip_balanced_parens(s);
    }

    end = s->pos;
    len = end - start;
    if (len < 0) len = 0;
    if (len >= n) len = n - 1;
    if (len > 0) {
        memcpy(out, s->src + start, (size_t)len);
    }
    out[len] = '\0';
    wit_trim(out);
    return out[0] != '\0';
}

static void consume_leading_metadata(Scanner *s, char *out, int n)
{
    char attr[MAX_METADATA];

    if (!s || !out || n <= 0) return;
    out[0] = '\0';

    while (!scanner_eof(s)) {
        int progressed = 0;

        while (!scanner_eof(s)) {
            char ch = scanner_peek(s);
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
                scanner_advance(s);
                progressed = 1;
            } else if (ch == '/' && s->pos + 1 < s->len && s->src[s->pos + 1] == '/') {
                while (!scanner_eof(s) && scanner_peek(s) != '\n')
                    scanner_advance(s);
                progressed = 1;
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
                progressed = 1;
            } else {
                break;
            }
        }

        if (scanner_peek(s) == '@') {
            if (consume_attribute_text(s, attr, (int)sizeof(attr))) {
                append_metadata_fragment(out, n, attr);
            }
            progressed = 1;
            continue;
        }

        if (!progressed) {
            break;
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

static int match_trailing_semicolon(Scanner *s)
{
    Scanner probe;

    if (!s) return 0;
    probe = *s;
    while (!scanner_eof(&probe)) {
        char ch = scanner_peek(&probe);
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            scanner_advance(&probe);
            continue;
        }
        break;
    }
    if (scanner_peek(&probe) != ';') {
        return 0;
    }
    *s = probe;
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
    int escaped = 0;

    if (!scanner_eof(s) && scanner_peek(s) == '%') {
        escaped = 1;
        scanner_advance(s);
    }
    while (!scanner_eof(s) && i < bufsize - 1) {
        char ch = scanner_peek(s);
        if (isalnum((unsigned char)ch) || ch == '-' || ch == '_') {
            buf[i++] = scanner_advance(s);
        } else {
            break;
        }
    }
    buf[i] = '\0';
    if (escaped && i == 0) {
        return 0;
    }
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

static int path_has_extension(const char *path, const char *ext)
{
    const char *dot;

    if (!path || !ext) return 0;
    dot = strrchr(path, '.');
    if (!dot) return 0;
    return strcmp(dot, ext) == 0;
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

static char *read_text_file(const char *path, long *size_out)
{
    FILE *f;
    long fsize;
    char *src;
    size_t nread;

    if (size_out) {
        *size_out = 0;
    }
    if (!path || path[0] == '\0') {
        return NULL;
    }

    f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    fsize = ftell(f);
    if (fsize < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    src = (char *)malloc((size_t)fsize + 1u);
    if (!src) {
        fclose(f);
        return NULL;
    }

    nread = fread(src, 1, (size_t)fsize, f);
    fclose(f);
    if (nread != (size_t)fsize) {
        free(src);
        return NULL;
    }

    src[fsize] = '\0';
    if (size_out) {
        *size_out = fsize;
    }
    return src;
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

static int wit_symbol_has_scope(const char *name)
{
    return name && (strstr(name, "::") != NULL || strchr(name, '/') != NULL);
}

static void wit_package_name_from_full(const char *package_full, char *out, int n)
{
    if (!out || n <= 0) return;
    if (!package_full || package_full[0] == '\0') {
        out[0] = '\0';
        return;
    }

    {
        const char *cursor = package_full;
        const char *colon = strchr(cursor, ':');
        const char *at = strrchr(cursor, '@');

        if (colon) {
            cursor = colon + 1;
        }
        if (at && at > cursor) {
            size_t len = (size_t)(at - cursor);
            if (len >= (size_t)n) len = (size_t)n - 1u;
            memcpy(out, cursor, len);
            out[len] = '\0';
        } else {
            snprintf(out, n, "%s", cursor);
        }
    }
}

static void wit_build_scoped_symbol_name(const char *package_full,
                                         const char *interface_name,
                                         const char *local_name,
                                         char *out,
                                         int n)
{
    if (!out || n <= 0) return;
    if (!local_name || local_name[0] == '\0') {
        out[0] = '\0';
        return;
    }
    if (package_full && package_full[0] != '\0' && interface_name && interface_name[0] != '\0') {
        snprintf(out, n, "%s/%s::%s", package_full, interface_name, local_name);
    } else if (interface_name && interface_name[0] != '\0') {
        snprintf(out, n, "%s::%s", interface_name, local_name);
    } else {
        snprintf(out, n, "%s", local_name);
    }
}

static void wit_split_symbol_name(const char *name,
                                  char *package_full,
                                  int package_n,
                                  char *interface_name,
                                  int interface_n,
                                  char *local_name,
                                  int local_n)
{
    const char *sep;
    const char *slash = NULL;
    size_t prefix_len;

    if (package_full && package_n > 0) package_full[0] = '\0';
    if (interface_name && interface_n > 0) interface_name[0] = '\0';
    if (local_name && local_n > 0) local_name[0] = '\0';
    if (!name) return;

    sep = strstr(name, "::");
    if (!sep) {
        if (local_name && local_n > 0) {
            snprintf(local_name, local_n, "%s", name);
        }
        return;
    }

    prefix_len = (size_t)(sep - name);
    for (const char *p = name; p < sep; p++) {
        if (*p == '/') slash = p;
    }

    if (slash && package_full && package_n > 0) {
        size_t len = (size_t)(slash - name);
        if (len >= (size_t)package_n) len = (size_t)package_n - 1u;
        memcpy(package_full, name, len);
        package_full[len] = '\0';
    }
    if (interface_name && interface_n > 0) {
        const char *iface = slash ? (slash + 1) : name;
        size_t len = prefix_len - (size_t)(iface - name);
        if (len >= (size_t)interface_n) len = (size_t)interface_n - 1u;
        memcpy(interface_name, iface, len);
        interface_name[len] = '\0';
    }
    if (local_name && local_n > 0) {
        snprintf(local_name, local_n, "%s", sep + 2);
    }
}

static const char *wit_symbol_local_name(const char *name)
{
    static char buf[MAX_NAME];
    wit_split_symbol_name(name, NULL, 0, NULL, 0, buf, (int)sizeof(buf));
    return buf;
}

static int wit_local_name_occurrences(const WitRegistry *reg, const char *local_name);

static int wit_interface_local_occurrences(const WitRegistry *reg,
                                           const char *interface_name,
                                           const char *local_name)
{
    int count = 0;

    if (!reg || !interface_name || interface_name[0] == '\0'
            || !local_name || local_name[0] == '\0') {
        return 0;
    }

    for (int i = 0; i < reg->record_count; i++)
        if (strcmp(reg->records[i].interface_name, interface_name) == 0
                && strcmp(reg->records[i].name, local_name) == 0) count++;
    for (int i = 0; i < reg->variant_count; i++)
        if (strcmp(reg->variants[i].interface_name, interface_name) == 0
                && strcmp(reg->variants[i].name, local_name) == 0) count++;
    for (int i = 0; i < reg->enum_count; i++)
        if (strcmp(reg->enums[i].interface_name, interface_name) == 0
                && strcmp(reg->enums[i].name, local_name) == 0) count++;
    for (int i = 0; i < reg->flags_count; i++)
        if (strcmp(reg->flags[i].interface_name, interface_name) == 0
                && strcmp(reg->flags[i].name, local_name) == 0) count++;
    for (int i = 0; i < reg->alias_count; i++)
        if (strcmp(reg->aliases[i].interface_name, interface_name) == 0
                && strcmp(reg->aliases[i].name, local_name) == 0) count++;
    for (int i = 0; i < reg->resource_count; i++)
        if (strcmp(reg->resources[i].interface_name, interface_name) == 0
                && strcmp(reg->resources[i].name, local_name) == 0) count++;

    return count;
}

static void wit_symbol_display_name(const WitRegistry *reg,
                                    const char *scope_package_full,
                                    const char *scope_interface_name,
                                    const char *symbol_name,
                                    char *out,
                                    int n)
{
    char package_full[MAX_PACKAGE];
    char package_name[MAX_NAME];
    char interface_name[MAX_NAME];
    char local_name[MAX_NAME];

    if (!out || n <= 0) return;
    out[0] = '\0';
    if (!symbol_name) return;

    wit_split_symbol_name(symbol_name,
                          package_full,
                          (int)sizeof(package_full),
                          interface_name,
                          (int)sizeof(interface_name),
                          local_name,
                          (int)sizeof(local_name));
    if (local_name[0] == '\0') {
        snprintf(out, n, "%s", symbol_name);
        return;
    }

    if ((!scope_package_full || scope_package_full[0] == '\0'
            || strcmp(scope_package_full, package_full) == 0)
            && scope_interface_name && scope_interface_name[0] != '\0'
            && strcmp(scope_interface_name, interface_name) == 0) {
        snprintf(out, n, "%s", local_name);
        return;
    }
    if (!reg || !wit_symbol_has_scope(symbol_name)) {
        snprintf(out, n, "%s", local_name);
        return;
    }
    if (wit_local_name_occurrences(reg, local_name) <= 1) {
        snprintf(out, n, "%s", local_name);
        return;
    }
    if (interface_name[0] != '\0' && wit_interface_local_occurrences(reg, interface_name, local_name) <= 1) {
        snprintf(out, n, "%s-%s", interface_name, local_name);
        return;
    }

    wit_package_name_from_full(package_full, package_name, (int)sizeof(package_name));
    if (package_name[0] != '\0' && interface_name[0] != '\0') {
        snprintf(out, n, "%s-%s-%s", package_name, interface_name, local_name);
    } else if (package_name[0] != '\0') {
        snprintf(out, n, "%s-%s", package_name, local_name);
    } else if (interface_name[0] != '\0') {
        snprintf(out, n, "%s-%s", interface_name, local_name);
    } else {
        snprintf(out, n, "%s", local_name);
    }
}

static void emit_trace_comment(FILE *out, const char *indent, const char *trace)
{
    if (!out || !trace || trace[0] == '\0') return;
    fprintf(out, "%s/* %s */\n", indent ? indent : "", trace);
}

static void emit_c_string_literal(FILE *out, const char *text)
{
    const unsigned char *p = (const unsigned char *)(text ? text : "");

    if (!out) return;

    fputc('"', out);
    while (*p) {
        switch (*p) {
        case '\\':
            fputs("\\\\", out);
            break;
        case '"':
            fputs("\\\"", out);
            break;
        case '\n':
            fputs("\\n", out);
            break;
        case '\r':
            fputs("\\r", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        default:
            if (*p < 0x20u || *p == 0x7Fu) {
                fprintf(out, "\\x%02X", *p);
            } else {
                fputc((int)*p, out);
            }
            break;
        }
        p++;
    }
    fputc('"', out);
}

static void format_field_trace(const WitRegistry *reg,
                               const char *scope_package_full,
                               const char *scope_interface_name,
                               const WitField *field,
                               char *out,
                               int n)
{
    char typebuf[256];

    if (!out || n <= 0) return;
    out[0] = '\0';
    if (!field) return;

    type_to_display_str(reg,
                        scope_package_full,
                        scope_interface_name,
                        field->wit_type,
                        typebuf,
                        (int)sizeof(typebuf));
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

static void format_variant_case_trace(const WitRegistry *reg,
                                      const char *scope_package_full,
                                      const char *scope_interface_name,
                                      const WitVariantCase *variant_case,
                                      char *out,
                                      int n)
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
        type_to_display_str(reg,
                            scope_package_full,
                            scope_interface_name,
                            variant_case->payload_type,
                            typebuf,
                            (int)sizeof(typebuf));
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
    WitTypeKind bare_kind;
    if (!scan_ident(s, name, MAX_NAME)) return -1;

    skip_whitespace(s);
    if (scanner_peek(s) != '<') {
        if (strcmp(name, "result") == 0) {
            int idx = type_alloc();
            if (idx < 0) return -1;
            g_type_pool[idx].kind = TYPE_RESULT;
            return idx;
        }
        /* bare identifier */
        int idx = type_alloc();
        if (idx < 0) return -1;
        g_type_pool[idx].kind = TYPE_IDENT;
        strncpy(g_type_pool[idx].ident, name, MAX_NAME - 1);
        return idx;
    }

    /* generic type: name<params...> */
    scanner_advance(s); /* consume '<' */

    if      (strcmp(name, "option") == 0) bare_kind = TYPE_OPTION;
    else if (strcmp(name, "list")   == 0) bare_kind = TYPE_LIST;
    else if (strcmp(name, "tuple")  == 0) bare_kind = TYPE_TUPLE;
    else if (strcmp(name, "result") == 0) bare_kind = TYPE_RESULT;
    else if (strcmp(name, "borrow") == 0) bare_kind = TYPE_BORROW;
    else {
        fprintf(stderr, "wit_codegen: line %d: unknown generic '%s'\n",
                s->line, name);
        return -1;
    }

    int idx = type_alloc();
    if (idx < 0) return -1;
    g_type_pool[idx].kind = bare_kind;

    /* parse comma-separated type parameters via recursion */
    while (g_type_pool[idx].param_count < MAX_TYPE_PARAMS) {
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
        (void)match_char(s, ',');
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
        (void)match_char(s, ',');
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
        (void)match_char(s, ',');
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
        (void)match_char(s, ',');
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
                             const char *scope_name,
                             const char *interface_name);
static int parse_world_decl(Scanner *s, WitRegistry *reg, const char *metadata);
static int scan_source_for_package_decl(const char *src,
                                        int len,
                                        char *out,
                                        int n);
static int parse_wit(Scanner *s, WitRegistry *reg);
static int resolve_use_bindings(WitRegistry *reg);
static int resolve_world_bindings(WitRegistry *reg);
static int resolve_registry_symbol_scopes(WitRegistry *reg);
static int parse_registry_source_file(WitRegistry *reg, const char *wit_path);
static int ensure_import_interface_loaded(WitRegistry *reg,
                                          const char *target_package_full,
                                          const char *target_interface_name);
static int ensure_import_world_loaded(WitRegistry *reg,
                                      const char *target_package_full,
                                      const char *target_world_name);

static int registry_has_loaded_path(const WitRegistry *reg, const char *path)
{
    if (!reg || !path || path[0] == '\0') return 0;
    for (int i = 0; i < reg->loaded_path_count; i++) {
        if (strcmp(reg->loaded_paths[i], path) == 0) {
            return 1;
        }
    }
    return 0;
}

static int registry_mark_loaded_path(WitRegistry *reg, const char *path)
{
    if (!reg || !path || path[0] == '\0') return 0;
    if (registry_has_loaded_path(reg, path)) return 1;
    if (reg->loaded_path_count >= MAX_LOADED_FILES) {
        fprintf(stderr, "wit_codegen: too many loaded WIT files\n");
        return 0;
    }
    snprintf(reg->loaded_paths[reg->loaded_path_count],
             sizeof(reg->loaded_paths[reg->loaded_path_count]),
             "%s",
             path);
    reg->loaded_path_count++;
    return 1;
}

static WitInterface *find_interface_decl(WitRegistry *reg,
                                         const char *package_full,
                                         const char *interface_name)
{
    if (!reg || !package_full || !interface_name) return NULL;

    for (int i = 0; i < reg->interface_count; i++) {
        if (strcmp(reg->interfaces[i].package_full, package_full) == 0
                && strcmp(reg->interfaces[i].name, interface_name) == 0) {
            return &reg->interfaces[i];
        }
    }
    return NULL;
}

static const WitInterface *find_interface_decl_const(const WitRegistry *reg,
                                                     const char *package_full,
                                                     const char *interface_name)
{
    return find_interface_decl((WitRegistry *)reg, package_full, interface_name);
}

static WitWorld *find_world_decl(WitRegistry *reg,
                                 const char *package_full,
                                 const char *world_name)
{
    if (!reg || !package_full || !world_name) return NULL;

    for (int i = 0; i < reg->world_count; i++) {
        if (strcmp(reg->worlds[i].package_full, package_full) == 0
                && strcmp(reg->worlds[i].name, world_name) == 0) {
            return &reg->worlds[i];
        }
    }
    return NULL;
}

static const WitWorld *find_world_decl_const(const WitRegistry *reg,
                                             const char *package_full,
                                             const char *world_name)
{
    return find_world_decl((WitRegistry *)reg, package_full, world_name);
}

static int registry_add_interface_with_origin(WitRegistry *reg,
                                              const char *package_full,
                                              const char *interface_name,
                                              const char *metadata,
                                              int imported,
                                              const char *origin_world_name,
                                              const char *origin_item_name)
{
    WitInterface *existing;

    if (!reg || !interface_name || interface_name[0] == '\0') {
        return 0;
    }

    existing = find_interface_decl(reg,
                                   package_full ? package_full : "",
                                   interface_name);
    if (existing) {
        if (metadata && metadata[0] != '\0' && existing->metadata[0] == '\0') {
            snprintf(existing->metadata, sizeof(existing->metadata), "%s", metadata);
        }
        if (origin_world_name && origin_world_name[0] != '\0' && existing->origin_world_name[0] == '\0') {
            snprintf(existing->origin_world_name,
                     sizeof(existing->origin_world_name),
                     "%s",
                     origin_world_name);
        }
        if (origin_item_name && origin_item_name[0] != '\0' && existing->origin_item_name[0] == '\0') {
            snprintf(existing->origin_item_name,
                     sizeof(existing->origin_item_name),
                     "%s",
                     origin_item_name);
        }
        if (imported) {
            existing->imported = 1;
        }
        return 1;
    }

    if (reg->interface_count >= MAX_TYPES) {
        fprintf(stderr, "wit_codegen: too many interfaces\n");
        return 0;
    }

    memset(&reg->interfaces[reg->interface_count], 0, sizeof(reg->interfaces[reg->interface_count]));
    snprintf(reg->interfaces[reg->interface_count].name,
             sizeof(reg->interfaces[reg->interface_count].name),
             "%s",
             interface_name);
    snprintf(reg->interfaces[reg->interface_count].package_full,
             sizeof(reg->interfaces[reg->interface_count].package_full),
             "%s",
             package_full ? package_full : "");
    if (metadata && metadata[0] != '\0') {
        snprintf(reg->interfaces[reg->interface_count].metadata,
                 sizeof(reg->interfaces[reg->interface_count].metadata),
                 "%s",
                 metadata);
    }
    if (origin_world_name && origin_world_name[0] != '\0') {
        snprintf(reg->interfaces[reg->interface_count].origin_world_name,
                 sizeof(reg->interfaces[reg->interface_count].origin_world_name),
                 "%s",
                 origin_world_name);
    }
    if (origin_item_name && origin_item_name[0] != '\0') {
        snprintf(reg->interfaces[reg->interface_count].origin_item_name,
                 sizeof(reg->interfaces[reg->interface_count].origin_item_name),
                 "%s",
                 origin_item_name);
    }
    reg->interfaces[reg->interface_count].imported = imported ? 1 : 0;
    reg->interface_count++;
    return 1;
}

static int registry_add_interface(WitRegistry *reg,
                                  const char *package_full,
                                  const char *interface_name,
                                  const char *metadata,
                                  int imported)
{
    return registry_add_interface_with_origin(reg,
                                              package_full,
                                              interface_name,
                                              metadata,
                                              imported,
                                              NULL,
                                              NULL);
}

static int registry_add_world(WitRegistry *reg,
                              const char *package_full,
                              const char *world_name,
                              const char *metadata,
                              int imported)
{
    WitWorld *existing;

    if (!reg || !world_name || world_name[0] == '\0') {
        return 0;
    }

    existing = find_world_decl(reg, package_full ? package_full : "", world_name);
    if (existing) {
        if (metadata && metadata[0] != '\0' && existing->metadata[0] == '\0') {
            snprintf(existing->metadata, sizeof(existing->metadata), "%s", metadata);
        }
        if (imported) {
            existing->imported = 1;
        }
        return 1;
    }

    if (reg->world_count >= MAX_WORLDS) {
        fprintf(stderr, "wit_codegen: too many worlds\n");
        return 0;
    }

    memset(&reg->worlds[reg->world_count], 0, sizeof(reg->worlds[reg->world_count]));
    snprintf(reg->worlds[reg->world_count].name,
             sizeof(reg->worlds[reg->world_count].name),
             "%s",
             world_name);
    snprintf(reg->worlds[reg->world_count].package_full,
             sizeof(reg->worlds[reg->world_count].package_full),
             "%s",
             package_full ? package_full : "");
    if (metadata && metadata[0] != '\0') {
        snprintf(reg->worlds[reg->world_count].metadata,
                 sizeof(reg->worlds[reg->world_count].metadata),
                 "%s",
                 metadata);
    }
    reg->worlds[reg->world_count].imported = imported ? 1 : 0;
    reg->world_count++;
    return 1;
}

static int registry_add_world_item(WitRegistry *reg, const WitWorldItem *item)
{
    WitWorldItem *dst;

    if (!reg || !item || !item->world_name[0] || !item->name[0]) {
        return 0;
    }

    for (int i = 0; i < reg->world_item_count; i++) {
        if (reg->world_items[i].kind == item->kind
                && strcmp(reg->world_items[i].package_full, item->package_full) == 0
                && strcmp(reg->world_items[i].world_name, item->world_name) == 0
                && strcmp(reg->world_items[i].name, item->name) == 0
                && strcmp(reg->world_items[i].target_package_full, item->target_package_full) == 0
                && strcmp(reg->world_items[i].target_name, item->target_name) == 0
                && strcmp(reg->world_items[i].lowered_target_package_full,
                          item->lowered_target_package_full) == 0
                && strcmp(reg->world_items[i].lowered_target_name,
                          item->lowered_target_name) == 0) {
            if (item->metadata[0] != '\0' && reg->world_items[i].metadata[0] == '\0') {
                snprintf(reg->world_items[i].metadata,
                         sizeof(reg->world_items[i].metadata),
                         "%s",
                         item->metadata);
            }
            if (item->target_kind != WIT_WORLD_TARGET_UNKNOWN) {
                reg->world_items[i].target_kind = item->target_kind;
            }
            if (item->lowered_target_package_full[0] != '\0'
                    && reg->world_items[i].lowered_target_package_full[0] == '\0') {
                snprintf(reg->world_items[i].lowered_target_package_full,
                         sizeof(reg->world_items[i].lowered_target_package_full),
                         "%s",
                         item->lowered_target_package_full);
            }
            if (item->lowered_target_name[0] != '\0'
                    && reg->world_items[i].lowered_target_name[0] == '\0') {
                snprintf(reg->world_items[i].lowered_target_name,
                         sizeof(reg->world_items[i].lowered_target_name),
                         "%s",
                         item->lowered_target_name);
            }
            if (item->imported) {
                reg->world_items[i].imported = 1;
            }
            return 1;
        }
    }

    if (reg->world_item_count >= MAX_WORLD_ITEMS) {
        fprintf(stderr, "wit_codegen: too many world items\n");
        return 0;
    }

    dst = &reg->world_items[reg->world_item_count++];
    *dst = *item;
    return 1;
}

static int registry_has_interface_symbols(const WitRegistry *reg,
                                          const char *package_full,
                                          const char *interface_name)
{
    if (!reg || !package_full || !interface_name) return 0;
    if (find_interface_decl_const(reg, package_full, interface_name)) {
        return 1;
    }

    for (int i = 0; i < reg->record_count; i++)
        if (strcmp(reg->records[i].package_full, package_full) == 0
                && strcmp(reg->records[i].interface_name, interface_name) == 0) return 1;
    for (int i = 0; i < reg->variant_count; i++)
        if (strcmp(reg->variants[i].package_full, package_full) == 0
                && strcmp(reg->variants[i].interface_name, interface_name) == 0) return 1;
    for (int i = 0; i < reg->enum_count; i++)
        if (strcmp(reg->enums[i].package_full, package_full) == 0
                && strcmp(reg->enums[i].interface_name, interface_name) == 0) return 1;
    for (int i = 0; i < reg->flags_count; i++)
        if (strcmp(reg->flags[i].package_full, package_full) == 0
                && strcmp(reg->flags[i].interface_name, interface_name) == 0) return 1;
    for (int i = 0; i < reg->alias_count; i++)
        if (strcmp(reg->aliases[i].package_full, package_full) == 0
                && strcmp(reg->aliases[i].interface_name, interface_name) == 0) return 1;
    for (int i = 0; i < reg->resource_count; i++)
        if (strcmp(reg->resources[i].package_full, package_full) == 0
                && strcmp(reg->resources[i].interface_name, interface_name) == 0) return 1;
    for (int i = 0; i < reg->use_binding_count; i++)
        if (strcmp(reg->use_bindings[i].package_full, package_full) == 0
                && strcmp(reg->use_bindings[i].interface_name, interface_name) == 0) return 1;
    return 0;
}

static int registry_add_use_binding(WitRegistry *reg,
                                    const char *interface_name,
                                    const char *local_name,
                                    const char *target_package_full,
                                    const char *target_interface_name,
                                    const char *target_name)
{
    WitUseBinding *binding;

    if (!reg || !interface_name || interface_name[0] == '\0'
            || !local_name || local_name[0] == '\0'
            || !target_interface_name || target_interface_name[0] == '\0'
            || !target_name || target_name[0] == '\0') {
        return 0;
    }

    for (int i = 0; i < reg->use_binding_count; i++) {
        binding = &reg->use_bindings[i];
        if (strcmp(binding->package_full, reg->package_full) == 0
                && strcmp(binding->interface_name, interface_name) == 0
                && strcmp(binding->local_name, local_name) == 0) {
            if (strcmp(binding->target_package_full, target_package_full ? target_package_full : reg->package_full) != 0
                    || strcmp(binding->target_interface_name, target_interface_name) != 0
                    || strcmp(binding->target_name, target_name) != 0) {
                codegen_die("conflicting use binding for %s in interface %s",
                            local_name,
                            interface_name);
            }
            return 1;
        }
    }

    if (reg->use_binding_count >= MAX_USE_BINDINGS) {
        fprintf(stderr, "wit_codegen: too many use bindings\n");
        return 0;
    }

    binding = &reg->use_bindings[reg->use_binding_count++];
    memset(binding, 0, sizeof(*binding));
    snprintf(binding->package_full, sizeof(binding->package_full), "%s", reg->package_full);
    snprintf(binding->interface_name, sizeof(binding->interface_name), "%s", interface_name);
    snprintf(binding->local_name, sizeof(binding->local_name), "%s", local_name);
    snprintf(binding->target_package_full,
             sizeof(binding->target_package_full),
             "%s",
             (target_package_full && target_package_full[0] != '\0') ? target_package_full : reg->package_full);
    snprintf(binding->target_interface_name, sizeof(binding->target_interface_name), "%s", target_interface_name);
    snprintf(binding->target_name, sizeof(binding->target_name), "%s", target_name);
    return 1;
}

static int registry_add_resource(WitRegistry *reg,
                                 const char *package_full,
                                 const char *name,
                                 const char *interface_name,
                                 const char *metadata,
                                 int imported)
{
    if (!reg || !name || name[0] == '\0') {
        return 0;
    }

    for (int i = 0; i < reg->resource_count; i++) {
        if (strcmp(reg->resources[i].name, name) == 0
                && strcmp(reg->resources[i].package_full, package_full ? package_full : "") == 0
                && strcmp(reg->resources[i].interface_name, interface_name ? interface_name : "") == 0) {
            if (metadata && metadata[0] != '\0' && reg->resources[i].metadata[0] == '\0') {
                snprintf(reg->resources[i].metadata,
                         sizeof(reg->resources[i].metadata),
                         "%s",
                         metadata);
            }
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
    snprintf(reg->resources[reg->resource_count].package_full,
             sizeof(reg->resources[reg->resource_count].package_full),
             "%s",
             package_full ? package_full : "");
    if (metadata && metadata[0] != '\0') {
        snprintf(reg->resources[reg->resource_count].metadata,
                 sizeof(reg->resources[reg->resource_count].metadata),
                 "%s",
                 metadata);
    }
    if (interface_name && interface_name[0] != '\0') {
        snprintf(reg->resources[reg->resource_count].interface_name,
                 sizeof(reg->resources[reg->resource_count].interface_name),
                 "%s",
                 interface_name);
    }
    wit_build_scoped_symbol_name(package_full,
                                 interface_name,
                                 name,
                                 reg->resources[reg->resource_count].symbol_name,
                                 (int)sizeof(reg->resources[reg->resource_count].symbol_name));
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
                            const char *scope_name,
                            const char *interface_name,
                            const char *metadata)
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
    }

    if (!expect_char(s, ';')) return 0;

    snprintf(func.package_full, sizeof(func.package_full), "%s", reg->package_full);
    snprintf(func.owner_name, sizeof(func.owner_name), "%s", scope_name ? scope_name : "");
    wit_build_scoped_symbol_name(reg->package_full,
                                 (scope == PARSE_SCOPE_RESOURCE) ? interface_name : NULL,
                                 scope_name ? scope_name : "",
                                 func.owner_symbol_name,
                                 (int)sizeof(func.owner_symbol_name));
    snprintf(func.interface_name,
             sizeof(func.interface_name),
             "%s",
             interface_name ? interface_name : "");
    if (metadata && metadata[0] != '\0') {
        snprintf(func.metadata, sizeof(func.metadata), "%s", metadata);
    }
    func.owner_kind = (scope == PARSE_SCOPE_RESOURCE) ? WIT_OWNER_RESOURCE : WIT_OWNER_INTERFACE;
    format_func_trace(&func, func.trace, (int)sizeof(func.trace));
    return registry_add_func(reg, &func);
}

static int try_parse_named_func(Scanner *s,
                                WitRegistry *reg,
                                ParseScope scope,
                                const char *scope_name,
                                const char *interface_name,
                                const char *metadata)
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
    return parse_named_func(s, reg, scope, scope_name, interface_name, metadata);
}

static int parse_constructor_func(Scanner *s,
                                  WitRegistry *reg,
                                  const char *scope_name,
                                  const char *interface_name,
                                  const char *metadata)
{
    WitFunc func;

    memset(&func, 0, sizeof(func));
    snprintf(func.name, sizeof(func.name), "constructor");
    func.kind = WIT_FUNC_CONSTRUCTOR;
    func.owner_kind = WIT_OWNER_RESOURCE;
    snprintf(func.package_full, sizeof(func.package_full), "%s", reg->package_full);
    snprintf(func.owner_name, sizeof(func.owner_name), "%s", scope_name ? scope_name : "");
    wit_build_scoped_symbol_name(reg->package_full,
                                 interface_name,
                                 scope_name ? scope_name : "",
                                 func.owner_symbol_name,
                                 (int)sizeof(func.owner_symbol_name));
    snprintf(func.interface_name,
             sizeof(func.interface_name),
             "%s",
             interface_name ? interface_name : "");
    if (metadata && metadata[0] != '\0') {
        snprintf(func.metadata, sizeof(func.metadata), "%s", metadata);
    }

    if (!match_keyword(s, "constructor")) return 0;
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
    format_func_trace(&func, func.trace, (int)sizeof(func.trace));
    return registry_add_func(reg, &func);
}

static int try_parse_constructor_func(Scanner *s,
                                      WitRegistry *reg,
                                      const char *scope_name,
                                      const char *interface_name,
                                      const char *metadata)
{
    Scanner probe = *s;
    if (!match_keyword(&probe, "constructor")) {
        return 0;
    }
    return parse_constructor_func(s, reg, scope_name, interface_name, metadata);
}

static void wit_trim_copy(char *out, int n, const char *src)
{
    if (!out || n <= 0) return;
    snprintf(out, n, "%s", src ? src : "");
    wit_trim(out);
}

static int parse_use_target_spec(const char *spec,
                                 const char *current_package_full,
                                 char *target_package_full,
                                 int target_package_n,
                                 char *target_interface_name,
                                 int target_interface_n)
{
    char trimmed[256];
    char package_part[MAX_PACKAGE];
    char interface_part[MAX_NAME];
    char *slash;
    char *at;

    wit_trim_copy(trimmed, (int)sizeof(trimmed), spec);
    if (trimmed[0] == '\0') return 0;
    while (trimmed[0] != '\0' && trimmed[strlen(trimmed) - 1u] == '.') {
        trimmed[strlen(trimmed) - 1u] = '\0';
    }

    slash = strchr(trimmed, '/');
    if (!slash) {
        snprintf(target_package_full, target_package_n, "%s", current_package_full ? current_package_full : "");
        snprintf(target_interface_name, target_interface_n, "%s", trimmed);
        return target_interface_name[0] != '\0';
    }

    *slash = '\0';
    snprintf(package_part, sizeof(package_part), "%s", trimmed);
    snprintf(interface_part, sizeof(interface_part), "%s", slash + 1);
    wit_trim(package_part);
    wit_trim(interface_part);

    at = strrchr(interface_part, '@');
    if (at) {
        *at = '\0';
        wit_trim(interface_part);
        snprintf(target_package_full, target_package_n, "%s@%s", package_part, at + 1);
    } else {
        snprintf(target_package_full, target_package_n, "%s", package_part);
    }
    snprintf(target_interface_name, target_interface_n, "%s", interface_part);
    return target_package_full[0] != '\0' && target_interface_name[0] != '\0';
}

static int parse_use_decl(Scanner *s, WitRegistry *reg, const char *interface_name)
{
    char raw[512];
    char target_spec[256];
    char target_package_full[MAX_PACKAGE];
    char target_interface_name[MAX_NAME];
    char *open_brace;
    char *close_brace;
    char *cursor;
    int i = 0;

    if (!interface_name || interface_name[0] == '\0') {
        codegen_die("top-level use declarations are not supported");
    }

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

    *open_brace = '\0';
    *close_brace = '\0';
    wit_trim_copy(target_spec, (int)sizeof(target_spec), raw);
    if (!parse_use_target_spec(target_spec,
                               reg->package_full,
                               target_package_full,
                               (int)sizeof(target_package_full),
                               target_interface_name,
                               (int)sizeof(target_interface_name))) {
        codegen_die("invalid use target '%s' in %s", target_spec, reg->source_path);
    }

    cursor = open_brace + 1;
    while (*cursor) {
        char entry[128];
        char imported_name[MAX_NAME];
        char local_name[MAX_NAME];
        char *comma = strchr(cursor, ',');
        char *as_kw;

        if (comma) {
            size_t len = (size_t)(comma - cursor);
            if (len >= sizeof(entry)) len = sizeof(entry) - 1u;
            memcpy(entry, cursor, len);
            entry[len] = '\0';
            cursor = comma + 1;
        } else {
            snprintf(entry, sizeof(entry), "%s", cursor);
            cursor += strlen(cursor);
        }
        wit_trim(entry);
        if (entry[0] == '\0') {
            continue;
        }

        as_kw = strstr(entry, " as ");
        if (as_kw) {
            *as_kw = '\0';
            wit_trim(entry);
            wit_trim_copy(imported_name, (int)sizeof(imported_name), entry);
            wit_trim_copy(local_name, (int)sizeof(local_name), as_kw + 4);
        } else {
            wit_trim_copy(imported_name, (int)sizeof(imported_name), entry);
            snprintf(local_name, sizeof(local_name), "%s", imported_name);
        }

        if (imported_name[0] != '\0'
                && !registry_add_use_binding(reg,
                                             interface_name,
                                             local_name,
                                             target_package_full,
                                             target_interface_name,
                                             imported_name)) {
            return 0;
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

static int parse_interface_decl(Scanner *s, WitRegistry *reg, const char *metadata)
{
    char name[MAX_NAME];

    if (!scan_ident(s, name, MAX_NAME)) return 0;
    if (!registry_add_interface(reg, reg->package_full, name, metadata, 0)) return 0;
    if (!expect_char(s, '{')) return 0;
    return parse_scope_items(s, reg, PARSE_SCOPE_INTERFACE, name, name);
}

static int parse_resource_decl(Scanner *s,
                               WitRegistry *reg,
                               const char *interface_name,
                               const char *metadata)
{
    char name[MAX_NAME];

    if (!scan_ident(s, name, MAX_NAME)) return 0;
    if (!registry_add_resource(reg, reg->package_full, name, interface_name, metadata, 0)) return 0;

    skip_whitespace(s);
    if (match_char(s, ';')) {
        return 1;
    }
    if (!expect_char(s, '{')) return 0;
    if (!parse_scope_items(s, reg, PARSE_SCOPE_RESOURCE, name, interface_name)) return 0;
    (void)match_trailing_semicolon(s);
    return 1;
}

static int world_interface_name_in_use(const WitRegistry *reg,
                                       const char *package_full,
                                       const char *interface_name)
{
    if (!reg || !interface_name || interface_name[0] == '\0') return 0;
    if (find_interface_decl_const(reg, package_full ? package_full : "", interface_name)) {
        return 1;
    }
    return registry_has_interface_symbols(reg, package_full ? package_full : "", interface_name);
}

static int make_inline_world_interface_name(const WitRegistry *reg,
                                            const char *world_name,
                                            const char *item_name,
                                            char *out,
                                            int n)
{
    char candidate[MAX_NAME];

    if (!out || n <= 0) return 0;
    out[0] = '\0';
    if (!reg || !item_name || item_name[0] == '\0') return 0;

    snprintf(candidate, sizeof(candidate), "%s", item_name);
    if (!world_interface_name_in_use(reg, reg->package_full, candidate)) {
        snprintf(out, n, "%s", candidate);
        return 1;
    }

    if (world_name && world_name[0] != '\0') {
        snprintf(candidate, sizeof(candidate), "%s-%s", world_name, item_name);
        if (!world_interface_name_in_use(reg, reg->package_full, candidate)) {
            snprintf(out, n, "%s", candidate);
            return 1;
        }
    }

    if (reg->package_tail_raw[0] != '\0' && world_name && world_name[0] != '\0') {
        snprintf(candidate, sizeof(candidate), "%s-%s-%s", reg->package_tail_raw, world_name, item_name);
        if (!world_interface_name_in_use(reg, reg->package_full, candidate)) {
            snprintf(out, n, "%s", candidate);
            return 1;
        }
    }

    return 0;
}

static void init_world_item(WitWorldItem *item,
                            WitWorldItemKind kind,
                            WitWorldTargetKind target_kind,
                            const char *package_full,
                            const char *world_name,
                            const char *item_name,
                            const char *target_package_full,
                            const char *target_name,
                            const char *lowered_target_package_full,
                            const char *lowered_target_name,
                            const char *metadata)
{
    if (!item) return;
    memset(item, 0, sizeof(*item));
    item->kind = kind;
    item->target_kind = target_kind;
    snprintf(item->package_full, sizeof(item->package_full), "%s", package_full ? package_full : "");
    snprintf(item->world_name, sizeof(item->world_name), "%s", world_name ? world_name : "");
    snprintf(item->name, sizeof(item->name), "%s", item_name ? item_name : "");
    snprintf(item->target_package_full,
             sizeof(item->target_package_full),
             "%s",
             target_package_full ? target_package_full : "");
    snprintf(item->target_name, sizeof(item->target_name), "%s", target_name ? target_name : "");
    snprintf(item->lowered_target_package_full,
             sizeof(item->lowered_target_package_full),
             "%s",
             lowered_target_package_full ? lowered_target_package_full : "");
    snprintf(item->lowered_target_name,
             sizeof(item->lowered_target_name),
             "%s",
             lowered_target_name ? lowered_target_name : "");
    if (metadata && metadata[0] != '\0') {
        snprintf(item->metadata, sizeof(item->metadata), "%s", metadata);
    }
}

static int parse_world_item_reference(Scanner *s,
                                      WitRegistry *reg,
                                      const char *world_name,
                                      WitWorldItemKind kind,
                                      const char *binding_name,
                                      const char *metadata)
{
    WitWorldItem item;
    char raw[512];
    char target_package_full[MAX_PACKAGE];
    char target_name[MAX_NAME];
    int i = 0;

    skip_whitespace(s);
    while (!scanner_eof(s) && scanner_peek(s) != ';' && i < (int)sizeof(raw) - 1) {
        raw[i++] = scanner_advance(s);
    }
    raw[i] = '\0';
    if (!expect_char(s, ';')) return 0;
    wit_trim(raw);
    if (raw[0] == '\0') {
        codegen_die("empty world item in %s world %s",
                    reg->source_path,
                    world_name ? world_name : "<unknown>");
    }

    if (!parse_use_target_spec(raw,
                               reg->package_full,
                               target_package_full,
                               (int)sizeof(target_package_full),
                               target_name,
                               (int)sizeof(target_name))) {
        codegen_die("invalid world item target '%s' in %s", raw, reg->source_path);
    }

    init_world_item(&item,
                    kind,
                    (kind == WIT_WORLD_ITEM_INCLUDE) ? WIT_WORLD_TARGET_WORLD : WIT_WORLD_TARGET_INTERFACE,
                    reg->package_full,
                    world_name,
                    (binding_name && binding_name[0] != '\0') ? binding_name : target_name,
                    target_package_full,
                    target_name,
                    target_package_full,
                    target_name,
                    metadata);
    return registry_add_world_item(reg, &item);
}

static int parse_inline_world_interface_item(Scanner *s,
                                             WitRegistry *reg,
                                             const char *world_name,
                                             WitWorldItemKind kind,
                                             const char *item_name,
                                             const char *metadata)
{
    WitWorldItem item;
    char interface_name[MAX_NAME];

    if (!make_inline_world_interface_name(reg,
                                          world_name,
                                          item_name,
                                          interface_name,
                                          (int)sizeof(interface_name))) {
        codegen_die("unable to create a unique inline interface name for %s in world %s",
                    item_name,
                    world_name ? world_name : "<unknown>");
    }

    if (!registry_add_interface_with_origin(reg,
                                            reg->package_full,
                                            interface_name,
                                            metadata,
                                            0,
                                            world_name,
                                            item_name)) {
        return 0;
    }
    if (!expect_char(s, '{')) return 0;
    if (!parse_scope_items(s, reg, PARSE_SCOPE_INTERFACE, interface_name, interface_name)) return 0;
    (void)match_trailing_semicolon(s);

    init_world_item(&item,
                    kind,
                    WIT_WORLD_TARGET_INTERFACE,
                    reg->package_full,
                    world_name,
                    item_name,
                    reg->package_full,
                    item_name,
                    reg->package_full,
                    interface_name,
                    metadata);
    return registry_add_world_item(reg, &item);
}

static int parse_inline_world_func_item(Scanner *s,
                                        WitRegistry *reg,
                                        const char *world_name,
                                        WitWorldItemKind kind,
                                        const char *item_name,
                                        const char *metadata)
{
    WitFunc func;
    WitWorldItem item;
    char interface_name[MAX_NAME];

    if (!match_keyword(s, "func")) return 0;
    if (!make_inline_world_interface_name(reg,
                                          world_name,
                                          item_name,
                                          interface_name,
                                          (int)sizeof(interface_name))) {
        codegen_die("unable to create a unique inline function surface name for %s in world %s",
                    item_name,
                    world_name ? world_name : "<unknown>");
    }

    if (!registry_add_interface_with_origin(reg,
                                            reg->package_full,
                                            interface_name,
                                            metadata,
                                            0,
                                            world_name,
                                            item_name)) {
        return 0;
    }

    memset(&func, 0, sizeof(func));
    snprintf(func.name, sizeof(func.name), "%s", item_name);
    snprintf(func.package_full, sizeof(func.package_full), "%s", reg->package_full);
    snprintf(func.owner_name, sizeof(func.owner_name), "%s", interface_name);
    wit_build_scoped_symbol_name(reg->package_full,
                                 NULL,
                                 interface_name,
                                 func.owner_symbol_name,
                                 (int)sizeof(func.owner_symbol_name));
    snprintf(func.interface_name, sizeof(func.interface_name), "%s", interface_name);
    if (metadata && metadata[0] != '\0') {
        snprintf(func.metadata, sizeof(func.metadata), "%s", metadata);
    }
    func.kind = WIT_FUNC_FREE;
    func.owner_kind = WIT_OWNER_INTERFACE;

    if (!parse_param_list(s, func.params, &func.param_count, MAX_FIELDS)) return 0;
    func.result_type = -1;
    if (match_arrow(s)) {
        func.result_type = parse_type_expr(s);
        if (func.result_type < 0) return 0;
    }
    if (!expect_char(s, ';')) return 0;

    format_func_trace(&func, func.trace, (int)sizeof(func.trace));
    if (!registry_add_func(reg, &func)) {
        return 0;
    }

    init_world_item(&item,
                    kind,
                    WIT_WORLD_TARGET_FUNCTION,
                    reg->package_full,
                    world_name,
                    item_name,
                    reg->package_full,
                    item_name,
                    reg->package_full,
                    interface_name,
                    metadata);
    return registry_add_world_item(reg, &item);
}

static int parse_world_item_decl(Scanner *s,
                                 WitRegistry *reg,
                                 const char *world_name,
                                 const char *metadata)
{
    Scanner probe;
    char item_name[MAX_NAME];

    if (match_keyword(s, "include")) {
        return parse_world_item_reference(s,
                                         reg,
                                         world_name,
                                         WIT_WORLD_ITEM_INCLUDE,
                                         NULL,
                                         metadata);
    } else if (match_keyword(s, "import")) {
        probe = *s;
        if (scan_ident(&probe, item_name, MAX_NAME) && match_char(&probe, ':')) {
            if (match_keyword(&probe, "interface")) {
                if (!scan_ident(s, item_name, MAX_NAME)) return 0;
                if (!expect_char(s, ':')) return 0;
                if (!match_keyword(s, "interface")) return 0;
                return parse_inline_world_interface_item(s,
                                                         reg,
                                                         world_name,
                                                         WIT_WORLD_ITEM_IMPORT,
                                                         item_name,
                                                         metadata);
            }
            if (match_keyword(&probe, "func")) {
                if (!scan_ident(s, item_name, MAX_NAME)) return 0;
                if (!expect_char(s, ':')) return 0;
                return parse_inline_world_func_item(s,
                                                    reg,
                                                    world_name,
                                                    WIT_WORLD_ITEM_IMPORT,
                                                    item_name,
                                                    metadata);
            }
        }
        return parse_world_item_reference(s,
                                         reg,
                                         world_name,
                                         WIT_WORLD_ITEM_IMPORT,
                                         NULL,
                                         metadata);
    } else if (match_keyword(s, "export")) {
        probe = *s;
        if (scan_ident(&probe, item_name, MAX_NAME) && match_char(&probe, ':')) {
            if (match_keyword(&probe, "interface")) {
                if (!scan_ident(s, item_name, MAX_NAME)) return 0;
                if (!expect_char(s, ':')) return 0;
                if (!match_keyword(s, "interface")) return 0;
                return parse_inline_world_interface_item(s,
                                                         reg,
                                                         world_name,
                                                         WIT_WORLD_ITEM_EXPORT,
                                                         item_name,
                                                         metadata);
            }
            if (match_keyword(&probe, "func")) {
                if (!scan_ident(s, item_name, MAX_NAME)) return 0;
                if (!expect_char(s, ':')) return 0;
                return parse_inline_world_func_item(s,
                                                    reg,
                                                    world_name,
                                                    WIT_WORLD_ITEM_EXPORT,
                                                    item_name,
                                                    metadata);
            }
        }
        return parse_world_item_reference(s,
                                         reg,
                                         world_name,
                                         WIT_WORLD_ITEM_EXPORT,
                                         NULL,
                                         metadata);
    } else {
        return 0;
    }
}

static int parse_world_decl(Scanner *s, WitRegistry *reg, const char *metadata)
{
    char name[MAX_NAME];
    char item_metadata[MAX_METADATA];

    if (!scan_ident(s, name, MAX_NAME)) return 0;
    if (!registry_add_world(reg, reg->package_full, name, metadata, 0)) return 0;

    skip_whitespace(s);
    if (match_char(s, ';')) {
        return 1;
    }
    if (!expect_char(s, '{')) return 0;

    while (!scanner_eof(s)) {
        consume_leading_metadata(s, item_metadata, (int)sizeof(item_metadata));
        skip_whitespace(s);
        if (scanner_peek(s) == '}') {
            scanner_advance(s);
            (void)match_trailing_semicolon(s);
            return 1;
        }
        if (!parse_world_item_decl(s, reg, name, item_metadata)) {
            codegen_die("unsupported world item in %s at line %d", reg->source_path, s->line);
        }
    }

    return 0;
}

static int apply_package_decl(WitRegistry *reg, const char *raw_decl)
{
    char raw[MAX_PACKAGE];
    const char *cursor;
    const char *colon;
    const char *at;

    if (!reg || !raw_decl) return 0;

    snprintf(raw, sizeof(raw), "%s", raw_decl);
    wit_trim(raw);
    if (raw[0] == '\0') {
        codegen_die("package declaration is empty");
    }

    reg->package_full[0] = '\0';
    reg->package_namespace[0] = '\0';
    reg->package_name[0] = '\0';
    reg->package_version[0] = '\0';

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

static int parse_package_decl(Scanner *s, WitRegistry *reg)
{
    char raw[MAX_PACKAGE];
    int i = 0;

    skip_whitespace(s);
    while (!scanner_eof(s) && scanner_peek(s) != ';' && i < MAX_PACKAGE - 1) {
        raw[i++] = scanner_advance(s);
    }
    raw[i] = '\0';
    if (!expect_char(s, ';')) return 0;

    return apply_package_decl(reg, raw);
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

static int infer_package_decl_from_siblings(WitRegistry *reg, const char *wit_path)
{
    char dir_path[MAX_PATH_TEXT];
    char file_base[MAX_NAME];
    char discovered[MAX_PACKAGE];
    DIR *dir;
    struct dirent *entry;

    if (!reg || !wit_path || wit_path[0] == '\0' || reg->package_name[0] != '\0') {
        return 1;
    }

    path_dirname(wit_path, dir_path, (int)sizeof(dir_path));
    path_basename(wit_path, file_base, (int)sizeof(file_base));
    discovered[0] = '\0';

    dir = opendir(dir_path[0] != '\0' ? dir_path : ".");
    if (!dir) {
        return 1;
    }

    while ((entry = readdir(dir)) != NULL) {
        char sibling_path[MAX_PATH_TEXT];
        char raw_package[MAX_PACKAGE];
        char *src;
        long src_len = 0;

        if (!path_has_extension(entry->d_name, ".wit")) {
            continue;
        }
        if (strcmp(entry->d_name, file_base) == 0) {
            continue;
        }

        snprintf(sibling_path, sizeof(sibling_path), "%s/%s", dir_path, entry->d_name);
        src = read_text_file(sibling_path, &src_len);
        if (!src) {
            continue;
        }

        raw_package[0] = '\0';
        if (scan_source_for_package_decl(src, (int)src_len, raw_package, (int)sizeof(raw_package))) {
            if (discovered[0] == '\0') {
                snprintf(discovered, sizeof(discovered), "%s", raw_package);
            } else if (strcmp(discovered, raw_package) != 0) {
                closedir(dir);
                free(src);
                codegen_die("conflicting sibling package declarations for %s: %s vs %s",
                            wit_path,
                            discovered,
                            raw_package);
            }
        }
        free(src);
    }

    closedir(dir);
    if (discovered[0] != '\0') {
        return apply_package_decl(reg, discovered);
    }
    return 1;
}

static int find_package_wit_dir(const char *wit_path,
                                const char *package_name,
                                char *out,
                                int n)
{
    char current[MAX_PATH_TEXT];
    char parent[MAX_PATH_TEXT];

    if (!wit_path || !package_name || package_name[0] == '\0' || !out || n <= 0) {
        return 0;
    }

    path_dirname(wit_path, current, (int)sizeof(current));
    while (current[0] != '\0') {
        char base[MAX_NAME];
        char package_dir[MAX_PATH_TEXT];
        char package_base[MAX_NAME];

        path_basename(current, base, (int)sizeof(base));
        if (strcmp(base, "wit") == 0) {
            path_dirname(current, package_dir, (int)sizeof(package_dir));
            path_basename(package_dir, package_base, (int)sizeof(package_base));
            if (strcmp(package_base, package_name) == 0) {
                snprintf(out, n, "%s", current);
                return 1;
            }
        }

        path_dirname(current, parent, (int)sizeof(parent));
        if (strcmp(parent, current) == 0) break;
        snprintf(current, sizeof(current), "%s", parent);
    }

    return 0;
}

static int find_target_package_wit_dir(const WitRegistry *reg,
                                       const char *target_package_full,
                                       char *out,
                                       int n)
{
    char current_package_name[MAX_NAME];
    char target_package_name[MAX_NAME];
    char current_wit_dir[MAX_PATH_TEXT];
    char package_dir[MAX_PATH_TEXT];
    char packages_root[MAX_PATH_TEXT];

    if (!reg || !out || n <= 0) {
        return 0;
    }
    out[0] = '\0';

    if (target_package_full && target_package_full[0] != '\0'
            && strcmp(target_package_full, reg->package_full) != 0) {
        wit_package_name_from_full(reg->package_full, current_package_name, (int)sizeof(current_package_name));
        wit_package_name_from_full(target_package_full, target_package_name, (int)sizeof(target_package_name));
        if (!find_package_wit_dir(reg->source_path, current_package_name, current_wit_dir, (int)sizeof(current_wit_dir))) {
            return 0;
        }
        path_dirname(current_wit_dir, package_dir, (int)sizeof(package_dir));
        path_dirname(package_dir, packages_root, (int)sizeof(packages_root));
        snprintf(out, n, "%s/%s/wit", packages_root, target_package_name);
        return 1;
    }

    path_dirname(reg->source_path, current_wit_dir, (int)sizeof(current_wit_dir));
    snprintf(out, n, "%s", current_wit_dir);
    return 1;
}

static int scan_source_for_named_decl(const char *src,
                                      int len,
                                      const char *keyword,
                                      const char *decl_name)
{
    Scanner s;
    char name[MAX_NAME];

    if (!src || len < 0 || !keyword || !decl_name || decl_name[0] == '\0') {
        return 0;
    }

    scanner_init(&s, src, len);
    while (!scanner_eof(&s)) {
        skip_whitespace(&s);
        if (scanner_eof(&s)) {
            break;
        }
        if (match_keyword(&s, keyword)) {
            if (scan_ident(&s, name, (int)sizeof(name)) && strcmp(name, decl_name) == 0) {
                return 1;
            }
            continue;
        }
        scanner_advance(&s);
    }

    return 0;
}

static int file_declares_named_wit_item(const char *path,
                                        const char *keyword,
                                        const char *decl_name)
{
    char *src;
    long len = 0;
    int found;

    if (!path || !keyword || !decl_name) return 0;
    src = read_text_file(path, &len);
    if (!src) {
        return 0;
    }
    found = scan_source_for_named_decl(src, (int)len, keyword, decl_name);
    free(src);
    return found;
}

static int find_named_decl_wit_path_in_dir(const char *wit_dir,
                                           const char *keyword,
                                           const char *decl_name,
                                           char *out,
                                           int n)
{
    DIR *dir;
    struct dirent *entry;

    if (!wit_dir || !keyword || !decl_name || !out || n <= 0) {
        return 0;
    }
    out[0] = '\0';

    dir = opendir(wit_dir);
    if (!dir) {
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        char candidate[MAX_PATH_TEXT];

        if (entry->d_name[0] == '.') {
            continue;
        }
        if (!path_has_extension(entry->d_name, ".wit")) {
            continue;
        }
        snprintf(candidate, sizeof(candidate), "%s/%s", wit_dir, entry->d_name);
        if (!file_declares_named_wit_item(candidate, keyword, decl_name)) {
            continue;
        }
        snprintf(out, n, "%s", candidate);
        closedir(dir);
        return 1;
    }

    closedir(dir);
    return 0;
}

static int resolve_import_wit_path(const WitRegistry *reg,
                                   const char *target_package_full,
                                   const char *target_interface_name,
                                   char *out,
                                   int n)
{
    char wit_dir[MAX_PATH_TEXT];

    if (!reg || !target_interface_name || target_interface_name[0] == '\0' || !out || n <= 0) {
        return 0;
    }
    out[0] = '\0';
    if (!find_target_package_wit_dir(reg,
                                     target_package_full ? target_package_full : reg->package_full,
                                     wit_dir,
                                     (int)sizeof(wit_dir))) {
        return 0;
    }
    snprintf(out, n, "%s/%s.wit", wit_dir, target_interface_name);
    if (file_exists(out) && file_declares_named_wit_item(out, "interface", target_interface_name)) {
        return 1;
    }
    return find_named_decl_wit_path_in_dir(wit_dir, "interface", target_interface_name, out, n);
}

static int resolve_import_world_wit_path(const WitRegistry *reg,
                                         const char *target_package_full,
                                         const char *target_world_name,
                                         char *out,
                                         int n)
{
    char wit_dir[MAX_PATH_TEXT];

    if (!reg || !target_world_name || target_world_name[0] == '\0') {
        return 0;
    }
    if (out && n > 0) out[0] = '\0';

    if (!find_target_package_wit_dir(reg,
                                     target_package_full ? target_package_full : reg->package_full,
                                     wit_dir,
                                     (int)sizeof(wit_dir))) {
        return 0;
    }

    snprintf(out, n, "%s/%s.wit", wit_dir, target_world_name);
    if (file_exists(out) && file_declares_named_wit_item(out, "world", target_world_name)) {
        return 1;
    }
    snprintf(out, n, "%s/world.wit", wit_dir);
    if (file_exists(out) && file_declares_named_wit_item(out, "world", target_world_name)) {
        return 1;
    }
    return find_named_decl_wit_path_in_dir(wit_dir, "world", target_world_name, out, n);
}

static int registry_has_exact_symbol(const WitRegistry *reg, const char *symbol_name)
{
    if (!reg || !symbol_name || symbol_name[0] == '\0') return 0;

    for (int i = 0; i < reg->alias_count; i++)
        if (strcmp(reg->aliases[i].symbol_name, symbol_name) == 0) return 1;
    for (int i = 0; i < reg->record_count; i++)
        if (strcmp(reg->records[i].symbol_name, symbol_name) == 0) return 1;
    for (int i = 0; i < reg->variant_count; i++)
        if (strcmp(reg->variants[i].symbol_name, symbol_name) == 0) return 1;
    for (int i = 0; i < reg->enum_count; i++)
        if (strcmp(reg->enums[i].symbol_name, symbol_name) == 0) return 1;
    for (int i = 0; i < reg->flags_count; i++)
        if (strcmp(reg->flags[i].symbol_name, symbol_name) == 0) return 1;
    for (int i = 0; i < reg->resource_count; i++)
        if (strcmp(reg->resources[i].symbol_name, symbol_name) == 0) return 1;
    return 0;
}

static int registry_find_exact_use_binding(const WitRegistry *reg,
                                           const WitUseBinding *binding)
{
    if (!reg || !binding) return -1;

    for (int i = 0; i < reg->use_binding_count; i++) {
        if (strcmp(reg->use_bindings[i].package_full, binding->package_full) == 0
                && strcmp(reg->use_bindings[i].interface_name, binding->interface_name) == 0
                && strcmp(reg->use_bindings[i].local_name, binding->local_name) == 0
                && strcmp(reg->use_bindings[i].target_package_full, binding->target_package_full) == 0
                && strcmp(reg->use_bindings[i].target_interface_name, binding->target_interface_name) == 0
                && strcmp(reg->use_bindings[i].target_name, binding->target_name) == 0) {
            return i;
        }
    }

    return -1;
}

static int merge_import_registry(WitRegistry *dst, const WitRegistry *src)
{
    if (!dst || !src) return 0;

    for (int i = 0; i < src->interface_count; i++) {
        if (!registry_add_interface_with_origin(dst,
                                                src->interfaces[i].package_full,
                                                src->interfaces[i].name,
                                                src->interfaces[i].metadata,
                                                1,
                                                src->interfaces[i].origin_world_name,
                                                src->interfaces[i].origin_item_name)) {
            return 0;
        }
    }
    for (int i = 0; i < src->world_count; i++) {
        if (!registry_add_world(dst,
                                src->worlds[i].package_full,
                                src->worlds[i].name,
                                src->worlds[i].metadata,
                                1)) {
            return 0;
        }
    }
    for (int i = 0; i < src->world_item_count; i++) {
        WitWorldItem imported_item = src->world_items[i];

        imported_item.imported = 1;
        if (!registry_add_world_item(dst, &imported_item)) {
            return 0;
        }
    }
    for (int i = 0; i < src->alias_count; i++) {
        if (!registry_has_exact_symbol(dst, src->aliases[i].symbol_name)) {
            if (dst->alias_count >= MAX_TYPES) return 0;
            dst->aliases[dst->alias_count++] = src->aliases[i];
        }
    }
    for (int i = 0; i < src->record_count; i++) {
        if (!registry_has_exact_symbol(dst, src->records[i].symbol_name)) {
            if (dst->record_count >= MAX_TYPES) return 0;
            dst->records[dst->record_count++] = src->records[i];
        }
    }
    for (int i = 0; i < src->variant_count; i++) {
        if (!registry_has_exact_symbol(dst, src->variants[i].symbol_name)) {
            if (dst->variant_count >= MAX_TYPES) return 0;
            dst->variants[dst->variant_count++] = src->variants[i];
        }
    }
    for (int i = 0; i < src->enum_count; i++) {
        if (!registry_has_exact_symbol(dst, src->enums[i].symbol_name)) {
            if (dst->enum_count >= MAX_TYPES) return 0;
            dst->enums[dst->enum_count++] = src->enums[i];
        }
    }
    for (int i = 0; i < src->flags_count; i++) {
        if (!registry_has_exact_symbol(dst, src->flags[i].symbol_name)) {
            if (dst->flags_count >= MAX_TYPES) return 0;
            dst->flags[dst->flags_count++] = src->flags[i];
        }
    }
    for (int i = 0; i < src->resource_count; i++) {
        if (!registry_has_exact_symbol(dst, src->resources[i].symbol_name)) {
            if (dst->resource_count >= MAX_TYPES) return 0;
            dst->resources[dst->resource_count++] = src->resources[i];
        }
    }
    for (int i = 0; i < src->use_binding_count; i++) {
        int use_idx = registry_find_exact_use_binding(dst, &src->use_bindings[i]);

        if (use_idx >= 0) {
            if (dst->use_bindings[use_idx].target_symbol_name[0] == '\0'
                    && src->use_bindings[i].target_symbol_name[0] != '\0') {
                snprintf(dst->use_bindings[use_idx].target_symbol_name,
                         sizeof(dst->use_bindings[use_idx].target_symbol_name),
                         "%s",
                         src->use_bindings[i].target_symbol_name);
            }
            continue;
        }
        if (dst->use_binding_count >= MAX_USE_BINDINGS) return 0;
        dst->use_bindings[dst->use_binding_count++] = src->use_bindings[i];
    }
    for (int i = 0; i < src->func_count; i++) {
        int seen = 0;

        for (int j = 0; j < dst->func_count; j++) {
            if (strcmp(dst->funcs[j].name, src->funcs[i].name) == 0
                    && strcmp(dst->funcs[j].package_full, src->funcs[i].package_full) == 0
                    && strcmp(dst->funcs[j].owner_name, src->funcs[i].owner_name) == 0
                    && strcmp(dst->funcs[j].interface_name, src->funcs[i].interface_name) == 0
                    && dst->funcs[j].kind == src->funcs[i].kind
                    && dst->funcs[j].owner_kind == src->funcs[i].owner_kind) {
                seen = 1;
                break;
            }
        }
        if (seen) {
            continue;
        }
        if (dst->func_count >= MAX_FUNCS) return 0;
        dst->funcs[dst->func_count++] = src->funcs[i];
    }
    for (int i = 0; i < src->loaded_path_count; i++) {
        if (!registry_mark_loaded_path(dst, src->loaded_paths[i])) {
            return 0;
        }
    }
    return 1;
}

static int init_registry_for_source(WitRegistry *reg,
                                    const char *wit_path,
                                    const char *src,
                                    int src_len)
{
    char raw_package[MAX_PACKAGE];

    if (!reg || !wit_path || !src) return 0;
    memset(reg, 0, sizeof(*reg));
    snprintf(reg->source_path, sizeof(reg->source_path), "%s", wit_path);

    raw_package[0] = '\0';
    if (scan_source_for_package_decl(src, src_len, raw_package, (int)sizeof(raw_package))) {
        if (!apply_package_decl(reg, raw_package)) return 0;
    } else if (!infer_package_decl_from_siblings(reg, wit_path)) {
        return 0;
    }

    if (!registry_mark_loaded_path(reg, wit_path)) return 0;
    return 1;
}

static int load_imported_registry_path(WitRegistry *reg, const char *import_path)
{
    WitRegistry *imported = NULL;

    if (!reg || !import_path || import_path[0] == '\0') return 0;
    if (registry_has_loaded_path(reg, import_path)) {
        return 1;
    }

    imported = (WitRegistry *)malloc(sizeof(*imported));
    if (!imported) {
        fprintf(stderr, "wit_codegen: out of memory while loading %s\n", import_path);
        return 0;
    }
    if (!parse_registry_source_file(imported, import_path)) {
        free(imported);
        return 0;
    }
    if (!merge_import_registry(reg, imported)) {
        free(imported);
        return 0;
    }
    free(imported);
    return 1;
}

static int ensure_import_interface_loaded(WitRegistry *reg,
                                          const char *target_package_full,
                                          const char *target_interface_name)
{
    char import_path[MAX_PATH_TEXT];
    const char *pkg;

    if (!reg || !target_interface_name || target_interface_name[0] == '\0') return 0;
    pkg = (target_package_full && target_package_full[0] != '\0')
        ? target_package_full
        : reg->package_full;
    if (registry_has_interface_symbols(reg, pkg, target_interface_name)) {
        return 1;
    }
    if (!resolve_import_wit_path(reg,
                                 pkg,
                                 target_interface_name,
                                 import_path,
                                 (int)sizeof(import_path))) {
        if (target_package_full && target_package_full[0] != '\0'
                && strcmp(target_package_full, reg->package_full) != 0) {
            codegen_die("unable to locate imported interface %s from package %s for %s",
                        target_interface_name,
                        target_package_full,
                        reg->source_path);
        }
        return 1;
    }
    if (!load_imported_registry_path(reg, import_path)) {
        return 0;
    }
    if (!registry_has_interface_symbols(reg, pkg, target_interface_name)) {
        codegen_die("unable to resolve imported interface %s from package %s for %s",
                    target_interface_name,
                    pkg,
                    reg->source_path);
    }
    return 1;
}

static int ensure_import_world_loaded(WitRegistry *reg,
                                      const char *target_package_full,
                                      const char *target_world_name)
{
    char import_path[MAX_PATH_TEXT];
    const char *pkg;

    if (!reg || !target_world_name || target_world_name[0] == '\0') return 0;
    pkg = (target_package_full && target_package_full[0] != '\0')
        ? target_package_full
        : reg->package_full;
    if (find_world_decl_const(reg, pkg, target_world_name)) {
        return 1;
    }
    if (!resolve_import_world_wit_path(reg,
                                       pkg,
                                       target_world_name,
                                       import_path,
                                       (int)sizeof(import_path))) {
        if (target_package_full && target_package_full[0] != '\0'
                && strcmp(target_package_full, reg->package_full) != 0) {
            codegen_die("unable to locate imported world %s from package %s for %s",
                        target_world_name,
                        target_package_full,
                        reg->source_path);
        }
        return 1;
    }
    if (!load_imported_registry_path(reg, import_path)) {
        return 0;
    }
    if (!find_world_decl_const(reg, pkg, target_world_name)) {
        codegen_die("unable to resolve imported world %s from package %s for %s",
                    target_world_name,
                    pkg,
                    reg->source_path);
    }
    return 1;
}

static int parse_registry_source_file(WitRegistry *reg, const char *wit_path)
{
    char *src;
    long src_len = 0;
    Scanner scanner;

    if (!reg || !wit_path || wit_path[0] == '\0') return 0;

    src = read_text_file(wit_path, &src_len);
    if (!src) {
        fprintf(stderr, "wit_codegen: cannot open %s\n", wit_path);
        return 0;
    }
    if (!init_registry_for_source(reg, wit_path, src, (int)src_len)) {
        free(src);
        return 0;
    }

    scanner_init(&scanner, src, (int)src_len);
    if (!parse_wit(&scanner, reg)) {
        fprintf(stderr, "wit_codegen: parse failed at line %d col %d\n",
                scanner.line, scanner.col);
        free(src);
        return 0;
    }
    if (!resolve_use_bindings(reg)) {
        free(src);
        return 0;
    }
    if (!resolve_world_bindings(reg)) {
        free(src);
        return 0;
    }
    if (!resolve_registry_symbol_scopes(reg)) {
        free(src);
        return 0;
    }
    finalize_package_info(reg, wit_path);
    free(src);
    return 1;
}

static int parse_scope_items(Scanner *s,
                             WitRegistry *reg,
                             ParseScope scope,
                             const char *scope_name,
                             const char *interface_name)
{
    char metadata[MAX_METADATA];

    while (!scanner_eof(s)) {
        consume_leading_metadata(s, metadata, (int)sizeof(metadata));
        skip_whitespace(s);
        if (scanner_eof(s)) break;
        if (scope != PARSE_SCOPE_TOP && scanner_peek(s) == '}') {
            scanner_advance(s);
            return 1;
        }

        if (scope == PARSE_SCOPE_RESOURCE
                && try_parse_constructor_func(s, reg, scope_name, interface_name, metadata)) {
            continue;
        } else if ((scope == PARSE_SCOPE_INTERFACE || scope == PARSE_SCOPE_RESOURCE)
                && try_parse_named_func(s, reg, scope, scope_name, interface_name, metadata)) {
            continue;
        } else if (match_keyword(s, "package")) {
            if (!parse_package_decl(s, reg)) return 0;
        } else if (match_keyword(s, "use")) {
            if (!parse_use_decl(s, reg, interface_name)) return 0;
        } else if (match_keyword(s, "world")) {
            if (!parse_world_decl(s, reg, metadata)) return 0;
        } else if (match_keyword(s, "interface")) {
            if (!parse_interface_decl(s, reg, metadata)) return 0;
        } else if (match_keyword(s, "export") || match_keyword(s, "import")) {
            while (!scanner_eof(s) && scanner_peek(s) != '{' && scanner_peek(s) != ';')
                scanner_advance(s);
            if (match_char(s, ';')) {
                continue;
            }
            skip_braced_block(s);
        } else if (match_keyword(s, "record")) {
            if (reg->record_count >= MAX_TYPES) {
                fprintf(stderr, "wit_codegen: too many records\n"); return 0;
            }
            if (!parse_record(s, &reg->records[reg->record_count])) return 0;
            snprintf(reg->records[reg->record_count].package_full,
                     sizeof(reg->records[reg->record_count].package_full),
                     "%s",
                     reg->package_full);
            if (metadata[0] != '\0') {
                snprintf(reg->records[reg->record_count].metadata,
                         sizeof(reg->records[reg->record_count].metadata),
                         "%s",
                         metadata);
            }
            snprintf(reg->records[reg->record_count].interface_name,
                     sizeof(reg->records[reg->record_count].interface_name),
                     "%s",
                     interface_name ? interface_name : "");
            wit_build_scoped_symbol_name(reg->package_full,
                                         interface_name,
                                         reg->records[reg->record_count].name,
                                         reg->records[reg->record_count].symbol_name,
                                         (int)sizeof(reg->records[reg->record_count].symbol_name));
            reg->record_count++;
        } else if (match_keyword(s, "variant")) {
            if (reg->variant_count >= MAX_TYPES) {
                fprintf(stderr, "wit_codegen: too many variants\n"); return 0;
            }
            if (!parse_variant(s, &reg->variants[reg->variant_count])) return 0;
            snprintf(reg->variants[reg->variant_count].package_full,
                     sizeof(reg->variants[reg->variant_count].package_full),
                     "%s",
                     reg->package_full);
            if (metadata[0] != '\0') {
                snprintf(reg->variants[reg->variant_count].metadata,
                         sizeof(reg->variants[reg->variant_count].metadata),
                         "%s",
                         metadata);
            }
            snprintf(reg->variants[reg->variant_count].interface_name,
                     sizeof(reg->variants[reg->variant_count].interface_name),
                     "%s",
                     interface_name ? interface_name : "");
            wit_build_scoped_symbol_name(reg->package_full,
                                         interface_name,
                                         reg->variants[reg->variant_count].name,
                                         reg->variants[reg->variant_count].symbol_name,
                                         (int)sizeof(reg->variants[reg->variant_count].symbol_name));
            reg->variant_count++;
        } else if (match_keyword(s, "enum")) {
            if (reg->enum_count >= MAX_TYPES) {
                fprintf(stderr, "wit_codegen: too many enums\n"); return 0;
            }
            if (!parse_enum(s, &reg->enums[reg->enum_count])) return 0;
            snprintf(reg->enums[reg->enum_count].package_full,
                     sizeof(reg->enums[reg->enum_count].package_full),
                     "%s",
                     reg->package_full);
            if (metadata[0] != '\0') {
                snprintf(reg->enums[reg->enum_count].metadata,
                         sizeof(reg->enums[reg->enum_count].metadata),
                         "%s",
                         metadata);
            }
            snprintf(reg->enums[reg->enum_count].interface_name,
                     sizeof(reg->enums[reg->enum_count].interface_name),
                     "%s",
                     interface_name ? interface_name : "");
            wit_build_scoped_symbol_name(reg->package_full,
                                         interface_name,
                                         reg->enums[reg->enum_count].name,
                                         reg->enums[reg->enum_count].symbol_name,
                                         (int)sizeof(reg->enums[reg->enum_count].symbol_name));
            reg->enum_count++;
        } else if (match_keyword(s, "flags")) {
            if (reg->flags_count >= MAX_TYPES) {
                fprintf(stderr, "wit_codegen: too many flags\n"); return 0;
            }
            if (!parse_flags(s, &reg->flags[reg->flags_count])) return 0;
            snprintf(reg->flags[reg->flags_count].package_full,
                     sizeof(reg->flags[reg->flags_count].package_full),
                     "%s",
                     reg->package_full);
            if (metadata[0] != '\0') {
                snprintf(reg->flags[reg->flags_count].metadata,
                         sizeof(reg->flags[reg->flags_count].metadata),
                         "%s",
                         metadata);
            }
            snprintf(reg->flags[reg->flags_count].interface_name,
                     sizeof(reg->flags[reg->flags_count].interface_name),
                     "%s",
                     interface_name ? interface_name : "");
            wit_build_scoped_symbol_name(reg->package_full,
                                         interface_name,
                                         reg->flags[reg->flags_count].name,
                                         reg->flags[reg->flags_count].symbol_name,
                                         (int)sizeof(reg->flags[reg->flags_count].symbol_name));
            reg->flags_count++;
        } else if (match_keyword(s, "type")) {
            if (reg->alias_count >= MAX_TYPES) {
                fprintf(stderr, "wit_codegen: too many aliases\n"); return 0;
            }
            if (!parse_alias(s, &reg->aliases[reg->alias_count])) return 0;
            snprintf(reg->aliases[reg->alias_count].package_full,
                     sizeof(reg->aliases[reg->alias_count].package_full),
                     "%s",
                     reg->package_full);
            if (metadata[0] != '\0') {
                snprintf(reg->aliases[reg->alias_count].metadata,
                         sizeof(reg->aliases[reg->alias_count].metadata),
                         "%s",
                         metadata);
            }
            snprintf(reg->aliases[reg->alias_count].interface_name,
                     sizeof(reg->aliases[reg->alias_count].interface_name),
                     "%s",
                     interface_name ? interface_name : "");
            wit_build_scoped_symbol_name(reg->package_full,
                                         interface_name,
                                         reg->aliases[reg->alias_count].name,
                                         reg->aliases[reg->alias_count].symbol_name,
                                         (int)sizeof(reg->aliases[reg->alias_count].symbol_name));
            reg->alias_count++;
        } else if (match_keyword(s, "resource")) {
            if (!parse_resource_decl(s, reg, interface_name, metadata)) return 0;
        } else {
            scanner_advance(s);
        }
    }
    return scope == PARSE_SCOPE_TOP;
}

static int parse_wit(Scanner *s, WitRegistry *reg)
{
    return parse_scope_items(s, reg, PARSE_SCOPE_TOP, NULL, NULL);
}

static int scan_source_for_package_decl(const char *src,
                                        int len,
                                        char *out,
                                        int n)
{
    Scanner s;

    if (!src || len < 0 || !out || n <= 0) return 0;
    out[0] = '\0';

    scanner_init(&s, src, len);
    while (!scanner_eof(&s)) {
        skip_whitespace(&s);
        if (scanner_eof(&s)) {
            break;
        }
        if (match_keyword(&s, "package")) {
            int i = 0;

            skip_whitespace(&s);
            while (!scanner_eof(&s) && scanner_peek(&s) != ';' && i < n - 1) {
                out[i++] = scanner_advance(&s);
            }
            out[i] = '\0';
            wit_trim(out);
            return out[0] != '\0';
        }
        scanner_advance(&s);
    }

    return 0;
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
static int is_primitive(const char *name);
static int resolve_use_bindings(WitRegistry *reg);
static int resolve_registry_symbol_scopes(WitRegistry *reg);

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

typedef struct {
    const WitFunc *func;
    int            needs_request_record;
    int            passthrough_record_payload;
    char           command_case_name[MAX_NAME];
    char           request_record_name[MAX_NAME];
    char           reply_base_name[MAX_NAME];
    char           reply_case_name[MAX_NAME];
} LoweredFuncPlan;

typedef enum {
    LOWERED_NAME_COMMAND,
    LOWERED_NAME_REPLY,
    LOWERED_NAME_REQUEST_RECORD,
} LoweredNameKind;

static const char *lowered_name_kind_label(LoweredNameKind kind)
{
    switch (kind) {
    case LOWERED_NAME_COMMAND:
        return "command";
    case LOWERED_NAME_REPLY:
        return "reply";
    case LOWERED_NAME_REQUEST_RECORD:
        return "request";
    }
    return "unknown";
}

static void lowered_join_names(const char *left,
                               const char *right,
                               char *out,
                               int n)
{
    if (!out || n <= 0) return;
    out[0] = '\0';

    if (left && left[0] != '\0' && right && right[0] != '\0') {
        snprintf(out, n, "%s-%s", left, right);
    } else if (right && right[0] != '\0') {
        snprintf(out, n, "%s", right);
    } else if (left && left[0] != '\0') {
        snprintf(out, n, "%s", left);
    }
}

static void lowered_command_name(const char *group_name,
                                 char *out,
                                 int n)
{
    snprintf(out, n, "%s-command", group_name);
}

static void lowered_reply_name(const char *group_name,
                               char *out,
                               int n)
{
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

static int lowered_type_case_name_hint(const WitRegistry *reg,
                                       int type_idx,
                                       char *out,
                                       int n)
{
    int resolved;
    WitTypeExpr *type_expr;
    const WitResource *resource;
    const WitRecord *record;
    const WitVariant *variant;
    const WitEnum *en;
    const WitFlags *fl;
    const WitAlias *alias;

    if (!reg || !out || n <= 0) return 0;
    out[0] = '\0';

    resolved = unwrap_borrow_type(reg, type_idx);
    if (resolved < 0) return 0;
    type_expr = &g_type_pool[resolved];
    if (type_expr->kind != TYPE_IDENT) return 0;

    resource = find_resource(reg, type_expr->ident);
    if (resource) {
        snprintf(out, n, "%s", resource->name);
        return 1;
    }

    record = find_record(reg, type_expr->ident);
    if (record) {
        snprintf(out, n, "%s", record->name);
        return 1;
    }

    variant = find_variant(reg, type_expr->ident);
    if (variant) {
        snprintf(out, n, "%s", variant->name);
        return 1;
    }

    en = find_enum(reg, type_expr->ident);
    if (en) {
        snprintf(out, n, "%s", en->name);
        return 1;
    }

    fl = find_flags(reg, type_expr->ident);
    if (fl) {
        snprintf(out, n, "%s", fl->name);
        return 1;
    }

    alias = find_alias(reg, type_expr->ident);
    if (alias) {
        snprintf(out, n, "%s", alias->name);
        return 1;
    }

    return 0;
}

static void lowered_command_case_candidate(const WitFunc *func,
                                           int level,
                                           char *out,
                                           int n)
{
    if (!out || n <= 0) return;
    out[0] = '\0';
    if (!func) return;

    switch (level) {
    case 0:
        snprintf(out, n, "%s", func->name);
        return;
    case 1:
        if (func->owner_kind == WIT_OWNER_RESOURCE && func->owner_name[0] != '\0') {
            lowered_join_names(func->owner_name, func->name, out, n);
        } else if (func->interface_name[0] != '\0') {
            lowered_join_names(func->interface_name, func->name, out, n);
        } else {
            snprintf(out, n, "%s", func->name);
        }
        return;
    default:
        if (func->interface_name[0] != '\0'
                && func->owner_kind == WIT_OWNER_RESOURCE
                && func->owner_name[0] != '\0') {
            char qualified[MAX_NAME];

            lowered_join_names(func->owner_name, func->name, qualified, (int)sizeof(qualified));
            lowered_join_names(func->interface_name, qualified, out, n);
        } else if (func->interface_name[0] != '\0') {
            lowered_join_names(func->interface_name, func->name, out, n);
        } else {
            snprintf(out, n, "%s", func->name);
        }
        return;
    }
}

static void lowered_request_record_candidate(const WitFunc *func,
                                             int level,
                                             char *out,
                                             int n)
{
    char base[MAX_NAME];

    if (!out || n <= 0) return;
    out[0] = '\0';
    if (!func) return;

    lowered_request_record_name(func, base, (int)sizeof(base));
    switch (level) {
    case 0:
        snprintf(out, n, "%s", base);
        return;
    case 1:
        lowered_join_names(base, "request", out, n);
        return;
    default:
        if (func->owner_kind == WIT_OWNER_RESOURCE && func->owner_name[0] != '\0') {
            char qualified[MAX_NAME];

            lowered_join_names(func->owner_name, func->name, qualified, (int)sizeof(qualified));
            lowered_join_names(qualified, "request", out, n);
        } else if (func->interface_name[0] != '\0') {
            char qualified[MAX_NAME];

            lowered_join_names(func->interface_name, func->name, qualified, (int)sizeof(qualified));
            lowered_join_names(qualified, "request", out, n);
        } else {
            lowered_join_names(base, "request", out, n);
        }
        return;
    }
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

static const char *wit_func_surface_name(const WitRegistry *reg, const WitFunc *func)
{
    const WitResource *resource;

    if (!func) return "";
    if (func->interface_name[0] != '\0') {
        return func->interface_name;
    }
    if (func->owner_kind == WIT_OWNER_RESOURCE) {
        resource = find_resource(reg,
                                 func->owner_symbol_name[0] != '\0'
                                     ? func->owner_symbol_name
                                     : func->owner_name);
        if (resource && resource->interface_name[0] != '\0') {
            return resource->interface_name;
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
        if (lowered_type_case_name_hint(reg, resolved, out, n)) {
            return;
        }
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
        if (ok_expr->kind == TYPE_IDENT) {
            const WitResource *resource = find_resource(reg, ok_expr->ident);
            if (resource) {
                snprintf(out, n, "%s", resource->name);
                return;
            }
        }
    }

    if (alias_name && alias_result_case_name(reg, alias_name, out, n)) {
        return;
    }

    snprintf(out, n, "%s", func->name);
}

static void lowered_reply_case_candidate(const WitFunc *func,
                                         const char *base_name,
                                         int level,
                                         char *out,
                                         int n)
{
    if (!out || n <= 0) return;
    out[0] = '\0';
    if (!func) return;

    switch (level) {
    case 0:
        snprintf(out, n, "%s", base_name && base_name[0] != '\0' ? base_name : func->name);
        return;
    case 1:
        if (func->owner_kind == WIT_OWNER_RESOURCE && func->owner_name[0] != '\0') {
            lowered_join_names(func->owner_name,
                               base_name && base_name[0] != '\0' ? base_name : func->name,
                               out,
                               n);
        } else {
            snprintf(out, n, "%s", func->name);
        }
        return;
    default:
        if (func->owner_kind == WIT_OWNER_RESOURCE && func->owner_name[0] != '\0') {
            lowered_join_names(func->owner_name, func->name, out, n);
        } else if (func->interface_name[0] != '\0') {
            lowered_join_names(func->interface_name, func->name, out, n);
        } else {
            snprintf(out, n, "%s", func->name);
        }
        return;
    }
}

static int lowered_names_conflict(const WitRegistry *reg,
                                  const LoweredFuncPlan *plans,
                                  int plan_count,
                                  int a,
                                  int b,
                                  LoweredNameKind kind)
{
    if (!plans || a < 0 || b < 0 || a >= plan_count || b >= plan_count) return 0;

    switch (kind) {
    case LOWERED_NAME_COMMAND:
        return strcmp(plans[a].command_case_name, plans[b].command_case_name) == 0;
    case LOWERED_NAME_REPLY:
        if (strcmp(plans[a].reply_case_name, plans[b].reply_case_name) != 0) {
            return 0;
        }
        return !same_type_shape(reg,
                                plans[a].func->result_type,
                                plans[b].func->result_type);
    case LOWERED_NAME_REQUEST_RECORD:
        if (!plans[a].needs_request_record || !plans[b].needs_request_record) {
            return 0;
        }
        return strcmp(plans[a].request_record_name, plans[b].request_record_name) == 0;
    }

    return 0;
}

static int lowered_request_name_conflicts_existing(const WitRegistry *reg,
                                                   const char *name)
{
    if (!reg || !name || name[0] == '\0') return 0;
    return find_record(reg, name) || find_variant(reg, name)
        || find_enum(reg, name) || find_flags(reg, name)
        || find_alias(reg, name) || find_resource(reg, name);
}

static int assign_lowered_plan_names(const WitRegistry *reg,
                                     LoweredFuncPlan *plans,
                                     int plan_count,
                                     LoweredNameKind kind)
{
    int levels[MAX_FUNCS];
    int bump_levels[MAX_FUNCS];
    int max_level = 0;
    int changed;

    if (!reg || !plans || plan_count < 0 || plan_count > MAX_FUNCS) {
        return 0;
    }

    switch (kind) {
    case LOWERED_NAME_COMMAND:
        max_level = 2;
        break;
    case LOWERED_NAME_REPLY:
        max_level = 2;
        break;
    case LOWERED_NAME_REQUEST_RECORD:
        max_level = 2;
        break;
    }

    memset(levels, 0, sizeof(levels));
    do {
        changed = 0;
        memset(bump_levels, 0, sizeof(bump_levels));
        for (int i = 0; i < plan_count; i++) {
            switch (kind) {
            case LOWERED_NAME_COMMAND:
                lowered_command_case_candidate(plans[i].func,
                                               levels[i],
                                               plans[i].command_case_name,
                                               (int)sizeof(plans[i].command_case_name));
                break;
            case LOWERED_NAME_REPLY:
                lowered_reply_case_candidate(plans[i].func,
                                             plans[i].reply_base_name,
                                             levels[i],
                                             plans[i].reply_case_name,
                                             (int)sizeof(plans[i].reply_case_name));
                break;
            case LOWERED_NAME_REQUEST_RECORD:
                if (!plans[i].needs_request_record) {
                    plans[i].request_record_name[0] = '\0';
                    break;
                }
                lowered_request_record_candidate(plans[i].func,
                                                 levels[i],
                                                 plans[i].request_record_name,
                                                 (int)sizeof(plans[i].request_record_name));
                break;
            }
        }

        if (kind == LOWERED_NAME_REQUEST_RECORD) {
            for (int i = 0; i < plan_count; i++) {
                if (!plans[i].needs_request_record) continue;
                if (lowered_request_name_conflicts_existing(reg, plans[i].request_record_name)) {
                    bump_levels[i] = 1;
                }
            }
        }

        for (int i = 0; i < plan_count; i++) {
            for (int j = i + 1; j < plan_count; j++) {
                if (!lowered_names_conflict(reg, plans, plan_count, i, j, kind)) {
                    continue;
                }
                bump_levels[i] = 1;
                bump_levels[j] = 1;
            }
        }

        for (int i = 0; i < plan_count; i++) {
            if (!bump_levels[i]) continue;
            if (levels[i] >= max_level) {
                const char *candidate = "";

                switch (kind) {
                case LOWERED_NAME_COMMAND:
                    candidate = plans[i].command_case_name;
                    break;
                case LOWERED_NAME_REPLY:
                    candidate = plans[i].reply_case_name;
                    break;
                case LOWERED_NAME_REQUEST_RECORD:
                    candidate = plans[i].request_record_name;
                    break;
                }
                fprintf(stderr,
                        "wit_codegen: unable to disambiguate lowered %s name '%s' for %s\n",
                        lowered_name_kind_label(kind),
                        candidate,
                        plans[i].func ? plans[i].func->trace : "(unknown)");
                return 0;
            }
            levels[i]++;
            changed = 1;
        }
    } while (changed);

    return 1;
}

static int build_lowered_group_plans(const WitRegistry *reg,
                                     const char *group_name,
                                     LoweredFuncPlan *plans,
                                     int *plan_count_out)
{
    int plan_count = 0;

    if (!reg || !group_name || !plans || !plan_count_out) return 0;

    for (int i = 0; i < reg->func_count; i++) {
        const WitFunc *func = &reg->funcs[i];
        LoweredFuncPlan *plan;

        if (strcmp(wit_func_surface_name(reg, func), group_name) != 0) {
            continue;
        }
        if (plan_count >= MAX_FUNCS) {
            fprintf(stderr, "wit_codegen: too many lowered operations in %s\n", group_name);
            return 0;
        }

        plan = &plans[plan_count++];
        memset(plan, 0, sizeof(*plan));
        plan->func = func;
        plan->passthrough_record_payload = can_passthrough_record_payload(reg, func);
        plan->needs_request_record = ((func->param_count > 0 || func->kind == WIT_FUNC_METHOD)
                                      && !plan->passthrough_record_payload);
        lowered_reply_case_name(reg,
                                func,
                                plan->reply_base_name,
                                (int)sizeof(plan->reply_base_name));
    }

    if (!assign_lowered_plan_names(reg, plans, plan_count, LOWERED_NAME_COMMAND)) {
        fprintf(stderr, "wit_codegen: unable to resolve lowered command names in %s\n", group_name);
        return 0;
    }
    if (!assign_lowered_plan_names(reg, plans, plan_count, LOWERED_NAME_REPLY)) {
        fprintf(stderr, "wit_codegen: unable to resolve lowered reply names in %s\n", group_name);
        return 0;
    }
    if (!assign_lowered_plan_names(reg, plans, plan_count, LOWERED_NAME_REQUEST_RECORD)) {
        fprintf(stderr, "wit_codegen: unable to resolve lowered request names in %s\n", group_name);
        return 0;
    }

    *plan_count_out = plan_count;
    return 1;
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
    LoweredFuncPlan plans[MAX_FUNCS];
    char command_name[MAX_NAME];
    char reply_name[MAX_NAME];
    const char *surface_name = NULL;
    const char *surface_package_full = NULL;
    int plan_count = 0;

    memset(&command_variant, 0, sizeof(command_variant));
    memset(&reply_variant, 0, sizeof(reply_variant));
    lowered_command_name(group_name, command_name, (int)sizeof(command_name));
    lowered_reply_name(group_name, reply_name, (int)sizeof(reply_name));
    snprintf(command_variant.name, sizeof(command_variant.name), "%s", command_name);
    snprintf(reply_variant.name, sizeof(reply_variant.name), "%s", reply_name);

    if (!build_lowered_group_plans(reg, group_name, plans, &plan_count)) {
        return 0;
    }
    if (plan_count > 0) {
        surface_name = wit_func_surface_name(reg, plans[0].func);
        surface_package_full = plans[0].func->package_full;
        snprintf(command_variant.package_full,
                 sizeof(command_variant.package_full),
                 "%s",
                 surface_package_full ? surface_package_full : "");
        snprintf(command_variant.interface_name,
                 sizeof(command_variant.interface_name),
                 "%s",
                 surface_name ? surface_name : "");
        wit_build_scoped_symbol_name(surface_package_full,
                                     surface_name,
                                     command_variant.name,
                                     command_variant.symbol_name,
                                     (int)sizeof(command_variant.symbol_name));
        snprintf(reply_variant.package_full,
                 sizeof(reply_variant.package_full),
                 "%s",
                 surface_package_full ? surface_package_full : "");
        snprintf(reply_variant.interface_name,
                 sizeof(reply_variant.interface_name),
                 "%s",
                 surface_name ? surface_name : "");
        wit_build_scoped_symbol_name(surface_package_full,
                                     surface_name,
                                     reply_variant.name,
                                     reply_variant.symbol_name,
                                     (int)sizeof(reply_variant.symbol_name));
    }

    for (int i = 0; i < plan_count; i++) {
        const LoweredFuncPlan *plan = &plans[i];
        const WitFunc *func = plan->func;
        WitRecord request_record;

        memset(&request_record, 0, sizeof(request_record));
        if (command_variant.case_count >= MAX_CASES) {
            fprintf(stderr, "wit_codegen: too many lowered command cases in %s\n", group_name);
            return 0;
        }

        if (plan->needs_request_record) {
            int field_count = 0;

            snprintf(request_record.name,
                     sizeof(request_record.name),
                     "%s",
                     plan->request_record_name);
            snprintf(request_record.package_full,
                     sizeof(request_record.package_full),
                     "%s",
                     func->package_full);
            snprintf(request_record.interface_name,
                     sizeof(request_record.interface_name),
                     "%s",
                     surface_name ? surface_name : wit_func_surface_name(reg, func));
            wit_build_scoped_symbol_name(func->package_full,
                                         surface_name ? surface_name : wit_func_surface_name(reg, func),
                                         request_record.name,
                                         request_record.symbol_name,
                                         (int)sizeof(request_record.symbol_name));

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
                         func->owner_symbol_name[0] != '\0'
                             ? func->owner_symbol_name
                             : func->owner_name);
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
                 plan->command_case_name);
        snprintf(command_variant.cases[command_variant.case_count].trace,
                 sizeof(command_variant.cases[command_variant.case_count].trace),
                 "%s",
                 func->trace);
        if (plan->needs_request_record) {
            int type_idx = type_alloc();
            if (type_idx < 0) {
                return 0;
            }
            g_type_pool[type_idx].kind = TYPE_IDENT;
            snprintf(g_type_pool[type_idx].ident,
                     sizeof(g_type_pool[type_idx].ident),
                     "%s",
                     plan->request_record_name);
            command_variant.cases[command_variant.case_count].payload_type = type_idx;
        } else if (plan->passthrough_record_payload) {
            command_variant.cases[command_variant.case_count].payload_type = func->params[0].wit_type;
        } else {
            command_variant.cases[command_variant.case_count].payload_type = -1;
        }
        command_variant.case_count++;

        for (int j = 0; j < reply_variant.case_count; j++) {
            if (strcmp(reply_variant.cases[j].name, plan->reply_case_name) == 0) {
                if (!same_type_shape(reg, reply_variant.cases[j].payload_type, func->result_type)) {
                    fprintf(stderr,
                            "wit_codegen: lowered reply case conflict in %s for case %s\n",
                            group_name,
                            plan->reply_case_name);
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
                 plan->reply_case_name);
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
            if (strcmp(wit_func_surface_name(reg, &reg->funcs[i]),
                       wit_func_surface_name(reg, &reg->funcs[j])) == 0) {
                seen = 1;
                break;
            }
        }
        if (seen) {
            continue;
        }
        if (!lower_operation_group(reg, wit_func_surface_name(reg, &reg->funcs[i]))) {
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
    const WitAlias *local_match = NULL;
    int local_count = 0;

    for (int i = 0; i < reg->alias_count; i++)
        if (reg->aliases[i].symbol_name[0] != '\0' && strcmp(reg->aliases[i].symbol_name, name) == 0)
            return &reg->aliases[i];
    for (int i = 0; i < reg->alias_count; i++)
        if (strcmp(reg->aliases[i].name, name) == 0) {
            local_match = &reg->aliases[i];
            local_count++;
        }
    return (local_count == 1) ? local_match : NULL;
}

static const WitRecord *find_record(const WitRegistry *reg, const char *name)
{
    const WitRecord *local_match = NULL;
    int local_count = 0;

    for (int i = 0; i < reg->record_count; i++)
        if (reg->records[i].symbol_name[0] != '\0' && strcmp(reg->records[i].symbol_name, name) == 0)
            return &reg->records[i];
    for (int i = 0; i < reg->record_count; i++)
        if (strcmp(reg->records[i].name, name) == 0) {
            local_match = &reg->records[i];
            local_count++;
        }
    return (local_count == 1) ? local_match : NULL;
}

static const WitVariant *find_variant(const WitRegistry *reg, const char *name)
{
    const WitVariant *local_match = NULL;
    int local_count = 0;

    for (int i = 0; i < reg->variant_count; i++)
        if (reg->variants[i].symbol_name[0] != '\0' && strcmp(reg->variants[i].symbol_name, name) == 0)
            return &reg->variants[i];
    for (int i = 0; i < reg->variant_count; i++)
        if (strcmp(reg->variants[i].name, name) == 0) {
            local_match = &reg->variants[i];
            local_count++;
        }
    return (local_count == 1) ? local_match : NULL;
}

static const WitEnum *find_enum(const WitRegistry *reg, const char *name)
{
    const WitEnum *local_match = NULL;
    int local_count = 0;

    for (int i = 0; i < reg->enum_count; i++)
        if (reg->enums[i].symbol_name[0] != '\0' && strcmp(reg->enums[i].symbol_name, name) == 0)
            return &reg->enums[i];
    for (int i = 0; i < reg->enum_count; i++)
        if (strcmp(reg->enums[i].name, name) == 0) {
            local_match = &reg->enums[i];
            local_count++;
        }
    return (local_count == 1) ? local_match : NULL;
}

static const WitFlags *find_flags(const WitRegistry *reg, const char *name)
{
    const WitFlags *local_match = NULL;
    int local_count = 0;

    for (int i = 0; i < reg->flags_count; i++)
        if (reg->flags[i].symbol_name[0] != '\0' && strcmp(reg->flags[i].symbol_name, name) == 0)
            return &reg->flags[i];
    for (int i = 0; i < reg->flags_count; i++)
        if (strcmp(reg->flags[i].name, name) == 0) {
            local_match = &reg->flags[i];
            local_count++;
        }
    return (local_count == 1) ? local_match : NULL;
}

static const WitResource *find_resource(const WitRegistry *reg, const char *name)
{
    const WitResource *local_match = NULL;
    int local_count = 0;

    for (int i = 0; i < reg->resource_count; i++)
        if (reg->resources[i].symbol_name[0] != '\0' && strcmp(reg->resources[i].symbol_name, name) == 0)
            return &reg->resources[i];
    for (int i = 0; i < reg->resource_count; i++)
        if (strcmp(reg->resources[i].name, name) == 0) {
            local_match = &reg->resources[i];
            local_count++;
        }
    return (local_count == 1) ? local_match : NULL;
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

static int is_builtin_ident(const char *name)
{
    return name && (strcmp(name, "string") == 0 || is_primitive(name));
}

static int lookup_decl_symbol_name(const WitRegistry *reg,
                                   const char *package_full,
                                   const char *interface_name,
                                   const char *local_name,
                                   char *out,
                                   int n)
{
    const char *pkg = package_full ? package_full : "";
    const char *iface = interface_name ? interface_name : "";
    const char *match = NULL;

    if (!reg || !local_name || local_name[0] == '\0') return 0;

#define CHECK_DECL_SCOPE(sym_package, sym_interface, sym_local, sym_name) \
    do { \
        if (strcmp((sym_package), pkg) == 0 \
                && strcmp((sym_interface), iface) == 0 \
                && strcmp((sym_local), local_name) == 0) { \
            if (match && strcmp(match, (sym_name)) != 0) { \
                codegen_die("ambiguous WIT name %s in %s/%s", \
                            local_name, \
                            pkg[0] ? pkg : "<none>", \
                            iface[0] ? iface : "<top-level>"); \
            } \
            match = (sym_name); \
        } \
    } while (0)

    for (int i = 0; i < reg->alias_count; i++)
        CHECK_DECL_SCOPE(reg->aliases[i].package_full, reg->aliases[i].interface_name,
                         reg->aliases[i].name, reg->aliases[i].symbol_name);
    for (int i = 0; i < reg->record_count; i++)
        CHECK_DECL_SCOPE(reg->records[i].package_full, reg->records[i].interface_name,
                         reg->records[i].name, reg->records[i].symbol_name);
    for (int i = 0; i < reg->variant_count; i++)
        CHECK_DECL_SCOPE(reg->variants[i].package_full, reg->variants[i].interface_name,
                         reg->variants[i].name, reg->variants[i].symbol_name);
    for (int i = 0; i < reg->enum_count; i++)
        CHECK_DECL_SCOPE(reg->enums[i].package_full, reg->enums[i].interface_name,
                         reg->enums[i].name, reg->enums[i].symbol_name);
    for (int i = 0; i < reg->flags_count; i++)
        CHECK_DECL_SCOPE(reg->flags[i].package_full, reg->flags[i].interface_name,
                         reg->flags[i].name, reg->flags[i].symbol_name);
    for (int i = 0; i < reg->resource_count; i++)
        CHECK_DECL_SCOPE(reg->resources[i].package_full, reg->resources[i].interface_name,
                         reg->resources[i].name, reg->resources[i].symbol_name);

#undef CHECK_DECL_SCOPE

    if (!match) return 0;
    if (out && n > 0) {
        snprintf(out, n, "%s", match);
    }
    return 1;
}

static int lookup_scoped_symbol_name(const WitRegistry *reg,
                                     const char *package_full,
                                     const char *interface_name,
                                     const char *local_name,
                                     char *out,
                                     int n)
{
    const char *pkg = package_full ? package_full : "";
    const char *iface = interface_name ? interface_name : "";

    if (!reg || !local_name || local_name[0] == '\0') return 0;

    if (lookup_decl_symbol_name(reg, pkg, iface, local_name, out, n)) {
        return 1;
    }

    if (iface[0] != '\0') {
        for (int i = 0; i < reg->use_binding_count; i++) {
            if (strcmp(reg->use_bindings[i].package_full, pkg) == 0
                    && strcmp(reg->use_bindings[i].interface_name, iface) == 0
                    && strcmp(reg->use_bindings[i].local_name, local_name) == 0) {
                if (reg->use_bindings[i].target_symbol_name[0] == '\0') {
                    codegen_die("unresolved use binding for %s in %s/%s",
                                local_name,
                                pkg,
                                iface);
                }
                if (out && n > 0) {
                    snprintf(out, n, "%s", reg->use_bindings[i].target_symbol_name);
                }
                return 1;
            }
        }
        return lookup_decl_symbol_name(reg, pkg, "", local_name, out, n);
    }
    return 0;
}

static int qualify_type_expr_symbols(const WitRegistry *reg,
                                     int type_idx,
                                     const char *package_full,
                                     const char *interface_name)
{
    WitTypeExpr *t;

    if (!reg || type_idx < 0) return 1;
    t = &g_type_pool[type_idx];

    if (t->kind == TYPE_IDENT) {
        char symbol_name[MAX_SYMBOL];

        if (t->ident[0] == '\0' || is_builtin_ident(t->ident) || wit_symbol_has_scope(t->ident)) {
            return 1;
        }
        if (lookup_scoped_symbol_name(reg,
                                      package_full,
                                      interface_name,
                                      t->ident,
                                      symbol_name,
                                      (int)sizeof(symbol_name))) {
            snprintf(t->ident, sizeof(t->ident), "%s", symbol_name);
        }
        return 1;
    }

    for (int i = 0; i < t->param_count; i++) {
        if (t->params[i] >= 0
                && !qualify_type_expr_symbols(reg, t->params[i], package_full, interface_name)) {
            return 0;
        }
    }
    return 1;
}

static int lookup_resolved_use_binding_symbol_name(const WitRegistry *reg,
                                                   const char *package_full,
                                                   const char *interface_name,
                                                   const char *local_name,
                                                   char *out,
                                                   int n)
{
    if (!reg || !local_name || local_name[0] == '\0') return 0;

    for (int i = 0; i < reg->use_binding_count; i++) {
        if (strcmp(reg->use_bindings[i].package_full, package_full ? package_full : "") == 0
                && strcmp(reg->use_bindings[i].interface_name, interface_name ? interface_name : "") == 0
                && strcmp(reg->use_bindings[i].local_name, local_name) == 0
                && reg->use_bindings[i].target_symbol_name[0] != '\0') {
            if (out && n > 0) {
                snprintf(out, n, "%s", reg->use_bindings[i].target_symbol_name);
            }
            return 1;
        }
    }

    return 0;
}

static int resolve_use_bindings(WitRegistry *reg)
{
    char symbol_name[MAX_SYMBOL];
    int unresolved = 0;
    int progress = 0;

    if (!reg) return 0;

    for (int pass = 0; pass <= reg->use_binding_count; pass++) {
        unresolved = 0;
        progress = 0;

        for (int i = 0; i < reg->use_binding_count; i++) {
            if (reg->use_bindings[i].target_symbol_name[0] != '\0') {
                continue;
            }
            if (!registry_has_interface_symbols(reg,
                                                reg->use_bindings[i].target_package_full,
                                                reg->use_bindings[i].target_interface_name)) {
                if (!ensure_import_interface_loaded(reg,
                                                    reg->use_bindings[i].target_package_full,
                                                    reg->use_bindings[i].target_interface_name)) {
                    return 0;
                }
            }
            if (!lookup_decl_symbol_name(reg,
                                         reg->use_bindings[i].target_package_full,
                                         reg->use_bindings[i].target_interface_name,
                                         reg->use_bindings[i].target_name,
                                         symbol_name,
                                         (int)sizeof(symbol_name))
                    && !lookup_resolved_use_binding_symbol_name(reg,
                                                                reg->use_bindings[i].target_package_full,
                                                                reg->use_bindings[i].target_interface_name,
                                                                reg->use_bindings[i].target_name,
                                                                symbol_name,
                                                                (int)sizeof(symbol_name))) {
                unresolved++;
                continue;
            }
            snprintf(reg->use_bindings[i].target_symbol_name,
                     sizeof(reg->use_bindings[i].target_symbol_name),
                     "%s",
                     symbol_name);
            progress = 1;
        }

        if (unresolved == 0) {
            return 1;
        }
        if (!progress) {
            break;
        }
    }

    for (int i = 0; i < reg->use_binding_count; i++) {
        if (reg->use_bindings[i].target_symbol_name[0] == '\0') {
            codegen_die("unknown imported WIT name %s from %s/%s in %s/%s",
                        reg->use_bindings[i].target_name,
                        reg->use_bindings[i].target_package_full,
                        reg->use_bindings[i].target_interface_name,
                        reg->use_bindings[i].package_full,
                        reg->use_bindings[i].interface_name);
        }
    }

    return 1;
}

static const char *wit_world_item_effective_target_package(const WitRegistry *reg,
                                                           const WitWorldItem *item)
{
    if (!item) return "";
    if (item->lowered_target_package_full[0] != '\0') {
        return item->lowered_target_package_full;
    }
    if (item->target_package_full[0] != '\0') {
        return item->target_package_full;
    }
    return reg ? reg->package_full : "";
}

static const char *wit_world_item_effective_target_name(const WitWorldItem *item)
{
    if (!item) return "";
    if (item->lowered_target_name[0] != '\0') {
        return item->lowered_target_name;
    }
    return item->target_name;
}

static int resolve_world_bindings(WitRegistry *reg)
{
    if (!reg) return 0;

    for (int i = 0; i < reg->world_item_count; i++) {
        const char *target_pkg = reg->world_items[i].target_package_full[0] != '\0'
            ? reg->world_items[i].target_package_full
            : reg->package_full;
        const char *effective_pkg = wit_world_item_effective_target_package(reg, &reg->world_items[i]);
        const char *effective_name = wit_world_item_effective_target_name(&reg->world_items[i]);

        if (reg->world_items[i].target_kind == WIT_WORLD_TARGET_UNKNOWN) {
            reg->world_items[i].target_kind =
                (reg->world_items[i].kind == WIT_WORLD_ITEM_INCLUDE)
                    ? WIT_WORLD_TARGET_WORLD
                    : WIT_WORLD_TARGET_INTERFACE;
        }

        if (reg->world_items[i].target_kind == WIT_WORLD_TARGET_WORLD) {
            if (!find_world_decl_const(reg, target_pkg, reg->world_items[i].target_name)) {
                if (!ensure_import_world_loaded(reg, target_pkg, reg->world_items[i].target_name)) {
                    return 0;
                }
            }
            if (!find_world_decl_const(reg, target_pkg, reg->world_items[i].target_name)) {
                codegen_die("unknown world %s from %s in world %s",
                            reg->world_items[i].target_name,
                            target_pkg,
                            reg->world_items[i].world_name);
            }
        } else if (reg->world_items[i].target_kind == WIT_WORLD_TARGET_INTERFACE
                   || reg->world_items[i].target_kind == WIT_WORLD_TARGET_FUNCTION) {
            if (!registry_has_interface_symbols(reg, effective_pkg, effective_name)) {
                if (!ensure_import_interface_loaded(reg, effective_pkg, effective_name)) {
                    return 0;
                }
            }
            if (!registry_has_interface_symbols(reg, effective_pkg, effective_name)) {
                codegen_die("unknown lowered interface %s from %s for world item %s in world %s",
                            effective_name,
                            effective_pkg,
                            reg->world_items[i].name,
                            reg->world_items[i].world_name);
            }
        }
    }

    return 1;
}

static int resolve_registry_symbol_scopes(WitRegistry *reg)
{
    char owner_symbol[MAX_SYMBOL];

    if (!reg) return 0;

    for (int i = 0; i < reg->alias_count; i++) {
        if (!qualify_type_expr_symbols(reg,
                                       reg->aliases[i].target,
                                       reg->aliases[i].package_full,
                                       reg->aliases[i].interface_name)) {
            return 0;
        }
    }
    for (int i = 0; i < reg->record_count; i++) {
        for (int j = 0; j < reg->records[i].field_count; j++) {
            if (!qualify_type_expr_symbols(reg,
                                           reg->records[i].fields[j].wit_type,
                                           reg->records[i].package_full,
                                           reg->records[i].interface_name)) {
                return 0;
            }
        }
    }
    for (int i = 0; i < reg->variant_count; i++) {
        for (int j = 0; j < reg->variants[i].case_count; j++) {
            if (reg->variants[i].cases[j].payload_type >= 0
                    && !qualify_type_expr_symbols(reg,
                                                  reg->variants[i].cases[j].payload_type,
                                                  reg->variants[i].package_full,
                                                  reg->variants[i].interface_name)) {
                return 0;
            }
        }
    }
    for (int i = 0; i < reg->func_count; i++) {
        for (int j = 0; j < reg->funcs[i].param_count; j++) {
            if (!qualify_type_expr_symbols(reg,
                                           reg->funcs[i].params[j].wit_type,
                                           reg->funcs[i].package_full,
                                           reg->funcs[i].interface_name)) {
                return 0;
            }
        }
        if (reg->funcs[i].result_type >= 0
                && !qualify_type_expr_symbols(reg,
                                              reg->funcs[i].result_type,
                                              reg->funcs[i].package_full,
                                              reg->funcs[i].interface_name)) {
                return 0;
        }
        if (reg->funcs[i].owner_kind == WIT_OWNER_RESOURCE) {
            if (!lookup_scoped_symbol_name(reg,
                                           reg->funcs[i].package_full,
                                           reg->funcs[i].interface_name,
                                           reg->funcs[i].owner_name,
                                           owner_symbol,
                                           (int)sizeof(owner_symbol))) {
                codegen_die("unknown resource %s in interface %s",
                            reg->funcs[i].owner_name,
                            reg->funcs[i].interface_name);
            }
            snprintf(reg->funcs[i].owner_symbol_name,
                     sizeof(reg->funcs[i].owner_symbol_name),
                     "%s",
                     owner_symbol);
        }
    }

    return 1;
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

static int wit_local_name_occurrences(const WitRegistry *reg, const char *local_name)
{
    int count = 0;

    if (!reg || !local_name || local_name[0] == '\0') return 0;

    for (int i = 0; i < reg->record_count; i++)
        if (strcmp(reg->records[i].name, local_name) == 0) count++;
    for (int i = 0; i < reg->variant_count; i++)
        if (strcmp(reg->variants[i].name, local_name) == 0) count++;
    for (int i = 0; i < reg->enum_count; i++)
        if (strcmp(reg->enums[i].name, local_name) == 0) count++;
    for (int i = 0; i < reg->flags_count; i++)
        if (strcmp(reg->flags[i].name, local_name) == 0) count++;
    for (int i = 0; i < reg->alias_count; i++)
        if (strcmp(reg->aliases[i].name, local_name) == 0) count++;
    for (int i = 0; i < reg->resource_count; i++)
        if (strcmp(reg->resources[i].name, local_name) == 0) count++;

    return count;
}

static void wit_emit_name_from_symbol(const WitRegistry *reg,
                                      const char *wit_name,
                                      char *out,
                                      int n)
{
    if (!out || n <= 0) return;
    out[0] = '\0';
    if (!wit_name) return;
    wit_symbol_display_name(reg, NULL, NULL, wit_name, out, n);
}

static const char *wit_record_symbol_name(const WitRecord *rec)
{
    return (rec && rec->symbol_name[0] != '\0') ? rec->symbol_name : (rec ? rec->name : "");
}

static const char *wit_variant_symbol_name(const WitVariant *var)
{
    return (var && var->symbol_name[0] != '\0') ? var->symbol_name : (var ? var->name : "");
}

static const char *wit_enum_symbol_name(const WitEnum *en)
{
    return (en && en->symbol_name[0] != '\0') ? en->symbol_name : (en ? en->name : "");
}

static const char *wit_flags_symbol_name(const WitFlags *fl)
{
    return (fl && fl->symbol_name[0] != '\0') ? fl->symbol_name : (fl ? fl->name : "");
}

static const char *wit_resource_symbol_name(const WitResource *resource)
{
    return (resource && resource->symbol_name[0] != '\0')
        ? resource->symbol_name
        : (resource ? resource->name : "");
}

static void wit_type_c_typename(const WitRegistry *reg, const char *wit_name, char *out, int n)
{
    char emitted[MAX_SYMBOL];
    char normalized[MAX_SYMBOL];
    char camel[MAX_SYMBOL];

    wit_emit_name_from_symbol(reg, wit_name, emitted, (int)sizeof(emitted));
    wit_trim_leading_package_tail(reg, emitted, normalized, (int)sizeof(normalized));
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
    char emitted[MAX_SYMBOL];
    char normalized[MAX_SYMBOL];
    char upper[MAX_SYMBOL];

    wit_emit_name_from_symbol(reg, wit_name, emitted, (int)sizeof(emitted));
    wit_trim_leading_package_tail(reg, emitted, normalized, (int)sizeof(normalized));
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
    char emitted[MAX_SYMBOL];
    char normalized[MAX_SYMBOL];
    char snake[MAX_SYMBOL];

    wit_emit_name_from_symbol(reg, wit_name, emitted, (int)sizeof(emitted));
    wit_trim_leading_package_tail(reg, emitted, normalized, (int)sizeof(normalized));
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

typedef struct {
    const WitWorldItem *item;
    const WitVariant   *command_variant;
    const WitVariant   *reply_variant;
} WorldEndpoint;

typedef struct {
    char package_full[MAX_PACKAGE];
    char world_name[MAX_NAME];
} WorldVisit;

static int wit_is_command_variant(const char *wit_name);
static const WitVariant *find_paired_reply_variant(const WitRegistry *reg,
                                                   const WitVariant *command_variant);

static void wit_package_symbol(const WitRegistry *reg,
                               const char *suffix,
                               char *out,
                               int n)
{
    if (!out || n <= 0) return;
    if (!suffix || suffix[0] == '\0') {
        out[0] = '\0';
        return;
    }

    if (reg && reg->package_snake[0] != '\0')
        snprintf(out, n, "sap_wit_%s_%s", reg->package_snake, suffix);
    else
        snprintf(out, n, "sap_wit_%s", suffix);
}

static void wit_dbi_schema_symbol(const WitRegistry *reg, char *out, int n)
{
    wit_package_symbol(reg, "dbi_schema", out, n);
}

static void wit_dbi_schema_count_symbol(const WitRegistry *reg, char *out, int n)
{
    wit_package_symbol(reg, "dbi_schema_count", out, n);
}

static void wit_interfaces_symbol(const WitRegistry *reg, char *out, int n)
{
    wit_package_symbol(reg, "interfaces", out, n);
}

static void wit_interfaces_count_symbol(const WitRegistry *reg, char *out, int n)
{
    wit_package_symbol(reg, "interfaces_count", out, n);
}

static void wit_worlds_symbol(const WitRegistry *reg, char *out, int n)
{
    wit_package_symbol(reg, "worlds", out, n);
}

static void wit_worlds_count_symbol(const WitRegistry *reg, char *out, int n)
{
    wit_package_symbol(reg, "worlds_count", out, n);
}

static void wit_world_bindings_symbol(const WitRegistry *reg, char *out, int n)
{
    wit_package_symbol(reg, "world_bindings", out, n);
}

static void wit_world_bindings_count_symbol(const WitRegistry *reg, char *out, int n)
{
    wit_package_symbol(reg, "world_bindings_count", out, n);
}

static const char *wit_world_item_kind_macro(WitWorldItemKind kind)
{
    switch (kind) {
    case WIT_WORLD_ITEM_INCLUDE:
        return "SAP_WIT_WORLD_ITEM_INCLUDE";
    case WIT_WORLD_ITEM_IMPORT:
        return "SAP_WIT_WORLD_ITEM_IMPORT";
    case WIT_WORLD_ITEM_EXPORT:
        return "SAP_WIT_WORLD_ITEM_EXPORT";
    }
    return "SAP_WIT_WORLD_ITEM_IMPORT";
}

static const char *wit_world_target_kind_macro(WitWorldTargetKind kind)
{
    switch (kind) {
    case WIT_WORLD_TARGET_INTERFACE:
        return "SAP_WIT_WORLD_TARGET_INTERFACE";
    case WIT_WORLD_TARGET_WORLD:
        return "SAP_WIT_WORLD_TARGET_WORLD";
    case WIT_WORLD_TARGET_FUNCTION:
        return "SAP_WIT_WORLD_TARGET_FUNCTION";
    case WIT_WORLD_TARGET_UNKNOWN:
        return "SAP_WIT_WORLD_TARGET_UNKNOWN";
    }
    return "SAP_WIT_WORLD_TARGET_UNKNOWN";
}

static int wit_world_item_matches_world(const WitWorldItem *item, const WitWorld *world)
{
    if (!item || !world) return 0;
    return strcmp(item->package_full, world->package_full) == 0
        && strcmp(item->world_name, world->name) == 0;
}

static uint32_t wit_world_binding_count(const WitRegistry *reg, const WitWorld *world)
{
    uint32_t count = 0;

    if (!reg || !world) return 0u;

    for (int i = 0; i < reg->world_item_count; i++) {
        if (wit_world_item_matches_world(&reg->world_items[i], world)) {
            count++;
        }
    }

    return count;
}

static void wit_package_ident_from_full(const char *package_full,
                                        void (*convert)(const char *, char *, int),
                                        char *out,
                                        int n)
{
    char package_name[MAX_NAME];

    if (!out || n <= 0) return;
    out[0] = '\0';
    if (!convert) return;

    wit_package_name_from_full(package_full, package_name, (int)sizeof(package_name));
    if (package_name[0] == '\0') return;
    convert(package_name, out, n);
}

static int wit_world_name_occurrences(const WitRegistry *reg,
                                      const char *package_full,
                                      const char *world_name)
{
    int count = 0;

    if (!reg || !world_name || world_name[0] == '\0') return 0;

    for (int i = 0; i < reg->world_count; i++) {
        if (strcmp(reg->worlds[i].name, world_name) != 0) continue;
        if (package_full && package_full[0] != '\0'
                && strcmp(reg->worlds[i].package_full, package_full) == 0) {
            return 1;
        }
        count++;
    }

    return count;
}

static void wit_world_display_name(const WitRegistry *reg,
                                   const char *package_full,
                                   const char *world_name,
                                   char *out,
                                   int n)
{
    char package_name[MAX_NAME];

    if (!out || n <= 0) return;
    out[0] = '\0';
    if (!world_name || world_name[0] == '\0') return;

    if (package_full && package_full[0] != '\0'
            && wit_world_name_occurrences(reg, package_full, world_name) <= 1) {
        snprintf(out, n, "%s", world_name);
        return;
    }

    wit_package_name_from_full(package_full, package_name, (int)sizeof(package_name));
    if (package_name[0] != '\0') {
        snprintf(out, n, "%s-%s", package_name, world_name);
    } else {
        snprintf(out, n, "%s", world_name);
    }
}

static void wit_world_binding_typename(const WitRegistry *reg,
                                       const char *package_full,
                                       const char *world_name,
                                       const char *kind_suffix,
                                       char *out,
                                       int n)
{
    char package_camel[MAX_NAME];
    char world_display[MAX_SYMBOL];
    char world_camel[MAX_SYMBOL];

    if (!out || n <= 0) return;
    out[0] = '\0';

    wit_package_ident_from_full(package_full,
                                wit_name_to_camel_ident,
                                package_camel,
                                (int)sizeof(package_camel));
    wit_world_display_name(reg, package_full, world_name, world_display, (int)sizeof(world_display));
    wit_name_to_camel_ident(world_display, world_camel, (int)sizeof(world_camel));

    if (package_camel[0] != '\0' && world_camel[0] != '\0') {
        snprintf(out, n, "SapWit%s%sWorld%s", package_camel, world_camel, kind_suffix);
    } else if (world_camel[0] != '\0') {
        snprintf(out, n, "SapWit%sWorld%s", world_camel, kind_suffix);
    } else if (package_camel[0] != '\0') {
        snprintf(out, n, "SapWit%sWorld%s", package_camel, kind_suffix);
    } else {
        snprintf(out, n, "SapWitWorld%s", kind_suffix);
    }
}

static void wit_world_binding_call_name(const WitRegistry *reg,
                                        const char *package_full,
                                        const char *world_name,
                                        const char *direction,
                                        const char *item_name,
                                        char *out,
                                        int n)
{
    char package_snake[MAX_NAME];
    char world_display[MAX_SYMBOL];
    char world_snake[MAX_SYMBOL];
    char item_snake[MAX_SYMBOL];

    if (!out || n <= 0) return;
    out[0] = '\0';

    wit_package_ident_from_full(package_full,
                                wit_name_to_snake_ident,
                                package_snake,
                                (int)sizeof(package_snake));
    wit_world_display_name(reg, package_full, world_name, world_display, (int)sizeof(world_display));
    wit_name_to_snake_ident(world_display, world_snake, (int)sizeof(world_snake));
    wit_name_to_snake_ident(item_name ? item_name : "", item_snake, (int)sizeof(item_snake));

    if (package_snake[0] != '\0') {
        snprintf(out,
                 n,
                 "sap_wit_world_%s_%s_%s_%s",
                 package_snake,
                 world_snake[0] != '\0' ? world_snake : "world",
                 direction ? direction : "bind",
                 item_snake[0] != '\0' ? item_snake : "item");
    } else {
        snprintf(out,
                 n,
                 "sap_wit_world_%s_%s_%s",
                 world_snake[0] != '\0' ? world_snake : "world",
                 direction ? direction : "bind",
                 item_snake[0] != '\0' ? item_snake : "item");
    }
}

static void wit_world_guest_call_name(const WitRegistry *reg,
                                      const char *package_full,
                                      const char *world_name,
                                      const char *direction,
                                      const char *item_name,
                                      char *out,
                                      int n)
{
    char package_snake[MAX_NAME];
    char world_display[MAX_SYMBOL];
    char world_snake[MAX_SYMBOL];
    char item_snake[MAX_SYMBOL];

    if (!out || n <= 0) return;
    out[0] = '\0';

    wit_package_ident_from_full(package_full,
                                wit_name_to_snake_ident,
                                package_snake,
                                (int)sizeof(package_snake));
    wit_world_display_name(reg, package_full, world_name, world_display, (int)sizeof(world_display));
    wit_name_to_snake_ident(world_display, world_snake, (int)sizeof(world_snake));
    wit_name_to_snake_ident(item_name ? item_name : "", item_snake, (int)sizeof(item_snake));

    if (package_snake[0] != '\0') {
        snprintf(out,
                 n,
                 "sap_wit_guest_%s_%s_%s_%s",
                 package_snake,
                 world_snake[0] != '\0' ? world_snake : "world",
                 direction ? direction : "bind",
                 item_snake[0] != '\0' ? item_snake : "item");
    } else {
        snprintf(out,
                 n,
                 "sap_wit_guest_%s_%s_%s",
                 world_snake[0] != '\0' ? world_snake : "world",
                 direction ? direction : "bind",
                 item_snake[0] != '\0' ? item_snake : "item");
    }
}

static void wit_world_endpoint_adapter_name(const WitRegistry *reg,
                                            const char *package_full,
                                            const char *world_name,
                                            const char *direction,
                                            const char *item_name,
                                            char *out,
                                            int n)
{
    char wrapper_name[MAX_NAME * 2];

    if (!out || n <= 0) return;
    out[0] = '\0';

    wit_world_binding_call_name(reg,
                                package_full,
                                world_name,
                                direction,
                                item_name,
                                wrapper_name,
                                (int)sizeof(wrapper_name));
    if (wrapper_name[0] != '\0') {
        snprintf(out, n, "%s_erased", wrapper_name);
    }
}

static void wit_world_endpoint_array_symbol(const WitRegistry *reg,
                                            const char *package_full,
                                            const char *world_name,
                                            const char *direction,
                                            char *out,
                                            int n)
{
    char package_snake[MAX_NAME];
    char world_display[MAX_SYMBOL];
    char world_snake[MAX_SYMBOL];

    if (!out || n <= 0) return;
    out[0] = '\0';

    wit_package_ident_from_full(package_full,
                                wit_name_to_snake_ident,
                                package_snake,
                                (int)sizeof(package_snake));
    wit_world_display_name(reg, package_full, world_name, world_display, (int)sizeof(world_display));
    wit_name_to_snake_ident(world_display, world_snake, (int)sizeof(world_snake));

    if (package_snake[0] != '\0') {
        snprintf(out,
                 n,
                 "sap_wit_%s_%s_%s_endpoints",
                 package_snake,
                 world_snake[0] != '\0' ? world_snake : "world",
                 direction ? direction : "bind");
    } else {
        snprintf(out,
                 n,
                 "sap_wit_%s_%s_endpoints",
                 world_snake[0] != '\0' ? world_snake : "world",
                 direction ? direction : "bind");
    }
}

static void wit_world_endpoint_count_symbol(const WitRegistry *reg,
                                            const char *package_full,
                                            const char *world_name,
                                            const char *direction,
                                            char *out,
                                            int n)
{
    char array_symbol[MAX_NAME * 2];

    if (!out || n <= 0) return;
    out[0] = '\0';
    wit_world_endpoint_array_symbol(reg,
                                    package_full,
                                    world_name,
                                    direction,
                                    array_symbol,
                                    (int)sizeof(array_symbol));
    if (array_symbol[0] != '\0') {
        snprintf(out, n, "%s_count", array_symbol);
    }
}

static const WitVariant *find_interface_command_variant(const WitRegistry *reg,
                                                        const char *package_full,
                                                        const char *interface_name)
{
    if (!reg || !interface_name || interface_name[0] == '\0') return NULL;

    for (int i = 0; i < reg->variant_count; i++) {
        if (!wit_is_command_variant(reg->variants[i].name)) continue;
        if (strcmp(reg->variants[i].package_full, package_full ? package_full : "") != 0) continue;
        if (strcmp(reg->variants[i].interface_name, interface_name) != 0) continue;
        return &reg->variants[i];
    }

    return NULL;
}

static int world_endpoint_seen(const WorldEndpoint *items,
                               int count,
                               const WitWorldItem *item)
{
    if (!items || !item) return 0;

    for (int i = 0; i < count; i++) {
        if (!items[i].item) continue;
        if (items[i].item->kind != item->kind) continue;
        if (strcmp(items[i].item->name, item->name) != 0) continue;
        if (strcmp(items[i].item->target_package_full, item->target_package_full) != 0) continue;
        if (strcmp(items[i].item->target_name, item->target_name) != 0) continue;
        if (strcmp(items[i].item->lowered_target_package_full, item->lowered_target_package_full) != 0) continue;
        if (strcmp(items[i].item->lowered_target_name, item->lowered_target_name) != 0) continue;
        return 1;
    }

    return 0;
}

static int world_visit_contains(const WorldVisit *visits,
                                int visit_count,
                                const char *package_full,
                                const char *world_name)
{
    if (!visits || !package_full || !world_name) return 0;

    for (int i = 0; i < visit_count; i++) {
        if (strcmp(visits[i].package_full, package_full) == 0
                && strcmp(visits[i].world_name, world_name) == 0) {
            return 1;
        }
    }

    return 0;
}

static int collect_world_endpoints_recursive(const WitRegistry *reg,
                                             const char *package_full,
                                             const char *world_name,
                                             WitWorldItemKind kind,
                                             WorldEndpoint *out,
                                             int *count_inout,
                                             int max_count,
                                             WorldVisit *visits,
                                             int visit_count)
{
    if (!reg || !package_full || !world_name || !out || !count_inout || !visits) {
        return 0;
    }

    if (world_visit_contains(visits, visit_count, package_full, world_name)) {
        return 1;
    }
    if (visit_count >= MAX_WORLDS) {
        fprintf(stderr, "wit_codegen: world include graph too deep around %s/%s\n",
                package_full, world_name);
        return 0;
    }

    snprintf(visits[visit_count].package_full,
             sizeof(visits[visit_count].package_full),
             "%s",
             package_full);
    snprintf(visits[visit_count].world_name,
             sizeof(visits[visit_count].world_name),
             "%s",
             world_name);
    visit_count++;

    for (int i = 0; i < reg->world_item_count; i++) {
        const WitWorldItem *item = &reg->world_items[i];
        const char *target_pkg = wit_world_item_effective_target_package(reg, item);
        const char *target_name = wit_world_item_effective_target_name(item);

        if (strcmp(item->package_full, package_full) != 0) continue;
        if (strcmp(item->world_name, world_name) != 0) continue;

        if (item->kind == WIT_WORLD_ITEM_INCLUDE) {
            if (!collect_world_endpoints_recursive(reg,
                                                   target_pkg,
                                                   item->target_name,
                                                   kind,
                                                   out,
                                                   count_inout,
                                                   max_count,
                                                   visits,
                                                   visit_count)) {
                return 0;
            }
            continue;
        }

        if (item->kind != kind
                || (item->target_kind != WIT_WORLD_TARGET_INTERFACE
                    && item->target_kind != WIT_WORLD_TARGET_FUNCTION)) {
            continue;
        }

        if (!world_endpoint_seen(out, *count_inout, item)) {
            const WitVariant *command_variant =
                find_interface_command_variant(reg, target_pkg, target_name);
            const WitVariant *reply_variant = command_variant
                ? find_paired_reply_variant(reg, command_variant)
                : NULL;

            if (!command_variant || !reply_variant) {
                continue;
            }
            if (*count_inout >= max_count) {
                fprintf(stderr, "wit_codegen: too many effective world endpoints in %s/%s\n",
                        package_full, world_name);
                return 0;
            }

            out[*count_inout].item = item;
            out[*count_inout].command_variant = command_variant;
            out[*count_inout].reply_variant = reply_variant;
            (*count_inout)++;
        }
    }

    return 1;
}

static int collect_world_endpoints(const WitRegistry *reg,
                                   const char *package_full,
                                   const char *world_name,
                                   WitWorldItemKind kind,
                                   WorldEndpoint *out,
                                   int *count_out,
                                   int max_count)
{
    WorldVisit visits[MAX_WORLDS];
    int count = 0;

    if (!count_out) return 0;
    *count_out = 0;
    if (!reg || !package_full || !world_name || !out) return 0;

    if (!collect_world_endpoints_recursive(reg,
                                           package_full,
                                           world_name,
                                           kind,
                                           out,
                                           &count,
                                           max_count,
                                           visits,
                                           0)) {
        return 0;
    }

    *count_out = count;
    return 1;
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
        names[n] = wit_record_symbol_name(&reg->records[i]);
        dep_cnt[n] = 0;
        for (int j = 0; j < reg->records[i].field_count; j++)
            collect_struct_deps(reg, reg->records[i].fields[j].wit_type,
                               dep_buf[n], &dep_cnt[n], MAX_TYPES);
        n++;
    }
    for (int i = 0; i < reg->variant_count; i++) {
        names[n] = wit_variant_symbol_name(&reg->variants[i]);
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
    char interfaces_symbol[MAX_NAME * 2];
    char interfaces_count_symbol[MAX_NAME * 2];
    char worlds_symbol[MAX_NAME * 2];
    char worlds_count_symbol[MAX_NAME * 2];
    char world_bindings_symbol[MAX_NAME * 2];
    char world_bindings_count_symbol[MAX_NAME * 2];
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
    fprintf(out, "#include \"croft/wit_guest_runtime.h\"\n");
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
            wit_macro_name(reg,
                           wit_resource_symbol_name(&reg->resources[i]),
                           macro_name,
                           (int)sizeof(macro_name));
            wit_resource_c_typename(reg,
                                    wit_resource_symbol_name(&reg->resources[i]),
                                    type_name,
                                    (int)sizeof(type_name));
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
        wit_macro_name(reg, wit_enum_symbol_name(en), macro_name, (int)sizeof(macro_name));
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
        wit_macro_name(reg, wit_flags_symbol_name(fl), macro_name, (int)sizeof(macro_name));
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
        wit_macro_name(reg, wit_variant_symbol_name(var), macro_name, (int)sizeof(macro_name));
        fprintf(out, "/* WIT variant %s case tags. */\n", var->name);
        for (int j = 0; j < var->case_count; j++) {
            char case_upper[MAX_NAME];
            char case_trace[256];
            wit_name_to_upper_ident(var->cases[j].name, case_upper, (int)sizeof(case_upper));
            format_variant_case_trace(reg,
                                      var->package_full,
                                      var->interface_name,
                                      &var->cases[j],
                                      case_trace,
                                      (int)sizeof(case_trace));
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
            fprintf(out, "/* WIT record %s -> %s. */\n", rec->name, type_name);
            fprintf(out, "typedef struct {\n");
            for (int j = 0; j < rec->field_count; j++) {
                char field_name[MAX_NAME];
                char field_trace[256];
                wit_name_to_snake_ident(rec->fields[j].name, field_name, (int)sizeof(field_name));
                format_field_trace(reg,
                                   rec->package_full,
                                   rec->interface_name,
                                   &rec->fields[j],
                                   field_trace,
                                   (int)sizeof(field_trace));
                emit_trace_comment(out, "    ", field_trace);
                emit_c_fields(out, reg, rec->fields[j].wit_type, field_name, "    ");
            }
            fprintf(out, "} %s;\n\n", type_name);
            continue;
        }
        const WitVariant *var = find_variant(reg, tname);
        if (var) {
            wit_type_c_typename(reg, tname, type_name, (int)sizeof(type_name));
            fprintf(out, "/* WIT variant %s -> %s. */\n", var->name, type_name);
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
                    format_variant_case_trace(reg,
                                              var->package_full,
                                              var->interface_name,
                                              &var->cases[j],
                                              case_trace,
                                              (int)sizeof(case_trace));
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
        wit_type_c_typename(reg, wit_variant_symbol_name(var), type_name, (int)sizeof(type_name));
        wit_zero_name(reg, wit_variant_symbol_name(var), zero_name, (int)sizeof(zero_name));
        wit_dispose_name(reg, wit_variant_symbol_name(var), dispose_name, (int)sizeof(dispose_name));
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

    fprintf(out, "/* Generated world binding bundles and wrapper dispatchers. */\n");
    for (int i = 0; i < reg->world_count; i++) {
        WorldEndpoint imports[MAX_WORLD_ITEMS];
        WorldEndpoint exports[MAX_WORLD_ITEMS];
        int import_count = 0;
        int export_count = 0;
        char imports_type[MAX_NAME * 2];
        char exports_type[MAX_NAME * 2];

        if (!collect_world_endpoints(reg,
                                     reg->worlds[i].package_full,
                                     reg->worlds[i].name,
                                     WIT_WORLD_ITEM_IMPORT,
                                     imports,
                                     &import_count,
                                     MAX_WORLD_ITEMS)) {
            return;
        }
        if (!collect_world_endpoints(reg,
                                     reg->worlds[i].package_full,
                                     reg->worlds[i].name,
                                     WIT_WORLD_ITEM_EXPORT,
                                     exports,
                                     &export_count,
                                     MAX_WORLD_ITEMS)) {
            return;
        }
        if (import_count <= 0 && export_count <= 0) {
            continue;
        }

        if (import_count > 0) {
            wit_world_binding_typename(reg,
                                       reg->worlds[i].package_full,
                                       reg->worlds[i].name,
                                       "Imports",
                                       imports_type,
                                       (int)sizeof(imports_type));
            fprintf(out, "/* WIT world %s effective imports. */\n", reg->worlds[i].name);
            fprintf(out, "typedef struct {\n");
            for (int j = 0; j < import_count; j++) {
                char field_name[MAX_NAME];
                char ops_type[MAX_NAME * 2];

                wit_name_to_snake_ident(imports[j].item->name, field_name, (int)sizeof(field_name));
                wit_dispatch_ops_typename(reg,
                                          wit_variant_symbol_name(imports[j].command_variant),
                                          ops_type,
                                          (int)sizeof(ops_type));
                fprintf(out, "    void *%s_ctx;\n", field_name);
                fprintf(out, "    const %s *%s_ops;\n", ops_type, field_name);
            }
            fprintf(out, "} %s;\n", imports_type);
            for (int j = 0; j < import_count; j++) {
                char command_type[MAX_NAME * 2];
                char reply_type[MAX_NAME * 2];
                char wrapper_name[MAX_NAME * 2];
                char guest_name[MAX_NAME * 2];

                wit_type_c_typename(reg,
                                    wit_variant_symbol_name(imports[j].command_variant),
                                    command_type,
                                    (int)sizeof(command_type));
                wit_type_c_typename(reg,
                                    wit_variant_symbol_name(imports[j].reply_variant),
                                    reply_type,
                                    (int)sizeof(reply_type));
                wit_world_binding_call_name(reg,
                                            reg->worlds[i].package_full,
                                            reg->worlds[i].name,
                                            "import",
                                            imports[j].item->name,
                                            wrapper_name,
                                            (int)sizeof(wrapper_name));
                wit_world_guest_call_name(reg,
                                          reg->worlds[i].package_full,
                                          reg->worlds[i].name,
                                          "import",
                                          imports[j].item->name,
                                          guest_name,
                                          (int)sizeof(guest_name));
                fprintf(out,
                        "int32_t %s(const %s *bindings, const %s *command, %s *reply_out);\n",
                        wrapper_name,
                        imports_type,
                        command_type,
                        reply_type);
                fprintf(out,
                        "int32_t %s(SapWitGuestTransport *transport, const %s *command, %s *reply_out);\n",
                        guest_name,
                        command_type,
                        reply_type);
            }
            {
                char endpoints_symbol[MAX_NAME * 2];
                char endpoints_count_symbol[MAX_NAME * 2];

                wit_world_endpoint_array_symbol(reg,
                                                reg->worlds[i].package_full,
                                                reg->worlds[i].name,
                                                "import",
                                                endpoints_symbol,
                                                (int)sizeof(endpoints_symbol));
                wit_world_endpoint_count_symbol(reg,
                                                reg->worlds[i].package_full,
                                                reg->worlds[i].name,
                                                "import",
                                                endpoints_count_symbol,
                                                (int)sizeof(endpoints_count_symbol));
                fprintf(out, "extern const SapWitWorldEndpointDescriptor %s[];\n", endpoints_symbol);
                fprintf(out, "extern const uint32_t %s;\n", endpoints_count_symbol);
            }
            fprintf(out, "\n");
        }

        if (export_count > 0) {
            wit_world_binding_typename(reg,
                                       reg->worlds[i].package_full,
                                       reg->worlds[i].name,
                                       "Exports",
                                       exports_type,
                                       (int)sizeof(exports_type));
            fprintf(out, "/* WIT world %s effective exports. */\n", reg->worlds[i].name);
            fprintf(out, "typedef struct {\n");
            for (int j = 0; j < export_count; j++) {
                char field_name[MAX_NAME];
                char ops_type[MAX_NAME * 2];

                wit_name_to_snake_ident(exports[j].item->name, field_name, (int)sizeof(field_name));
                wit_dispatch_ops_typename(reg,
                                          wit_variant_symbol_name(exports[j].command_variant),
                                          ops_type,
                                          (int)sizeof(ops_type));
                fprintf(out, "    void *%s_ctx;\n", field_name);
                fprintf(out, "    const %s *%s_ops;\n", ops_type, field_name);
            }
            fprintf(out, "} %s;\n", exports_type);
            for (int j = 0; j < export_count; j++) {
                char command_type[MAX_NAME * 2];
                char reply_type[MAX_NAME * 2];
                char wrapper_name[MAX_NAME * 2];

                wit_type_c_typename(reg,
                                    wit_variant_symbol_name(exports[j].command_variant),
                                    command_type,
                                    (int)sizeof(command_type));
                wit_type_c_typename(reg,
                                    wit_variant_symbol_name(exports[j].reply_variant),
                                    reply_type,
                                    (int)sizeof(reply_type));
                wit_world_binding_call_name(reg,
                                            reg->worlds[i].package_full,
                                            reg->worlds[i].name,
                                            "export",
                                            exports[j].item->name,
                                            wrapper_name,
                                            (int)sizeof(wrapper_name));
                fprintf(out,
                        "int32_t %s(const %s *bindings, const %s *command, %s *reply_out);\n",
                        wrapper_name,
                        exports_type,
                        command_type,
                        reply_type);
            }
            {
                char endpoints_symbol[MAX_NAME * 2];
                char endpoints_count_symbol[MAX_NAME * 2];

                wit_world_endpoint_array_symbol(reg,
                                                reg->worlds[i].package_full,
                                                reg->worlds[i].name,
                                                "export",
                                                endpoints_symbol,
                                                (int)sizeof(endpoints_symbol));
                wit_world_endpoint_count_symbol(reg,
                                                reg->worlds[i].package_full,
                                                reg->worlds[i].name,
                                                "export",
                                                endpoints_count_symbol,
                                                (int)sizeof(endpoints_count_symbol));
                fprintf(out, "extern const SapWitWorldEndpointDescriptor %s[];\n", endpoints_symbol);
                fprintf(out, "extern const uint32_t %s;\n", endpoints_count_symbol);
            }
            fprintf(out, "\n");
        }
    }

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
    if (reg->interface_count > 0) {
        wit_interfaces_symbol(reg, interfaces_symbol, (int)sizeof(interfaces_symbol));
        wit_interfaces_count_symbol(reg, interfaces_count_symbol, (int)sizeof(interfaces_count_symbol));
        fprintf(out, "extern const SapWitInterfaceDescriptor %s[];\n", interfaces_symbol);
        fprintf(out, "extern const uint32_t %s;\n\n", interfaces_count_symbol);
    }
    if (reg->world_count > 0) {
        wit_worlds_symbol(reg, worlds_symbol, (int)sizeof(worlds_symbol));
        wit_worlds_count_symbol(reg, worlds_count_symbol, (int)sizeof(worlds_count_symbol));
        fprintf(out, "extern const SapWitWorldDescriptor %s[];\n", worlds_symbol);
        fprintf(out, "extern const uint32_t %s;\n\n", worlds_count_symbol);
    }
    if (reg->world_item_count > 0) {
        wit_world_bindings_symbol(reg, world_bindings_symbol, (int)sizeof(world_bindings_symbol));
        wit_world_bindings_count_symbol(reg,
                                        world_bindings_count_symbol,
                                        (int)sizeof(world_bindings_count_symbol));
        fprintf(out, "extern const SapWitWorldBindingDescriptor %s[];\n", world_bindings_symbol);
        fprintf(out, "extern const uint32_t %s;\n\n", world_bindings_count_symbol);
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
    fprintf(out, "# Columns: kind\twit-name\tnormalized\tc-name\tmacro-or-function\tmetadata\n");

    for (int i = 0; i < reg->interface_count; i++) {
        char wit_name[MAX_SYMBOL];
        char metadata[MAX_METADATA];

        if (reg->interfaces[i].package_full[0] != '\0') {
            snprintf(wit_name, sizeof(wit_name), "%s/%s", reg->interfaces[i].package_full, reg->interfaces[i].name);
        } else {
            snprintf(wit_name, sizeof(wit_name), "%s", reg->interfaces[i].name);
        }
        snprintf(metadata,
                 sizeof(metadata),
                 "imported=%d%s%s%s%s%s%s",
                 reg->interfaces[i].imported ? 1 : 0,
                 reg->interfaces[i].metadata[0] ? "; attrs=" : "",
                 reg->interfaces[i].metadata[0] ? reg->interfaces[i].metadata : "",
                 reg->interfaces[i].origin_world_name[0] ? "; origin=" : "",
                 reg->interfaces[i].origin_world_name[0] ? reg->interfaces[i].origin_world_name : "",
                 reg->interfaces[i].origin_item_name[0] ? "::" : "",
                 reg->interfaces[i].origin_item_name[0] ? reg->interfaces[i].origin_item_name : "");
        fprintf(out, "interface\t%s\t%s\t<none>\t<none>\t%s\n",
                wit_name,
                reg->interfaces[i].name,
                metadata);
    }

    for (int i = 0; i < reg->world_count; i++) {
        char wit_name[MAX_SYMBOL];
        char metadata[MAX_METADATA];

        if (reg->worlds[i].package_full[0] != '\0') {
            snprintf(wit_name, sizeof(wit_name), "%s/%s", reg->worlds[i].package_full, reg->worlds[i].name);
        } else {
            snprintf(wit_name, sizeof(wit_name), "%s", reg->worlds[i].name);
        }
        snprintf(metadata,
                 sizeof(metadata),
                 "imported=%d%s%s",
                 reg->worlds[i].imported ? 1 : 0,
                 reg->worlds[i].metadata[0] ? "; attrs=" : "",
                 reg->worlds[i].metadata[0] ? reg->worlds[i].metadata : "");
        fprintf(out, "world\t%s\t%s\t<none>\t<none>\t%s\n",
                wit_name,
                reg->worlds[i].name,
                metadata);
    }

    for (int i = 0; i < reg->world_item_count; i++) {
        const char *kind = "world-item";
        const char *target_kind = "unknown";
        char wit_name[MAX_SYMBOL];
        char target_name[MAX_SYMBOL];
        char metadata[MAX_METADATA];

        switch (reg->world_items[i].kind) {
        case WIT_WORLD_ITEM_INCLUDE:
            kind = "world-include";
            break;
        case WIT_WORLD_ITEM_IMPORT:
            kind = "world-import";
            break;
        case WIT_WORLD_ITEM_EXPORT:
            kind = "world-export";
            break;
        }
        switch (reg->world_items[i].target_kind) {
        case WIT_WORLD_TARGET_INTERFACE:
            target_kind = "interface";
            break;
        case WIT_WORLD_TARGET_WORLD:
            target_kind = "world";
            break;
        case WIT_WORLD_TARGET_FUNCTION:
            target_kind = "function";
            break;
        case WIT_WORLD_TARGET_UNKNOWN:
            target_kind = "unknown";
            break;
        }

        if (reg->world_items[i].package_full[0] != '\0' && reg->world_items[i].world_name[0] != '\0') {
            snprintf(wit_name,
                     sizeof(wit_name),
                     "%s/%s::%s",
                     reg->world_items[i].package_full,
                     reg->world_items[i].world_name,
                     reg->world_items[i].name);
        } else if (reg->world_items[i].world_name[0] != '\0') {
            snprintf(wit_name,
                     sizeof(wit_name),
                     "%s::%s",
                     reg->world_items[i].world_name,
                     reg->world_items[i].name);
        } else {
            snprintf(wit_name, sizeof(wit_name), "%s", reg->world_items[i].name);
        }
        if (reg->world_items[i].target_package_full[0] != '\0') {
            snprintf(target_name,
                     sizeof(target_name),
                     "%s/%s",
                     reg->world_items[i].target_package_full,
                     reg->world_items[i].target_name);
        } else {
            snprintf(target_name, sizeof(target_name), "%s", reg->world_items[i].target_name);
        }
        snprintf(metadata,
                 sizeof(metadata),
                 "imported=%d; target-kind=%s; target=%s%s%s",
                 reg->world_items[i].imported ? 1 : 0,
                 target_kind,
                 target_name,
                 reg->world_items[i].metadata[0] ? "; attrs=" : "",
                 reg->world_items[i].metadata[0] ? reg->world_items[i].metadata : "");
        if ((reg->world_items[i].target_kind == WIT_WORLD_TARGET_FUNCTION
                || reg->world_items[i].lowered_target_name[0] != '\0')
                && (reg->world_items[i].lowered_target_name[0] != '\0')
                && (strcmp(reg->world_items[i].lowered_target_name,
                           reg->world_items[i].target_name) != 0
                    || strcmp(reg->world_items[i].lowered_target_package_full,
                              reg->world_items[i].target_package_full) != 0
                    || reg->world_items[i].target_kind == WIT_WORLD_TARGET_FUNCTION)) {
            char lowered_name[MAX_SYMBOL];

            if (reg->world_items[i].lowered_target_package_full[0] != '\0') {
                snprintf(lowered_name,
                         sizeof(lowered_name),
                         "%s/%s",
                         reg->world_items[i].lowered_target_package_full,
                         reg->world_items[i].lowered_target_name);
            } else {
                snprintf(lowered_name, sizeof(lowered_name), "%s", reg->world_items[i].lowered_target_name);
            }
            snprintf(metadata + strlen(metadata),
                     sizeof(metadata) - strlen(metadata),
                     "; lowered=%s",
                     lowered_name);
        }
        fprintf(out, "%s\t%s\t%s\t<none>\t<none>\t%s\n",
                kind,
                wit_name,
                reg->world_items[i].name,
                metadata);
    }

    for (int i = 0; i < reg->resource_count; i++) {
        wit_emit_name_from_symbol(reg,
                                  wit_resource_symbol_name(&reg->resources[i]),
                                  normalized,
                                  (int)sizeof(normalized));
        wit_resource_c_typename(reg, wit_resource_symbol_name(&reg->resources[i]), c_name, (int)sizeof(c_name));
        wit_macro_name(reg, wit_resource_symbol_name(&reg->resources[i]), macro_name, (int)sizeof(macro_name));
        fprintf(out, "resource\t%s\t%s\t%s\t%s_RESOURCE_INVALID\t%s\n",
                wit_resource_symbol_name(&reg->resources[i]),
                normalized[0] ? normalized : "<exact-package>",
                c_name,
                macro_name,
                reg->resources[i].metadata[0] ? reg->resources[i].metadata : "-");
    }

    for (int i = 0; i < reg->record_count; i++) {
        wit_emit_name_from_symbol(reg,
                                  wit_record_symbol_name(&reg->records[i]),
                                  normalized,
                                  (int)sizeof(normalized));
        wit_type_c_typename(reg, wit_record_symbol_name(&reg->records[i]), c_name, (int)sizeof(c_name));
        wit_writer_name(reg, wit_record_symbol_name(&reg->records[i]), fn_name, (int)sizeof(fn_name));
        fprintf(out, "record\t%s\t%s\t%s\t%s\t%s\n",
                wit_record_symbol_name(&reg->records[i]),
                normalized[0] ? normalized : "<exact-package>",
                c_name,
                fn_name,
                reg->records[i].metadata[0] ? reg->records[i].metadata : "-");
    }

    for (int i = 0; i < reg->variant_count; i++) {
        wit_emit_name_from_symbol(reg,
                                  wit_variant_symbol_name(&reg->variants[i]),
                                  normalized,
                                  (int)sizeof(normalized));
        wit_type_c_typename(reg, wit_variant_symbol_name(&reg->variants[i]), c_name, (int)sizeof(c_name));
        wit_macro_name(reg, wit_variant_symbol_name(&reg->variants[i]), macro_name, (int)sizeof(macro_name));
        fprintf(out, "variant\t%s\t%s\t%s\t%s\t%s\n",
                wit_variant_symbol_name(&reg->variants[i]),
                normalized[0] ? normalized : "<exact-package>",
                c_name,
                macro_name,
                reg->variants[i].metadata[0] ? reg->variants[i].metadata : "-");
    }

    for (int i = 0; i < reg->enum_count; i++) {
        wit_emit_name_from_symbol(reg,
                                  wit_enum_symbol_name(&reg->enums[i]),
                                  normalized,
                                  (int)sizeof(normalized));
        wit_type_c_typename(reg, wit_enum_symbol_name(&reg->enums[i]), c_name, (int)sizeof(c_name));
        wit_macro_name(reg, wit_enum_symbol_name(&reg->enums[i]), macro_name, (int)sizeof(macro_name));
        fprintf(out, "enum\t%s\t%s\t%s\t%s\t%s\n",
                wit_enum_symbol_name(&reg->enums[i]),
                normalized[0] ? normalized : "<exact-package>",
                c_name,
                macro_name,
                reg->enums[i].metadata[0] ? reg->enums[i].metadata : "-");
    }

    for (int i = 0; i < reg->flags_count; i++) {
        wit_emit_name_from_symbol(reg,
                                  wit_flags_symbol_name(&reg->flags[i]),
                                  normalized,
                                  (int)sizeof(normalized));
        wit_type_c_typename(reg, wit_flags_symbol_name(&reg->flags[i]), c_name, (int)sizeof(c_name));
        wit_macro_name(reg, wit_flags_symbol_name(&reg->flags[i]), macro_name, (int)sizeof(macro_name));
        fprintf(out, "flags\t%s\t%s\t%s\t%s\t%s\n",
                wit_flags_symbol_name(&reg->flags[i]),
                normalized[0] ? normalized : "<exact-package>",
                c_name,
                macro_name,
                reg->flags[i].metadata[0] ? reg->flags[i].metadata : "-");
    }

    for (int i = 0; i < ndbi; i++) {
        wit_trim_leading_package_tail(reg, dbis[i].val_rec, normalized, (int)sizeof(normalized));
        wit_validator_name(reg, dbis[i].val_rec, fn_name, (int)sizeof(fn_name));
        fprintf(out, "validator\t%s\t%s\t<none>\t%s\t-\n",
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

    wit_writer_name(reg, wit_record_symbol_name(rec), fn_name, (int)sizeof(fn_name));
    wit_type_c_typename(reg, wit_record_symbol_name(rec), type_name, (int)sizeof(type_name));

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

    wit_writer_name(reg, wit_variant_symbol_name(var), fn_name, (int)sizeof(fn_name));
    wit_type_c_typename(reg, wit_variant_symbol_name(var), type_name, (int)sizeof(type_name));
    wit_macro_name(reg, wit_variant_symbol_name(var), macro_name, (int)sizeof(macro_name));

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

    wit_reader_name(reg, wit_record_symbol_name(rec), fn_name, (int)sizeof(fn_name));
    wit_type_c_typename(reg, wit_record_symbol_name(rec), type_name, (int)sizeof(type_name));

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

    wit_reader_name(reg, wit_variant_symbol_name(var), fn_name, (int)sizeof(fn_name));
    wit_type_c_typename(reg, wit_variant_symbol_name(var), type_name, (int)sizeof(type_name));
    wit_macro_name(reg, wit_variant_symbol_name(var), macro_name, (int)sizeof(macro_name));

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

    wit_type_c_typename(reg, wit_variant_symbol_name(var), type_name, (int)sizeof(type_name));
    wit_zero_name(reg, wit_variant_symbol_name(var), zero_name, (int)sizeof(zero_name));
    wit_dispose_name(reg, wit_variant_symbol_name(var), dispose_name, (int)sizeof(dispose_name));
    wit_macro_name(reg, wit_variant_symbol_name(var), macro_name, (int)sizeof(macro_name));

    fprintf(out, "/* Generated zero helper for %s. */\n", var->name);
    fprintf(out, "void %s(%s *out)\n{\n", zero_name, type_name);
    fprintf(out, "    if (!out) {\n");
    fprintf(out, "        return;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    sap_wit_rt_memset(out, 0, sizeof(*out));\n");
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
            fprintf(out, "            sap_wit_rt_free((void*)out->val.%s.v_val.ok.v_data);\n", case_snake);
            fprintf(out, "        }\n");
        } else if (ownership_kind == 2) {
            fprintf(out, "        if (out->val.%s.is_v_ok\n", case_snake);
            fprintf(out, "                && out->val.%s.v_val.ok.has_v\n", case_snake);
            fprintf(out, "                && out->val.%s.v_val.ok.v_data) {\n", case_snake);
            fprintf(out, "            sap_wit_rt_free((void*)out->val.%s.v_val.ok.v_data);\n", case_snake);
            fprintf(out, "        }\n");
        }
        fprintf(out, "        break;\n");
    }
    fprintf(out, "    default:\n");
    fprintf(out, "        break;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    sap_wit_rt_memset(out, 0, sizeof(*out));\n");
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
    char interfaces_symbol[MAX_NAME * 2];
    char interfaces_count_symbol[MAX_NAME * 2];
    char worlds_symbol[MAX_NAME * 2];
    char worlds_count_symbol[MAX_NAME * 2];
    char world_bindings_symbol[MAX_NAME * 2];
    char world_bindings_count_symbol[MAX_NAME * 2];
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
    fprintf(out, "#include <stddef.h>\n");
    fprintf(out, "#include \"croft/wit_runtime_support.h\"\n\n");

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
    if (reg->interface_count > 0) {
        wit_interfaces_symbol(reg, interfaces_symbol, (int)sizeof(interfaces_symbol));
        wit_interfaces_count_symbol(reg, interfaces_count_symbol, (int)sizeof(interfaces_count_symbol));
        fprintf(out, "/* WIT interface metadata for package %s. */\n", reg->package_full);
        fprintf(out, "const SapWitInterfaceDescriptor %s[] = {\n", interfaces_symbol);
        for (int i = 0; i < reg->interface_count; i++) {
            fprintf(out, "    {");
            emit_c_string_literal(out, reg->interfaces[i].package_full);
            fprintf(out, ", ");
            emit_c_string_literal(out, reg->interfaces[i].name);
            fprintf(out, ", ");
            emit_c_string_literal(out, reg->interfaces[i].metadata);
            fprintf(out, ", ");
            emit_c_string_literal(out, reg->interfaces[i].origin_world_name);
            fprintf(out, ", ");
            emit_c_string_literal(out, reg->interfaces[i].origin_item_name);
            fprintf(out, ", %uu},\n", reg->interfaces[i].imported ? 1u : 0u);
        }
        fprintf(out, "};\n\n");
        fprintf(out, "const uint32_t %s =\n", interfaces_count_symbol);
        fprintf(out, "    (uint32_t)(sizeof(%s) / sizeof(%s[0]));\n\n",
                interfaces_symbol, interfaces_symbol);
    }
    if (reg->world_count > 0) {
        uint32_t binding_offset = 0u;

        wit_worlds_symbol(reg, worlds_symbol, (int)sizeof(worlds_symbol));
        wit_worlds_count_symbol(reg, worlds_count_symbol, (int)sizeof(worlds_count_symbol));
        fprintf(out, "/* WIT world metadata for package %s. */\n", reg->package_full);
        fprintf(out, "const SapWitWorldDescriptor %s[] = {\n", worlds_symbol);
        for (int i = 0; i < reg->world_count; i++) {
            uint32_t binding_count = wit_world_binding_count(reg, &reg->worlds[i]);

            fprintf(out, "    {");
            emit_c_string_literal(out, reg->worlds[i].package_full);
            fprintf(out, ", ");
            emit_c_string_literal(out, reg->worlds[i].name);
            fprintf(out, ", ");
            emit_c_string_literal(out, reg->worlds[i].metadata);
            fprintf(out,
                    ", %uu, %uu, %uu},\n",
                    reg->worlds[i].imported ? 1u : 0u,
                    binding_offset,
                    binding_count);
            binding_offset += binding_count;
        }
        fprintf(out, "};\n\n");
        fprintf(out, "const uint32_t %s =\n", worlds_count_symbol);
        fprintf(out, "    (uint32_t)(sizeof(%s) / sizeof(%s[0]));\n\n",
                worlds_symbol, worlds_symbol);
    }
    if (reg->world_item_count > 0) {
        wit_world_bindings_symbol(reg, world_bindings_symbol, (int)sizeof(world_bindings_symbol));
        wit_world_bindings_count_symbol(reg,
                                        world_bindings_count_symbol,
                                        (int)sizeof(world_bindings_count_symbol));
        fprintf(out, "/* WIT world binding metadata for package %s. */\n", reg->package_full);
        fprintf(out, "const SapWitWorldBindingDescriptor %s[] = {\n", world_bindings_symbol);
        for (int i = 0; i < reg->world_count; i++) {
            for (int j = 0; j < reg->world_item_count; j++) {
                if (!wit_world_item_matches_world(&reg->world_items[j], &reg->worlds[i])) {
                    continue;
                }

                fprintf(out, "    {%s, %s, ",
                        wit_world_item_kind_macro(reg->world_items[j].kind),
                        wit_world_target_kind_macro(reg->world_items[j].target_kind));
                emit_c_string_literal(out, reg->world_items[j].package_full);
                fprintf(out, ", ");
                emit_c_string_literal(out, reg->world_items[j].world_name);
                fprintf(out, ", ");
                emit_c_string_literal(out, reg->world_items[j].name);
                fprintf(out, ", ");
                emit_c_string_literal(out, reg->world_items[j].metadata);
                fprintf(out, ", %uu, ", reg->world_items[j].imported ? 1u : 0u);
                emit_c_string_literal(out, reg->world_items[j].target_package_full);
                fprintf(out, ", ");
                emit_c_string_literal(out, reg->world_items[j].target_name);
                fprintf(out, ", ");
                emit_c_string_literal(out, reg->world_items[j].lowered_target_package_full);
                fprintf(out, ", ");
                emit_c_string_literal(out, reg->world_items[j].lowered_target_name);
                fprintf(out, "},\n");
            }
        }
        fprintf(out, "};\n\n");
        fprintf(out, "const uint32_t %s =\n", world_bindings_count_symbol);
        fprintf(out, "    (uint32_t)(sizeof(%s) / sizeof(%s[0]));\n\n",
                world_bindings_symbol, world_bindings_symbol);
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

    fprintf(out, "/* ---- Generated world binding wrappers ---- */\n\n");
    for (int i = 0; i < reg->world_count; i++) {
        WorldEndpoint imports[MAX_WORLD_ITEMS];
        WorldEndpoint exports[MAX_WORLD_ITEMS];
        int import_count = 0;
        int export_count = 0;
        char imports_type[MAX_NAME * 2];
        char exports_type[MAX_NAME * 2];

        if (!collect_world_endpoints(reg,
                                     reg->worlds[i].package_full,
                                     reg->worlds[i].name,
                                     WIT_WORLD_ITEM_IMPORT,
                                     imports,
                                     &import_count,
                                     MAX_WORLD_ITEMS)) {
            return;
        }
        if (!collect_world_endpoints(reg,
                                     reg->worlds[i].package_full,
                                     reg->worlds[i].name,
                                     WIT_WORLD_ITEM_EXPORT,
                                     exports,
                                     &export_count,
                                     MAX_WORLD_ITEMS)) {
            return;
        }

        if (import_count > 0) {
            wit_world_binding_typename(reg,
                                       reg->worlds[i].package_full,
                                       reg->worlds[i].name,
                                       "Imports",
                                       imports_type,
                                       (int)sizeof(imports_type));
            for (int j = 0; j < import_count; j++) {
                char command_type[MAX_NAME * 2];
                char reply_type[MAX_NAME * 2];
                char dispatch_name[MAX_NAME * 2];
                char wrapper_name[MAX_NAME * 2];
                char adapter_name[MAX_NAME * 2];
                char read_adapter_name[MAX_NAME * 3];
                char write_command_adapter_name[MAX_NAME * 3];
                char read_reply_adapter_name[MAX_NAME * 3];
                char write_adapter_name[MAX_NAME * 3];
                char dispose_adapter_name[MAX_NAME * 3];
                char command_reader[MAX_NAME * 2];
                char command_writer[MAX_NAME * 2];
                char reply_reader[MAX_NAME * 2];
                char reply_writer[MAX_NAME * 2];
                char reply_dispose[MAX_NAME * 2];
                char field_name[MAX_NAME];

                wit_type_c_typename(reg,
                                    wit_variant_symbol_name(imports[j].command_variant),
                                    command_type,
                                    (int)sizeof(command_type));
                wit_type_c_typename(reg,
                                    wit_variant_symbol_name(imports[j].reply_variant),
                                    reply_type,
                                    (int)sizeof(reply_type));
                wit_dispatch_name(reg,
                                  wit_variant_symbol_name(imports[j].command_variant),
                                  dispatch_name,
                                  (int)sizeof(dispatch_name));
                wit_world_binding_call_name(reg,
                                            reg->worlds[i].package_full,
                                            reg->worlds[i].name,
                                            "import",
                                            imports[j].item->name,
                                            wrapper_name,
                                            (int)sizeof(wrapper_name));
                wit_world_endpoint_adapter_name(reg,
                                                reg->worlds[i].package_full,
                                                reg->worlds[i].name,
                                                "import",
                                                imports[j].item->name,
                                                adapter_name,
                                                (int)sizeof(adapter_name));
                wit_reader_name(reg,
                                wit_variant_symbol_name(imports[j].command_variant),
                                command_reader,
                                (int)sizeof(command_reader));
                wit_writer_name(reg,
                                wit_variant_symbol_name(imports[j].command_variant),
                                command_writer,
                                (int)sizeof(command_writer));
                wit_reader_name(reg,
                                wit_variant_symbol_name(imports[j].reply_variant),
                                reply_reader,
                                (int)sizeof(reply_reader));
                wit_writer_name(reg,
                                wit_variant_symbol_name(imports[j].reply_variant),
                                reply_writer,
                                (int)sizeof(reply_writer));
                wit_dispose_name(reg,
                                 wit_variant_symbol_name(imports[j].reply_variant),
                                 reply_dispose,
                                 (int)sizeof(reply_dispose));
                snprintf(read_adapter_name,
                         sizeof(read_adapter_name),
                         "%s_read_command",
                         adapter_name);
                snprintf(write_command_adapter_name,
                         sizeof(write_command_adapter_name),
                         "%s_write_command",
                         adapter_name);
                snprintf(read_reply_adapter_name,
                         sizeof(read_reply_adapter_name),
                         "%s_read_reply",
                         adapter_name);
                snprintf(write_adapter_name,
                         sizeof(write_adapter_name),
                         "%s_write_reply",
                         adapter_name);
                snprintf(dispose_adapter_name,
                         sizeof(dispose_adapter_name),
                         "%s_dispose_reply",
                         adapter_name);
                wit_name_to_snake_ident(imports[j].item->name, field_name, (int)sizeof(field_name));

                fprintf(out,
                        "int32_t %s(const %s *bindings, const %s *command, %s *reply_out)\n{\n",
                        wrapper_name,
                        imports_type,
                        command_type,
                        reply_type);
                fprintf(out, "    if (!bindings || !command || !reply_out || !bindings->%s_ops) {\n", field_name);
                fprintf(out, "        return -1;\n");
                fprintf(out, "    }\n");
                fprintf(out,
                        "    return %s(bindings->%s_ctx, bindings->%s_ops, command, reply_out);\n",
                        dispatch_name,
                        field_name,
                        field_name);
                fprintf(out, "}\n\n");

                fprintf(out,
                        "static int32_t %s(const void *bindings, const void *command, void *reply_out)\n{\n",
                        adapter_name);
                fprintf(out,
                        "    return %s((const %s *)bindings,\n"
                        "             (const %s *)command,\n"
                        "             (%s *)reply_out);\n",
                        wrapper_name,
                        imports_type,
                        command_type,
                        reply_type);
                fprintf(out, "}\n\n");

                fprintf(out,
                        "static int %s(const ThatchRegion *region, ThatchCursor *cursor, void *out)\n{\n",
                        read_adapter_name);
                fprintf(out,
                        "    return %s(region, cursor, (%s *)out);\n",
                        command_reader,
                        command_type);
                fprintf(out, "}\n\n");

                fprintf(out,
                        "static int %s(ThatchRegion *region, const void *value)\n{\n",
                        write_command_adapter_name);
                fprintf(out,
                        "    return %s(region, (const %s *)value);\n",
                        command_writer,
                        command_type);
                fprintf(out, "}\n\n");

                fprintf(out,
                        "static int %s(const ThatchRegion *region, ThatchCursor *cursor, void *out)\n{\n",
                        read_reply_adapter_name);
                fprintf(out,
                        "    return %s(region, cursor, (%s *)out);\n",
                        reply_reader,
                        reply_type);
                fprintf(out, "}\n\n");

                fprintf(out,
                        "static int %s(ThatchRegion *region, const void *value)\n{\n",
                        write_adapter_name);
                fprintf(out,
                        "    return %s(region, (const %s *)value);\n",
                        reply_writer,
                        reply_type);
                fprintf(out, "}\n\n");

                fprintf(out,
                        "static void %s(void *value)\n{\n",
                        dispose_adapter_name);
                fprintf(out,
                        "    %s((%s *)value);\n",
                        reply_dispose,
                        reply_type);
                fprintf(out, "}\n\n");
            }

            {
                char endpoints_symbol[MAX_NAME * 2];
                char endpoints_count_symbol[MAX_NAME * 2];

                wit_world_endpoint_array_symbol(reg,
                                                reg->worlds[i].package_full,
                                                reg->worlds[i].name,
                                                "import",
                                                endpoints_symbol,
                                                (int)sizeof(endpoints_symbol));
                wit_world_endpoint_count_symbol(reg,
                                                reg->worlds[i].package_full,
                                                reg->worlds[i].name,
                                                "import",
                                                endpoints_count_symbol,
                                                (int)sizeof(endpoints_count_symbol));
                fprintf(out, "const SapWitWorldEndpointDescriptor %s[] = {\n", endpoints_symbol);
                for (int j = 0; j < import_count; j++) {
                    char command_type[MAX_NAME * 2];
                    char reply_type[MAX_NAME * 2];
                    char ops_type[MAX_NAME * 2];
                    char adapter_name[MAX_NAME * 2];
                    char read_adapter_name[MAX_NAME * 3];
                    char write_command_adapter_name[MAX_NAME * 3];
                    char read_reply_adapter_name[MAX_NAME * 3];
                    char write_adapter_name[MAX_NAME * 3];
                    char dispose_adapter_name[MAX_NAME * 3];
                    char field_name[MAX_NAME];

                    wit_type_c_typename(reg,
                                        wit_variant_symbol_name(imports[j].command_variant),
                                        command_type,
                                        (int)sizeof(command_type));
                    wit_type_c_typename(reg,
                                        wit_variant_symbol_name(imports[j].reply_variant),
                                        reply_type,
                                        (int)sizeof(reply_type));
                    wit_dispatch_ops_typename(reg,
                                              wit_variant_symbol_name(imports[j].command_variant),
                                              ops_type,
                                              (int)sizeof(ops_type));
                    wit_world_endpoint_adapter_name(reg,
                                                    reg->worlds[i].package_full,
                                                    reg->worlds[i].name,
                                                    "import",
                                                    imports[j].item->name,
                                                    adapter_name,
                                                    (int)sizeof(adapter_name));
                    snprintf(read_adapter_name,
                             sizeof(read_adapter_name),
                             "%s_read_command",
                             adapter_name);
                    snprintf(write_command_adapter_name,
                             sizeof(write_command_adapter_name),
                             "%s_write_command",
                             adapter_name);
                    snprintf(read_reply_adapter_name,
                             sizeof(read_reply_adapter_name),
                             "%s_read_reply",
                             adapter_name);
                    snprintf(write_adapter_name,
                             sizeof(write_adapter_name),
                             "%s_write_reply",
                             adapter_name);
                    snprintf(dispose_adapter_name,
                             sizeof(dispose_adapter_name),
                             "%s_dispose_reply",
                             adapter_name);
                    wit_name_to_snake_ident(imports[j].item->name, field_name, (int)sizeof(field_name));

                    fprintf(out, "    {%s, %s, ",
                            wit_world_item_kind_macro(WIT_WORLD_ITEM_IMPORT),
                            wit_world_target_kind_macro(imports[j].item->target_kind));
                    emit_c_string_literal(out, reg->worlds[i].package_full);
                    fprintf(out, ", ");
                    emit_c_string_literal(out, reg->worlds[i].name);
                    fprintf(out, ", ");
                    emit_c_string_literal(out, imports[j].item->name);
                    fprintf(out, ", ");
                    emit_c_string_literal(out, imports[j].item->target_package_full);
                    fprintf(out, ", ");
                    emit_c_string_literal(out, imports[j].item->target_name);
                    fprintf(out, ", ");
                    emit_c_string_literal(out, wit_world_item_effective_target_package(reg, imports[j].item));
                    fprintf(out, ", ");
                    emit_c_string_literal(out, wit_world_item_effective_target_name(imports[j].item));
                    fprintf(out, ", ");
                    emit_c_string_literal(out, imports_type);
                    fprintf(out, ", ");
                    emit_c_string_literal(out, ops_type);
                    fprintf(out, ", ");
                    emit_c_string_literal(out, command_type);
                    fprintf(out, ", ");
                    emit_c_string_literal(out, reply_type);
                    fprintf(out,
                            ", sizeof(%s), sizeof(%s), offsetof(%s, %s_ctx), offsetof(%s, %s_ops), %s, %s, %s, %s, %s, %s},\n",
                            command_type,
                            reply_type,
                            imports_type,
                            field_name,
                            imports_type,
                            field_name,
                            read_adapter_name,
                            write_command_adapter_name,
                            read_reply_adapter_name,
                            write_adapter_name,
                            dispose_adapter_name,
                            adapter_name);
                }
                fprintf(out, "};\n");
                fprintf(out, "const uint32_t %s =\n", endpoints_count_symbol);
                fprintf(out, "    (uint32_t)(sizeof(%s) / sizeof(%s[0]));\n\n",
                        endpoints_symbol,
                        endpoints_symbol);

                for (int j = 0; j < import_count; j++) {
                    char command_type[MAX_NAME * 2];
                    char reply_type[MAX_NAME * 2];
                    char guest_name[MAX_NAME * 2];

                    wit_type_c_typename(reg,
                                        wit_variant_symbol_name(imports[j].command_variant),
                                        command_type,
                                        (int)sizeof(command_type));
                    wit_type_c_typename(reg,
                                        wit_variant_symbol_name(imports[j].reply_variant),
                                        reply_type,
                                        (int)sizeof(reply_type));
                    wit_world_guest_call_name(reg,
                                              reg->worlds[i].package_full,
                                              reg->worlds[i].name,
                                              "import",
                                              imports[j].item->name,
                                              guest_name,
                                              (int)sizeof(guest_name));
                    fprintf(out,
                            "int32_t %s(SapWitGuestTransport *transport, const %s *command, %s *reply_out)\n{\n",
                            guest_name,
                            command_type,
                            reply_type);
                    fprintf(out,
                            "    return sap_wit_guest_transport_call(transport,\n"
                            "                                       &%s[%d],\n"
                            "                                       command,\n"
                            "                                       reply_out);\n",
                            endpoints_symbol,
                            j);
                    fprintf(out, "}\n\n");
                }
            }
        }

        if (export_count > 0) {
            wit_world_binding_typename(reg,
                                       reg->worlds[i].package_full,
                                       reg->worlds[i].name,
                                       "Exports",
                                       exports_type,
                                       (int)sizeof(exports_type));
            for (int j = 0; j < export_count; j++) {
                char command_type[MAX_NAME * 2];
                char reply_type[MAX_NAME * 2];
                char dispatch_name[MAX_NAME * 2];
                char wrapper_name[MAX_NAME * 2];
                char adapter_name[MAX_NAME * 2];
                char read_adapter_name[MAX_NAME * 3];
                char write_command_adapter_name[MAX_NAME * 3];
                char read_reply_adapter_name[MAX_NAME * 3];
                char write_adapter_name[MAX_NAME * 3];
                char dispose_adapter_name[MAX_NAME * 3];
                char command_reader[MAX_NAME * 2];
                char command_writer[MAX_NAME * 2];
                char reply_reader[MAX_NAME * 2];
                char reply_writer[MAX_NAME * 2];
                char reply_dispose[MAX_NAME * 2];
                char field_name[MAX_NAME];

                wit_type_c_typename(reg,
                                    wit_variant_symbol_name(exports[j].command_variant),
                                    command_type,
                                    (int)sizeof(command_type));
                wit_type_c_typename(reg,
                                    wit_variant_symbol_name(exports[j].reply_variant),
                                    reply_type,
                                    (int)sizeof(reply_type));
                wit_dispatch_name(reg,
                                  wit_variant_symbol_name(exports[j].command_variant),
                                  dispatch_name,
                                  (int)sizeof(dispatch_name));
                wit_world_binding_call_name(reg,
                                            reg->worlds[i].package_full,
                                            reg->worlds[i].name,
                                            "export",
                                            exports[j].item->name,
                                            wrapper_name,
                                            (int)sizeof(wrapper_name));
                wit_world_endpoint_adapter_name(reg,
                                                reg->worlds[i].package_full,
                                                reg->worlds[i].name,
                                                "export",
                                                exports[j].item->name,
                                                adapter_name,
                                                (int)sizeof(adapter_name));
                wit_reader_name(reg,
                                wit_variant_symbol_name(exports[j].command_variant),
                                command_reader,
                                (int)sizeof(command_reader));
                wit_writer_name(reg,
                                wit_variant_symbol_name(exports[j].command_variant),
                                command_writer,
                                (int)sizeof(command_writer));
                wit_reader_name(reg,
                                wit_variant_symbol_name(exports[j].reply_variant),
                                reply_reader,
                                (int)sizeof(reply_reader));
                wit_writer_name(reg,
                                wit_variant_symbol_name(exports[j].reply_variant),
                                reply_writer,
                                (int)sizeof(reply_writer));
                wit_dispose_name(reg,
                                 wit_variant_symbol_name(exports[j].reply_variant),
                                 reply_dispose,
                                 (int)sizeof(reply_dispose));
                snprintf(read_adapter_name,
                         sizeof(read_adapter_name),
                         "%s_read_command",
                         adapter_name);
                snprintf(write_command_adapter_name,
                         sizeof(write_command_adapter_name),
                         "%s_write_command",
                         adapter_name);
                snprintf(read_reply_adapter_name,
                         sizeof(read_reply_adapter_name),
                         "%s_read_reply",
                         adapter_name);
                snprintf(write_adapter_name,
                         sizeof(write_adapter_name),
                         "%s_write_reply",
                         adapter_name);
                snprintf(dispose_adapter_name,
                         sizeof(dispose_adapter_name),
                         "%s_dispose_reply",
                         adapter_name);
                wit_name_to_snake_ident(exports[j].item->name, field_name, (int)sizeof(field_name));

                fprintf(out,
                        "int32_t %s(const %s *bindings, const %s *command, %s *reply_out)\n{\n",
                        wrapper_name,
                        exports_type,
                        command_type,
                        reply_type);
                fprintf(out, "    if (!bindings || !command || !reply_out || !bindings->%s_ops) {\n", field_name);
                fprintf(out, "        return -1;\n");
                fprintf(out, "    }\n");
                fprintf(out,
                        "    return %s(bindings->%s_ctx, bindings->%s_ops, command, reply_out);\n",
                        dispatch_name,
                        field_name,
                        field_name);
                fprintf(out, "}\n\n");

                fprintf(out,
                        "static int32_t %s(const void *bindings, const void *command, void *reply_out)\n{\n",
                        adapter_name);
                fprintf(out,
                        "    return %s((const %s *)bindings,\n"
                        "             (const %s *)command,\n"
                        "             (%s *)reply_out);\n",
                        wrapper_name,
                        exports_type,
                        command_type,
                        reply_type);
                fprintf(out, "}\n\n");

                fprintf(out,
                        "static int %s(const ThatchRegion *region, ThatchCursor *cursor, void *out)\n{\n",
                        read_adapter_name);
                fprintf(out,
                        "    return %s(region, cursor, (%s *)out);\n",
                        command_reader,
                        command_type);
                fprintf(out, "}\n\n");

                fprintf(out,
                        "static int %s(ThatchRegion *region, const void *value)\n{\n",
                        write_command_adapter_name);
                fprintf(out,
                        "    return %s(region, (const %s *)value);\n",
                        command_writer,
                        command_type);
                fprintf(out, "}\n\n");

                fprintf(out,
                        "static int %s(const ThatchRegion *region, ThatchCursor *cursor, void *out)\n{\n",
                        read_reply_adapter_name);
                fprintf(out,
                        "    return %s(region, cursor, (%s *)out);\n",
                        reply_reader,
                        reply_type);
                fprintf(out, "}\n\n");

                fprintf(out,
                        "static int %s(ThatchRegion *region, const void *value)\n{\n",
                        write_adapter_name);
                fprintf(out,
                        "    return %s(region, (const %s *)value);\n",
                        reply_writer,
                        reply_type);
                fprintf(out, "}\n\n");

                fprintf(out,
                        "static void %s(void *value)\n{\n",
                        dispose_adapter_name);
                fprintf(out,
                        "    %s((%s *)value);\n",
                        reply_dispose,
                        reply_type);
                fprintf(out, "}\n\n");
            }

            {
                char endpoints_symbol[MAX_NAME * 2];
                char endpoints_count_symbol[MAX_NAME * 2];

                wit_world_endpoint_array_symbol(reg,
                                                reg->worlds[i].package_full,
                                                reg->worlds[i].name,
                                                "export",
                                                endpoints_symbol,
                                                (int)sizeof(endpoints_symbol));
                wit_world_endpoint_count_symbol(reg,
                                                reg->worlds[i].package_full,
                                                reg->worlds[i].name,
                                                "export",
                                                endpoints_count_symbol,
                                                (int)sizeof(endpoints_count_symbol));
                fprintf(out, "const SapWitWorldEndpointDescriptor %s[] = {\n", endpoints_symbol);
                for (int j = 0; j < export_count; j++) {
                    char command_type[MAX_NAME * 2];
                    char reply_type[MAX_NAME * 2];
                    char ops_type[MAX_NAME * 2];
                    char adapter_name[MAX_NAME * 2];
                    char read_adapter_name[MAX_NAME * 3];
                    char write_command_adapter_name[MAX_NAME * 3];
                    char read_reply_adapter_name[MAX_NAME * 3];
                    char write_adapter_name[MAX_NAME * 3];
                    char dispose_adapter_name[MAX_NAME * 3];
                    char field_name[MAX_NAME];

                    wit_type_c_typename(reg,
                                        wit_variant_symbol_name(exports[j].command_variant),
                                        command_type,
                                        (int)sizeof(command_type));
                    wit_type_c_typename(reg,
                                        wit_variant_symbol_name(exports[j].reply_variant),
                                        reply_type,
                                        (int)sizeof(reply_type));
                    wit_dispatch_ops_typename(reg,
                                              wit_variant_symbol_name(exports[j].command_variant),
                                              ops_type,
                                              (int)sizeof(ops_type));
                    wit_world_endpoint_adapter_name(reg,
                                                    reg->worlds[i].package_full,
                                                    reg->worlds[i].name,
                                                    "export",
                                                    exports[j].item->name,
                                                    adapter_name,
                                                    (int)sizeof(adapter_name));
                    snprintf(read_adapter_name,
                             sizeof(read_adapter_name),
                             "%s_read_command",
                             adapter_name);
                    snprintf(write_command_adapter_name,
                             sizeof(write_command_adapter_name),
                             "%s_write_command",
                             adapter_name);
                    snprintf(read_reply_adapter_name,
                             sizeof(read_reply_adapter_name),
                             "%s_read_reply",
                             adapter_name);
                    snprintf(write_adapter_name,
                             sizeof(write_adapter_name),
                             "%s_write_reply",
                             adapter_name);
                    snprintf(dispose_adapter_name,
                             sizeof(dispose_adapter_name),
                             "%s_dispose_reply",
                             adapter_name);
                    wit_name_to_snake_ident(exports[j].item->name, field_name, (int)sizeof(field_name));

                    fprintf(out, "    {%s, %s, ",
                            wit_world_item_kind_macro(WIT_WORLD_ITEM_EXPORT),
                            wit_world_target_kind_macro(exports[j].item->target_kind));
                    emit_c_string_literal(out, reg->worlds[i].package_full);
                    fprintf(out, ", ");
                    emit_c_string_literal(out, reg->worlds[i].name);
                    fprintf(out, ", ");
                    emit_c_string_literal(out, exports[j].item->name);
                    fprintf(out, ", ");
                    emit_c_string_literal(out, exports[j].item->target_package_full);
                    fprintf(out, ", ");
                    emit_c_string_literal(out, exports[j].item->target_name);
                    fprintf(out, ", ");
                    emit_c_string_literal(out, wit_world_item_effective_target_package(reg, exports[j].item));
                    fprintf(out, ", ");
                    emit_c_string_literal(out, wit_world_item_effective_target_name(exports[j].item));
                    fprintf(out, ", ");
                    emit_c_string_literal(out, exports_type);
                    fprintf(out, ", ");
                    emit_c_string_literal(out, ops_type);
                    fprintf(out, ", ");
                    emit_c_string_literal(out, command_type);
                    fprintf(out, ", ");
                    emit_c_string_literal(out, reply_type);
                    fprintf(out,
                            ", sizeof(%s), sizeof(%s), offsetof(%s, %s_ctx), offsetof(%s, %s_ops), %s, %s, %s, %s, %s, %s},\n",
                            command_type,
                            reply_type,
                            exports_type,
                            field_name,
                            exports_type,
                            field_name,
                            read_adapter_name,
                            write_command_adapter_name,
                            read_reply_adapter_name,
                            write_adapter_name,
                            dispose_adapter_name,
                            adapter_name);
                }
                fprintf(out, "};\n");
                fprintf(out, "const uint32_t %s =\n", endpoints_count_symbol);
                fprintf(out, "    (uint32_t)(sizeof(%s) / sizeof(%s[0]));\n\n",
                        endpoints_symbol,
                        endpoints_symbol);
            }
        }
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
        fprintf(out, "    sap_wit_rt_memset(&scratch, 0, sizeof(scratch));\n");
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

    long fsize = 0;
    char *src = read_text_file(wit_path, &fsize);
    if (!src) {
        fprintf(stderr, "wit_codegen: cannot open %s\n", wit_path);
        return 1;
    }

    Scanner scanner;
    WitRegistry *reg = (WitRegistry *)malloc(sizeof(*reg));
    if (!reg) {
        free(src);
        return 1;
    }
    if (!init_registry_for_source(reg, wit_path, src, (int)fsize)) {
        free(reg);
        free(src);
        return 1;
    }

    scanner_init(&scanner, src, (int)fsize);
    if (!parse_wit(&scanner, reg)) {
        fprintf(stderr, "wit_codegen: parse failed at line %d col %d\n",
                scanner.line, scanner.col);
        free(reg);
        free(src);
        return 1;
    }
    if (!resolve_use_bindings(reg)) {
        free(reg);
        free(src);
        return 1;
    }
    if (!resolve_world_bindings(reg)) {
        free(reg);
        free(src);
        return 1;
    }
    if (!resolve_registry_symbol_scopes(reg)) {
        free(reg);
        free(src);
        return 1;
    }
    if (!lower_operations(reg)) {
        free(reg);
        free(src);
        return 1;
    }
    finalize_package_info(reg, wit_path);

    DbiEntry dbis[MAX_TYPES];
    int ndbi = extract_dbis(reg, dbis, MAX_TYPES);
    if (ndbi < 0) {
        fprintf(stderr, "wit_codegen: DBI extraction failed\n");
        free(reg);
        free(src);
        return 1;
    }
    if (header_path) {
        FILE *hdr = fopen(header_path, "w");
        if (!hdr) {
            fprintf(stderr, "wit_codegen: cannot create %s\n", header_path);
            free(reg);
            free(src); return 1;
        }
        emit_header(hdr, reg, dbis, ndbi, wit_path, header_path);
        fclose(hdr);
    }

    if (source_path && header_path) {
        FILE *csrc = fopen(source_path, "w");
        if (!csrc) {
            fprintf(stderr, "wit_codegen: cannot create %s\n", source_path);
            free(reg);
            free(src); return 1;
        }
        emit_source(csrc, reg, dbis, ndbi, wit_path, header_path);
        fclose(csrc);
    }

    if (manifest_path) {
        FILE *manifest = fopen(manifest_path, "w");
        if (!manifest) {
            fprintf(stderr, "wit_codegen: cannot create %s\n", manifest_path);
            free(reg);
            free(src);
            return 1;
        }
        emit_manifest(manifest, reg, dbis, ndbi, wit_path);
        fclose(manifest);
    }

    printf("wit_codegen: PASS (interfaces=%d worlds=%d records=%d variants=%d enums=%d flags=%d "
           "aliases=%d resources=%d dbis=%d)\n",
           reg->interface_count, reg->world_count,
           reg->record_count, reg->variant_count, reg->enum_count,
           reg->flags_count, reg->alias_count, reg->resource_count, ndbi);

    free(reg);
    free(src);
    return 0;
}
