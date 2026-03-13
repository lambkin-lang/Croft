#ifndef CROFT_JSON_VIEWER_STATE_H
#define CROFT_JSON_VIEWER_STATE_H

#include "croft/json_viewer_core.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CROFT_JSON_VIEWER_DOC_STORAGE_CAP 16384u
#define CROFT_JSON_VIEWER_RENDER_CAP 16384u
#define CROFT_JSON_VIEWER_PATH_STORAGE_CAP 4096u
#define CROFT_JSON_VIEWER_VISIBLE_PATH_STORAGE_CAP 8192u
#define CROFT_JSON_VIEWER_PATH_CAP 128u
#define CROFT_JSON_VIEWER_LINE_CAP 512u
#define CROFT_JSON_VIEWER_PATH_LABEL_CAP 288u

typedef struct {
    CroftJsonViewerDocument doc;
    uint8_t doc_storage[CROFT_JSON_VIEWER_DOC_STORAGE_CAP];
    char rendered[CROFT_JSON_VIEWER_RENDER_CAP];
    uint32_t line_offsets[CROFT_JSON_VIEWER_LINE_CAP];
    uint32_t line_lengths[CROFT_JSON_VIEWER_LINE_CAP];
    uint32_t line_count;
    char path_storage[CROFT_JSON_VIEWER_PATH_STORAGE_CAP];
    const char *paths[CROFT_JSON_VIEWER_PATH_CAP];
    uint32_t path_count;
    char visible_path_storage[CROFT_JSON_VIEWER_VISIBLE_PATH_STORAGE_CAP];
    const char *visible_line_paths[CROFT_JSON_VIEWER_LINE_CAP];
    uint32_t visible_line_count;
    uint32_t expanded_indices[CROFT_JSON_VIEWER_PATH_CAP];
    uint32_t expanded_count;
    uint32_t selected_path_index;
    float scroll_y;
    uint32_t error_position;
} CroftJsonViewerState;

void croft_json_viewer_state_reset(CroftJsonViewerState *state);

int croft_json_viewer_state_load(CroftJsonViewerState *state,
                                 const uint8_t *json,
                                 uint32_t json_len);

int croft_json_viewer_state_select_path_index(CroftJsonViewerState *state, uint32_t index);
int croft_json_viewer_state_toggle_path_index(CroftJsonViewerState *state, uint32_t index);
int croft_json_viewer_state_set_path_expanded(CroftJsonViewerState *state,
                                              uint32_t index,
                                              uint8_t expanded);
int croft_json_viewer_state_path_is_expanded(const CroftJsonViewerState *state, uint32_t index);
int croft_json_viewer_state_path_is_expandable(const CroftJsonViewerState *state, uint32_t index);

int croft_json_viewer_state_select_prev(CroftJsonViewerState *state);
int croft_json_viewer_state_select_next(CroftJsonViewerState *state);
int croft_json_viewer_state_toggle_selected(CroftJsonViewerState *state);
int croft_json_viewer_state_set_selected_expanded(CroftJsonViewerState *state, uint8_t expanded);
int croft_json_viewer_state_selected_is_expanded(const CroftJsonViewerState *state);

const char *croft_json_viewer_state_selected_path(const CroftJsonViewerState *state);
uint32_t croft_json_viewer_state_line_count(const CroftJsonViewerState *state);
int croft_json_viewer_state_line(const CroftJsonViewerState *state,
                                 uint32_t index,
                                 const char **text_out,
                                 uint32_t *len_out);
int croft_json_viewer_state_content_line_index(const CroftJsonViewerState *state,
                                               float y_from_content_top,
                                               float line_height,
                                               uint32_t *line_index_out);
int croft_json_viewer_state_line_path_index(const CroftJsonViewerState *state,
                                            uint32_t line_index,
                                            uint32_t *path_index_out);
int croft_json_viewer_state_select_line(CroftJsonViewerState *state, uint32_t line_index);
int croft_json_viewer_state_selected_line(const CroftJsonViewerState *state,
                                          uint32_t *line_index_out);

float croft_json_viewer_state_max_scroll(const CroftJsonViewerState *state,
                                         float line_height,
                                         float viewport_height);
void croft_json_viewer_state_clamp_scroll(CroftJsonViewerState *state,
                                          float line_height,
                                          float viewport_height);
void croft_json_viewer_state_scroll_pixels(CroftJsonViewerState *state,
                                           float delta_pixels,
                                           float line_height,
                                           float viewport_height);
void croft_json_viewer_state_scroll_milli(CroftJsonViewerState *state,
                                          int32_t y_milli,
                                          float line_height,
                                          float viewport_height);

int croft_json_viewer_state_handle_key(CroftJsonViewerState *state,
                                       int32_t key,
                                       int32_t action,
                                       float line_height,
                                       float viewport_height);

int croft_json_viewer_state_format_path_label(const CroftJsonViewerState *state,
                                              uint32_t index,
                                              char *out,
                                              uint32_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_JSON_VIEWER_STATE_H */
