#include "croft/json_viewer_state.h"
#include "croft/wit_runtime_support.h"

enum {
    CROFT_JSON_VIEWER_KEY_RELEASE = 0,
    CROFT_JSON_VIEWER_KEY_ENTER = 257,
    CROFT_JSON_VIEWER_KEY_RIGHT = 262,
    CROFT_JSON_VIEWER_KEY_LEFT = 263,
    CROFT_JSON_VIEWER_KEY_DOWN = 264,
    CROFT_JSON_VIEWER_KEY_UP = 265,
    CROFT_JSON_VIEWER_KEY_HOME = 268,
    CROFT_JSON_VIEWER_KEY_END = 269,
    CROFT_JSON_VIEWER_KEY_SPACE = 32,
    CROFT_JSON_VIEWER_KEY_KP_ENTER = 335
};

static int croft_json_viewer_state_is_expanded_index(const CroftJsonViewerState *state, uint32_t index)
{
    uint32_t i;

    if (!state) {
        return 0;
    }
    for (i = 0u; i < state->expanded_count; i++) {
        if (state->expanded_indices[i] == index) {
            return 1;
        }
    }
    return 0;
}

static int croft_json_viewer_state_text_equals(const char *lhs, const char *rhs)
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

static int croft_json_viewer_state_path_is_expandable_index(const CroftJsonViewerState *state, uint32_t index)
{
    const char *path;
    uint32_t path_len;
    uint32_t i;

    if (!state || index >= state->path_count) {
        return 0;
    }

    path = state->paths[index];
    if (!path) {
        return 0;
    }
    path_len = (uint32_t)sap_wit_rt_strlen(path);
    for (i = 0u; i < state->path_count; i++) {
        const char *candidate = state->paths[i];
        uint32_t j = 0u;

        if (i == index || !candidate) {
            continue;
        }
        while (j < path_len && candidate[j] == path[j]) {
            j++;
        }
        if (j != path_len) {
            continue;
        }
        if (path_len == 1u && path[0] == '.' && candidate[0] == '.' && candidate[1] != '\0') {
            return 1;
        }
        if (candidate[path_len] == '.' || candidate[path_len] == '[') {
            return 1;
        }
    }
    return 0;
}

static int croft_json_viewer_state_find_path_index(const CroftJsonViewerState *state,
                                                   const char *path,
                                                   uint32_t *index_out)
{
    uint32_t i;

    if (!state || !path || !index_out) {
        return ERR_INVALID;
    }
    for (i = 0u; i < state->path_count; i++) {
        if (state->paths[i] && croft_json_viewer_state_text_equals(state->paths[i], path)) {
            *index_out = i;
            return ERR_OK;
        }
    }
    return ERR_NOT_FOUND;
}

static int croft_json_viewer_state_trim_parent_path(char *path)
{
    uint32_t len;
    uint32_t i;

    if (!path || path[0] == '\0' || (path[0] == '.' && path[1] == '\0')) {
        return 0;
    }
    len = (uint32_t)sap_wit_rt_strlen(path);
    for (i = len; i > 0u; i--) {
        if (path[i - 1u] == '[') {
            path[i - 1u] = '\0';
            return 1;
        }
        if (path[i - 1u] == '.') {
            if (i - 1u == 0u) {
                path[1] = '\0';
            } else {
                path[i - 1u] = '\0';
            }
            return 1;
        }
    }
    return 0;
}

static int croft_json_viewer_state_find_selectable_index_for_path(const CroftJsonViewerState *state,
                                                                  const char *path,
                                                                  uint32_t *index_out)
{
    char trimmed[256];
    uint32_t len;
    int rc;

    if (!state || !path || !index_out) {
        return ERR_INVALID;
    }
    rc = croft_json_viewer_state_find_path_index(state, path, index_out);
    if (rc == ERR_OK) {
        return rc;
    }

    len = (uint32_t)sap_wit_rt_strlen(path);
    if (len + 1u > sizeof(trimmed)) {
        return ERR_FULL;
    }
    sap_wit_rt_memcpy(trimmed, path, len + 1u);
    while (croft_json_viewer_state_trim_parent_path(trimmed)) {
        rc = croft_json_viewer_state_find_path_index(state, trimmed, index_out);
        if (rc == ERR_OK) {
            return rc;
        }
    }
    return ERR_NOT_FOUND;
}

static int croft_json_viewer_state_rebuild_lines(CroftJsonViewerState *state)
{
    uint32_t i;
    uint32_t start = 0u;
    uint32_t len = 0u;

    if (!state) {
        return ERR_INVALID;
    }

    state->line_count = 0u;
    len = (uint32_t)sap_wit_rt_strlen(state->rendered);
    for (i = 0u; i <= len; i++) {
        if (state->rendered[i] == '\n' || state->rendered[i] == '\0') {
            if (i == len && start == len) {
                break;
            }
            if (state->line_count >= CROFT_JSON_VIEWER_LINE_CAP) {
                return ERR_FULL;
            }
            state->line_offsets[state->line_count] = start;
            state->line_lengths[state->line_count] = i - start;
            state->line_count++;
            start = i + 1u;
        }
    }
    return ERR_OK;
}

static int croft_json_viewer_state_parse_visible_line_paths(CroftJsonViewerState *state)
{
    uint32_t i;
    uint32_t start = 0u;
    uint32_t len = 0u;

    if (!state) {
        return ERR_INVALID;
    }

    state->visible_line_count = 0u;
    len = (uint32_t)sap_wit_rt_strlen(state->visible_path_storage);
    for (i = 0u; i <= len; i++) {
        if (state->visible_path_storage[i] == '\n' || state->visible_path_storage[i] == '\0') {
            if (i > start) {
                if (state->visible_line_count >= CROFT_JSON_VIEWER_LINE_CAP) {
                    return ERR_FULL;
                }
                state->visible_path_storage[i] = '\0';
                state->visible_line_paths[state->visible_line_count++] =
                    state->visible_path_storage + start;
            }
            start = i + 1u;
        }
    }
    if (state->visible_line_count != state->line_count) {
        return ERR_RANGE;
    }
    return state->visible_line_count > 0u ? ERR_OK : ERR_NOT_FOUND;
}

static int croft_json_viewer_state_rebuild_render(CroftJsonViewerState *state)
{
    const char *expanded_paths[CROFT_JSON_VIEWER_PATH_CAP];
    uint32_t i;
    int rc;

    if (!state) {
        return ERR_INVALID;
    }
    if (state->expanded_count > CROFT_JSON_VIEWER_PATH_CAP) {
        return ERR_RANGE;
    }

    for (i = 0u; i < state->expanded_count; i++) {
        if (state->expanded_indices[i] >= state->path_count) {
            return ERR_RANGE;
        }
        expanded_paths[i] = state->paths[state->expanded_indices[i]];
    }
    rc = croft_json_viewer_render_collapsed_view(&state->doc,
                                                 expanded_paths,
                                                 state->expanded_count,
                                                 state->rendered,
                                                 sizeof(state->rendered));
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_json_viewer_render_visible_paths(&state->doc,
                                                expanded_paths,
                                                state->expanded_count,
                                                state->visible_path_storage,
                                                sizeof(state->visible_path_storage));
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_json_viewer_state_rebuild_lines(state);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_json_viewer_state_parse_visible_line_paths(state);
}

static int croft_json_viewer_state_parse_paths(CroftJsonViewerState *state)
{
    uint32_t i;
    uint32_t start = 0u;
    uint32_t len = 0u;

    if (!state) {
        return ERR_INVALID;
    }

    state->path_count = 0u;
    len = (uint32_t)sap_wit_rt_strlen(state->path_storage);
    for (i = 0u; i <= len; i++) {
        if (state->path_storage[i] == '\n' || state->path_storage[i] == '\0') {
            if (i > start) {
                if (state->path_count >= CROFT_JSON_VIEWER_PATH_CAP) {
                    return ERR_FULL;
                }
                state->path_storage[i] = '\0';
                state->paths[state->path_count++] = state->path_storage + start;
            }
            start = i + 1u;
        }
    }
    return state->path_count > 0u ? ERR_OK : ERR_NOT_FOUND;
}

static int croft_json_viewer_state_remove_expanded(CroftJsonViewerState *state, uint32_t index)
{
    uint32_t i;

    if (!state) {
        return ERR_INVALID;
    }
    for (i = 0u; i < state->expanded_count; i++) {
        if (state->expanded_indices[i] == index) {
            while (i + 1u < state->expanded_count) {
                state->expanded_indices[i] = state->expanded_indices[i + 1u];
                i++;
            }
            state->expanded_count--;
            return ERR_OK;
        }
    }
    return ERR_NOT_FOUND;
}

static int croft_json_viewer_state_append_expanded(CroftJsonViewerState *state, uint32_t index)
{
    if (!state || index >= state->path_count) {
        return ERR_INVALID;
    }
    if (!croft_json_viewer_state_path_is_expandable_index(state, index)) {
        return ERR_OK;
    }
    if (croft_json_viewer_state_is_expanded_index(state, index)) {
        return ERR_OK;
    }
    if (state->expanded_count >= CROFT_JSON_VIEWER_PATH_CAP) {
        return ERR_FULL;
    }
    state->expanded_indices[state->expanded_count++] = index;
    return ERR_OK;
}

static int croft_json_viewer_put_text(char *out,
                                      uint32_t out_cap,
                                      uint32_t *len_io,
                                      const char *text)
{
    uint32_t i = 0u;

    if (!out || !len_io || !text) {
        return ERR_INVALID;
    }
    while (text[i] != '\0') {
        if (*len_io + 1u >= out_cap) {
            return ERR_FULL;
        }
        out[*len_io] = text[i];
        (*len_io)++;
        i++;
    }
    out[*len_io] = '\0';
    return ERR_OK;
}

void croft_json_viewer_state_reset(CroftJsonViewerState *state)
{
    if (!state) {
        return;
    }
    sap_wit_rt_memset(state, 0, sizeof(*state));
}

int croft_json_viewer_state_load(CroftJsonViewerState *state,
                                 const uint8_t *json,
                                 uint32_t json_len)
{
    int rc;

    if (!state || (!json && json_len > 0u)) {
        return ERR_INVALID;
    }

    croft_json_viewer_state_reset(state);
    rc = croft_json_viewer_parse(&state->doc,
                                 state->doc_storage,
                                 sizeof(state->doc_storage),
                                 json,
                                 json_len,
                                 &state->error_position);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_json_viewer_render_paths(&state->doc,
                                        state->path_storage,
                                        sizeof(state->path_storage));
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_json_viewer_state_parse_paths(state);
    if (rc != ERR_OK) {
        return rc;
    }
    state->expanded_count = 0u;
    rc = croft_json_viewer_state_append_expanded(state, 0u);
    if (rc != ERR_OK) {
        return rc;
    }
    state->selected_path_index = state->path_count > 1u ? 1u : 0u;
    return croft_json_viewer_state_rebuild_render(state);
}

int croft_json_viewer_state_select_path_index(CroftJsonViewerState *state, uint32_t index)
{
    if (!state || state->path_count == 0u) {
        return ERR_INVALID;
    }
    if (index >= state->path_count) {
        return ERR_RANGE;
    }
    state->selected_path_index = index;
    return ERR_OK;
}

int croft_json_viewer_state_toggle_path_index(CroftJsonViewerState *state, uint32_t index)
{
    int rc;

    rc = croft_json_viewer_state_select_path_index(state, index);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_json_viewer_state_toggle_selected(state);
}

int croft_json_viewer_state_set_path_expanded(CroftJsonViewerState *state,
                                              uint32_t index,
                                              uint8_t expanded)
{
    int rc;

    rc = croft_json_viewer_state_select_path_index(state, index);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_json_viewer_state_set_selected_expanded(state, expanded);
}

int croft_json_viewer_state_path_is_expanded(const CroftJsonViewerState *state, uint32_t index)
{
    if (!state || index >= state->path_count) {
        return 0;
    }
    return croft_json_viewer_state_is_expanded_index(state, index);
}

int croft_json_viewer_state_path_is_expandable(const CroftJsonViewerState *state, uint32_t index)
{
    return croft_json_viewer_state_path_is_expandable_index(state, index);
}

int croft_json_viewer_state_select_prev(CroftJsonViewerState *state)
{
    if (!state || state->path_count == 0u) {
        return ERR_INVALID;
    }
    if (state->selected_path_index == 0u) {
        state->selected_path_index = state->path_count - 1u;
    } else {
        state->selected_path_index--;
    }
    return ERR_OK;
}

int croft_json_viewer_state_select_next(CroftJsonViewerState *state)
{
    if (!state || state->path_count == 0u) {
        return ERR_INVALID;
    }
    state->selected_path_index = (state->selected_path_index + 1u) % state->path_count;
    return ERR_OK;
}

int croft_json_viewer_state_toggle_selected(CroftJsonViewerState *state)
{
    int rc;

    if (!state || state->path_count == 0u) {
        return ERR_INVALID;
    }
    if (!croft_json_viewer_state_path_is_expandable_index(state, state->selected_path_index)) {
        return ERR_OK;
    }
    if (croft_json_viewer_state_is_expanded_index(state, state->selected_path_index)) {
        rc = croft_json_viewer_state_remove_expanded(state, state->selected_path_index);
        if (rc != ERR_OK) {
            return rc;
        }
    } else {
        rc = croft_json_viewer_state_append_expanded(state, state->selected_path_index);
        if (rc != ERR_OK) {
            return rc;
        }
    }
    return croft_json_viewer_state_rebuild_render(state);
}

int croft_json_viewer_state_set_selected_expanded(CroftJsonViewerState *state, uint8_t expanded)
{
    int rc;

    if (!state || state->path_count == 0u) {
        return ERR_INVALID;
    }
    if (!croft_json_viewer_state_path_is_expandable_index(state, state->selected_path_index)) {
        return ERR_OK;
    }
    if (expanded) {
        rc = croft_json_viewer_state_append_expanded(state, state->selected_path_index);
    } else {
        rc = croft_json_viewer_state_remove_expanded(state, state->selected_path_index);
        if (rc == ERR_NOT_FOUND) {
            rc = ERR_OK;
        }
    }
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_json_viewer_state_rebuild_render(state);
}

int croft_json_viewer_state_selected_is_expanded(const CroftJsonViewerState *state)
{
    if (!state || state->path_count == 0u) {
        return 0;
    }
    return croft_json_viewer_state_path_is_expanded(state, state->selected_path_index);
}

const char *croft_json_viewer_state_selected_path(const CroftJsonViewerState *state)
{
    if (!state || state->path_count == 0u || state->selected_path_index >= state->path_count) {
        return "";
    }
    return state->paths[state->selected_path_index];
}

uint32_t croft_json_viewer_state_line_count(const CroftJsonViewerState *state)
{
    return state ? state->line_count : 0u;
}

int croft_json_viewer_state_line(const CroftJsonViewerState *state,
                                 uint32_t index,
                                 const char **text_out,
                                 uint32_t *len_out)
{
    if (!state || !text_out || !len_out || index >= state->line_count) {
        return ERR_INVALID;
    }
    *text_out = state->rendered + state->line_offsets[index];
    *len_out = state->line_lengths[index];
    return ERR_OK;
}

int croft_json_viewer_state_content_line_index(const CroftJsonViewerState *state,
                                               float y_from_content_top,
                                               float line_height,
                                               uint32_t *line_index_out)
{
    float absolute_y;
    uint32_t index;

    if (!state || !line_index_out || line_height <= 0.0f || y_from_content_top < 0.0f) {
        return ERR_INVALID;
    }
    absolute_y = state->scroll_y + y_from_content_top;
    if (absolute_y < 0.0f) {
        return ERR_INVALID;
    }
    index = (uint32_t)(absolute_y / line_height);
    if (index >= state->line_count) {
        return ERR_RANGE;
    }
    *line_index_out = index;
    return ERR_OK;
}

int croft_json_viewer_state_line_path_index(const CroftJsonViewerState *state,
                                            uint32_t line_index,
                                            uint32_t *path_index_out)
{
    if (!state || !path_index_out || line_index >= state->visible_line_count) {
        return ERR_INVALID;
    }
    return croft_json_viewer_state_find_path_index(state,
                                                   state->visible_line_paths[line_index],
                                                   path_index_out);
}

int croft_json_viewer_state_select_line(CroftJsonViewerState *state, uint32_t line_index)
{
    uint32_t path_index = 0u;
    int rc;

    if (!state || line_index >= state->visible_line_count) {
        return ERR_INVALID;
    }
    rc = croft_json_viewer_state_find_selectable_index_for_path(state,
                                                                state->visible_line_paths[line_index],
                                                                &path_index);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_json_viewer_state_select_path_index(state, path_index);
}

int croft_json_viewer_state_selected_line(const CroftJsonViewerState *state,
                                          uint32_t *line_index_out)
{
    const char *selected_path;
    uint32_t i;

    if (!state || !line_index_out) {
        return ERR_INVALID;
    }
    selected_path = croft_json_viewer_state_selected_path(state);
    for (i = 0u; i < state->visible_line_count; i++) {
        if (state->visible_line_paths[i]
                && croft_json_viewer_state_text_equals(state->visible_line_paths[i], selected_path)) {
            *line_index_out = i;
            return ERR_OK;
        }
    }
    return ERR_NOT_FOUND;
}

float croft_json_viewer_state_max_scroll(const CroftJsonViewerState *state,
                                         float line_height,
                                         float viewport_height)
{
    float content_height;

    if (!state || line_height <= 0.0f || viewport_height <= 0.0f) {
        return 0.0f;
    }
    content_height = (float)state->line_count * line_height;
    return content_height > viewport_height ? content_height - viewport_height : 0.0f;
}

void croft_json_viewer_state_clamp_scroll(CroftJsonViewerState *state,
                                          float line_height,
                                          float viewport_height)
{
    float max_scroll;

    if (!state) {
        return;
    }
    max_scroll = croft_json_viewer_state_max_scroll(state, line_height, viewport_height);
    if (state->scroll_y < 0.0f) {
        state->scroll_y = 0.0f;
    } else if (state->scroll_y > max_scroll) {
        state->scroll_y = max_scroll;
    }
}

void croft_json_viewer_state_scroll_pixels(CroftJsonViewerState *state,
                                           float delta_pixels,
                                           float line_height,
                                           float viewport_height)
{
    if (!state) {
        return;
    }
    state->scroll_y += delta_pixels;
    croft_json_viewer_state_clamp_scroll(state, line_height, viewport_height);
}

void croft_json_viewer_state_scroll_milli(CroftJsonViewerState *state,
                                          int32_t y_milli,
                                          float line_height,
                                          float viewport_height)
{
    float delta;

    if (!state || y_milli == 0) {
        return;
    }
    delta = -((float)y_milli / 1000.0f) * line_height * 3.0f;
    croft_json_viewer_state_scroll_pixels(state, delta, line_height, viewport_height);
}

int croft_json_viewer_state_handle_key(CroftJsonViewerState *state,
                                       int32_t key,
                                       int32_t action,
                                       float line_height,
                                       float viewport_height)
{
    if (!state || action == CROFT_JSON_VIEWER_KEY_RELEASE) {
        return 0;
    }

    switch (key) {
        case CROFT_JSON_VIEWER_KEY_UP:
            return croft_json_viewer_state_select_prev(state) == ERR_OK;
        case CROFT_JSON_VIEWER_KEY_DOWN:
            return croft_json_viewer_state_select_next(state) == ERR_OK;
        case CROFT_JSON_VIEWER_KEY_LEFT:
            return croft_json_viewer_state_set_selected_expanded(state, 0u) == ERR_OK;
        case CROFT_JSON_VIEWER_KEY_RIGHT:
            return croft_json_viewer_state_set_selected_expanded(state, 1u) == ERR_OK;
        case CROFT_JSON_VIEWER_KEY_ENTER:
        case CROFT_JSON_VIEWER_KEY_KP_ENTER:
        case CROFT_JSON_VIEWER_KEY_SPACE:
            return croft_json_viewer_state_toggle_selected(state) == ERR_OK;
        case CROFT_JSON_VIEWER_KEY_HOME:
            state->scroll_y = 0.0f;
            return 1;
        case CROFT_JSON_VIEWER_KEY_END:
            state->scroll_y = croft_json_viewer_state_max_scroll(state,
                                                                 line_height,
                                                                 viewport_height);
            return 1;
        default:
            return 0;
    }
}

int croft_json_viewer_state_format_path_label(const CroftJsonViewerState *state,
                                              uint32_t index,
                                              char *out,
                                              uint32_t out_cap)
{
    const char *path;
    uint32_t len = 0u;
    int rc;

    if (!state || !out || out_cap == 0u || index >= state->path_count) {
        return ERR_INVALID;
    }

    path = state->paths[index];
    out[0] = '\0';
    rc = croft_json_viewer_put_text(out,
                                    out_cap,
                                    &len,
                                    croft_json_viewer_state_path_is_expandable_index(state, index)
                                        ? (croft_json_viewer_state_is_expanded_index(state, index)
                                               ? "- "
                                               : "+ ")
                                        : "  ");
    if (rc == ERR_OK) {
        rc = croft_json_viewer_put_text(out,
                                        out_cap,
                                        &len,
                                        path && path[0] == '.' && path[1] == '\0' ? "root" : path);
    }
    return rc;
}
