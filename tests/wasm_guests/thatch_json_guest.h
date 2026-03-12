#ifndef CROFT_THATCH_JSON_GUEST_H
#define CROFT_THATCH_JSON_GUEST_H

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

#ifdef __cplusplus
}
#endif

#endif /* CROFT_THATCH_JSON_GUEST_H */
