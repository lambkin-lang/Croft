#include "croft/json_viewer_state.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define ASSERT_JSON_VIEWER(cond)                                            \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "    ASSERT failed: %s (%s:%d)\n",              \
                    #cond, __FILE__, __LINE__);                             \
            return 1;                                                       \
        }                                                                   \
    } while (0)

static const uint8_t k_test_json[] =
    "{"
    "\"project\":\"Croft\","
    "\"features\":{\"solver\":true,\"runtime\":\"wasm3-windowed\"},"
    "\"items\":[\"alpha\",{\"nested\":true}],"
    "\"empty\":{},"
    "\"notes\":{\"viewer\":\"read-only\"}"
    "}";

static uint32_t find_path_index(const CroftJsonViewerState *state, const char *path)
{
    uint32_t i;

    if (!state || !path) {
        return UINT_MAX;
    }
    for (i = 0u; i < state->path_count; i++) {
        if (strcmp(state->paths[i], path) == 0) {
            return i;
        }
    }
    return UINT_MAX;
}

int test_json_viewer_state_path_selection_and_expansion(void)
{
    CroftJsonViewerState state;
    uint32_t root_index;
    uint32_t items_index;
    uint32_t empty_index;
    uint32_t line_index = UINT_MAX;
    char label[CROFT_JSON_VIEWER_PATH_LABEL_CAP];

    ASSERT_JSON_VIEWER(croft_json_viewer_state_load(&state,
                                                    k_test_json,
                                                    (uint32_t)(sizeof(k_test_json) - 1u)) == ERR_OK);
    ASSERT_JSON_VIEWER(strcmp(croft_json_viewer_state_selected_path(&state), ".features") == 0);

    root_index = find_path_index(&state, ".");
    items_index = find_path_index(&state, ".items");
    empty_index = find_path_index(&state, ".empty");

    ASSERT_JSON_VIEWER(root_index != UINT_MAX);
    ASSERT_JSON_VIEWER(items_index != UINT_MAX);
    ASSERT_JSON_VIEWER(empty_index != UINT_MAX);
    ASSERT_JSON_VIEWER(croft_json_viewer_state_path_is_expandable(&state, root_index));
    ASSERT_JSON_VIEWER(croft_json_viewer_state_path_is_expandable(&state, items_index));
    ASSERT_JSON_VIEWER(!croft_json_viewer_state_path_is_expandable(&state, empty_index));
    ASSERT_JSON_VIEWER(find_path_index(&state, ".project") == UINT_MAX);

    ASSERT_JSON_VIEWER(croft_json_viewer_state_select_path_index(&state, items_index) == ERR_OK);
    ASSERT_JSON_VIEWER(strcmp(croft_json_viewer_state_selected_path(&state), ".items") == 0);
    ASSERT_JSON_VIEWER(croft_json_viewer_state_toggle_path_index(&state, items_index) == ERR_OK);
    ASSERT_JSON_VIEWER(croft_json_viewer_state_path_is_expanded(&state, items_index));

    ASSERT_JSON_VIEWER(croft_json_viewer_state_toggle_path_index(&state, empty_index) == ERR_OK);
    ASSERT_JSON_VIEWER(!croft_json_viewer_state_path_is_expanded(&state, empty_index));
    ASSERT_JSON_VIEWER(croft_json_viewer_state_select_path_index(&state, state.path_count) == ERR_RANGE);
    ASSERT_JSON_VIEWER(croft_json_viewer_state_content_line_index(&state, 10.0f, 22.0f, &line_index)
                       == ERR_OK);
    ASSERT_JSON_VIEWER(line_index == 0u);
    ASSERT_JSON_VIEWER(croft_json_viewer_state_content_line_index(&state, 50.0f, 22.0f, &line_index)
                       == ERR_OK);
    ASSERT_JSON_VIEWER(line_index == 2u);
    ASSERT_JSON_VIEWER(croft_json_viewer_state_select_line(&state, line_index) == ERR_OK);
    ASSERT_JSON_VIEWER(strcmp(croft_json_viewer_state_selected_path(&state), ".features") == 0);
    ASSERT_JSON_VIEWER(croft_json_viewer_state_selected_line(&state, &line_index) == ERR_OK);
    ASSERT_JSON_VIEWER(line_index == 2u);
    ASSERT_JSON_VIEWER(croft_json_viewer_state_line_path_index(&state, 1u, &line_index)
                       == ERR_NOT_FOUND);

    ASSERT_JSON_VIEWER(croft_json_viewer_state_format_path_label(&state,
                                                                 root_index,
                                                                 label,
                                                                 sizeof(label)) == ERR_OK);
    ASSERT_JSON_VIEWER(strcmp(label, "- root") == 0);
    return 0;
}
