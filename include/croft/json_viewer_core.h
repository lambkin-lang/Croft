#ifndef CROFT_JSON_VIEWER_CORE_H
#define CROFT_JSON_VIEWER_CORE_H

#include "sapling/err.h"
#include "sapling/thatch.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ThatchRegion region;
    ThatchCursor root;
} CroftGuestJsonDocument;

typedef CroftGuestJsonDocument CroftJsonViewerDocument;

int croft_guest_json_parse(CroftGuestJsonDocument *doc,
                           uint8_t *storage,
                           uint32_t storage_cap,
                           const uint8_t *json,
                           uint32_t json_len,
                           uint32_t *err_pos_out);

int croft_guest_json_render_collapsed_view(const CroftGuestJsonDocument *doc,
                                           const char *const *expanded_paths,
                                           uint32_t expanded_path_count,
                                           char *out,
                                           uint32_t out_cap);

int croft_guest_json_render_cursor_paths(const CroftGuestJsonDocument *doc,
                                         char *out,
                                         uint32_t out_cap);

int croft_guest_json_render_visible_paths(const CroftGuestJsonDocument *doc,
                                          const char *const *expanded_paths,
                                          uint32_t expanded_path_count,
                                          char *out,
                                          uint32_t out_cap);

static inline int croft_json_viewer_parse(CroftJsonViewerDocument *doc,
                                          uint8_t *storage,
                                          uint32_t storage_cap,
                                          const uint8_t *json,
                                          uint32_t json_len,
                                          uint32_t *err_pos_out)
{
    return croft_guest_json_parse(doc, storage, storage_cap, json, json_len, err_pos_out);
}

static inline int croft_json_viewer_render_collapsed_view(const CroftJsonViewerDocument *doc,
                                                          const char *const *expanded_paths,
                                                          uint32_t expanded_path_count,
                                                          char *out,
                                                          uint32_t out_cap)
{
    return croft_guest_json_render_collapsed_view(doc,
                                                  expanded_paths,
                                                  expanded_path_count,
                                                  out,
                                                  out_cap);
}

static inline int croft_json_viewer_render_paths(const CroftJsonViewerDocument *doc,
                                                 char *out,
                                                 uint32_t out_cap)
{
    return croft_guest_json_render_cursor_paths(doc, out, out_cap);
}

static inline int croft_json_viewer_render_visible_paths(const CroftJsonViewerDocument *doc,
                                                         const char *const *expanded_paths,
                                                         uint32_t expanded_path_count,
                                                         char *out,
                                                         uint32_t out_cap)
{
    return croft_guest_json_render_visible_paths(doc,
                                                 expanded_paths,
                                                 expanded_path_count,
                                                 out,
                                                 out_cap);
}

#ifdef __cplusplus
}
#endif

#endif /* CROFT_JSON_VIEWER_CORE_H */
