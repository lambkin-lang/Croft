#include "croft/scene.h"
#include "croft/editor_document.h"

#include <stdio.h>
#include <string.h>

#define ASSERT_A11Y(cond)                                              \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "    ASSERT failed: %s (%s:%d)\n",         \
                    #cond, __FILE__, __LINE__);                        \
            return 1;                                                  \
        }                                                              \
    } while (0)

typedef struct test_scene_a11y_node {
    croft_scene_a11y_handle handle;
    croft_scene_a11y_role role;
    float x;
    float y;
    float width;
    float height;
    char label[256];
    char value[256];
    uint32_t update_count;
    uint32_t label_update_count;
    uint32_t value_update_count;
} test_scene_a11y_node;

static test_scene_a11y_node g_a11y_nodes[16];
static uint32_t g_a11y_node_count = 0u;
static croft_scene_a11y_handle g_last_parent = (croft_scene_a11y_handle)0u;
static croft_scene_a11y_handle g_last_child = (croft_scene_a11y_handle)0u;

static test_scene_a11y_node* test_scene_a11y_find(croft_scene_a11y_handle handle)
{
    uint32_t index;

    for (index = 0u; index < g_a11y_node_count; index++) {
        if (g_a11y_nodes[index].handle == handle) {
            return &g_a11y_nodes[index];
        }
    }
    return NULL;
}

static int32_t test_scene_a11y_open(void* userdata)
{
    (void)userdata;
    return 0;
}

static void test_scene_a11y_close(void* userdata)
{
    (void)userdata;
}

static croft_scene_a11y_handle test_scene_a11y_create_node(void* userdata,
                                                           croft_scene_a11y_role role,
                                                           const croft_scene_a11y_node_config* config)
{
    test_scene_a11y_node* node;

    (void)userdata;
    if (!config || g_a11y_node_count >= (sizeof(g_a11y_nodes) / sizeof(g_a11y_nodes[0]))) {
        return (croft_scene_a11y_handle)0u;
    }

    node = &g_a11y_nodes[g_a11y_node_count++];
    memset(node, 0, sizeof(*node));
    node->handle = (croft_scene_a11y_handle)(uintptr_t)g_a11y_node_count;
    node->role = role;
    node->x = config->x;
    node->y = config->y;
    node->width = config->width;
    node->height = config->height;
    if (config->label) {
        snprintf(node->label, sizeof(node->label), "%s", config->label);
    }
    return node->handle;
}

static void test_scene_a11y_add_child(void* userdata,
                                      croft_scene_a11y_handle parent,
                                      croft_scene_a11y_handle child)
{
    (void)userdata;
    g_last_parent = parent;
    g_last_child = child;
}

static void test_scene_a11y_update_frame(void* userdata,
                                         croft_scene_a11y_handle node_handle,
                                         float x,
                                         float y,
                                         float w,
                                         float h)
{
    test_scene_a11y_node* node;

    (void)userdata;
    node = test_scene_a11y_find(node_handle);
    if (!node) {
        return;
    }

    node->x = x;
    node->y = y;
    node->width = w;
    node->height = h;
    node->update_count++;
}

static void test_scene_a11y_update_label(void* userdata,
                                         croft_scene_a11y_handle node_handle,
                                         const char* label)
{
    test_scene_a11y_node* node;

    (void)userdata;
    node = test_scene_a11y_find(node_handle);
    if (!node) {
        return;
    }

    snprintf(node->label, sizeof(node->label), "%s", label ? label : "");
    node->label_update_count++;
}

static void test_scene_a11y_update_value(void* userdata,
                                         croft_scene_a11y_handle node_handle,
                                         const char* value)
{
    test_scene_a11y_node* node;

    (void)userdata;
    node = test_scene_a11y_find(node_handle);
    if (!node) {
        return;
    }

    snprintf(node->value, sizeof(node->value), "%s", value ? value : "");
    node->value_update_count++;
}

static void test_scene_a11y_destroy_node(void* userdata, croft_scene_a11y_handle node)
{
    (void)userdata;
    (void)node;
}

int test_scene_accessibility_tree_updates(void)
{
    static const croft_scene_a11y_bridge_vtbl bridge = {
        .open = test_scene_a11y_open,
        .close = test_scene_a11y_close,
        .create_node = test_scene_a11y_create_node,
        .add_child = test_scene_a11y_add_child,
        .update_frame = test_scene_a11y_update_frame,
        .update_label = test_scene_a11y_update_label,
        .update_value = test_scene_a11y_update_value,
        .destroy_node = test_scene_a11y_destroy_node
    };
    viewport_node root = {0};
    code_block_node child = {0};
    text_editor_node editor = {0};
    croft_editor_document* document = NULL;
    test_scene_a11y_node* root_node;
    test_scene_a11y_node* child_node;
    test_scene_a11y_node* editor_node;
    static const uint8_t initial_text[] = "alpha\nbeta";
    static const uint8_t replacement_text[] = "gamma";

    memset(g_a11y_nodes, 0, sizeof(g_a11y_nodes));
    g_a11y_node_count = 0u;
    g_last_parent = (croft_scene_a11y_handle)0u;
    g_last_child = (croft_scene_a11y_handle)0u;
    croft_scene_a11y_install_bridge(&bridge, NULL);

    viewport_node_init(&root, 0.0f, 0.0f, 800.0f, 600.0f);
    root.scroll_x = 10.0f;
    root.scroll_y = 20.0f;
    root.scale = 2.0f;
    code_block_node_init(&child, 30.0f, 40.0f, 100.0f, 50.0f, "Hello");
    document = croft_editor_document_create(initial_text, sizeof(initial_text) - 1u);
    ASSERT_A11Y(document != NULL);
    text_editor_node_init(&editor, NULL, 12.0f, 18.0f, 240.0f, 160.0f, NULL);
    text_editor_node_bind_document(&editor, document);
    scene_node_add_child(&root.base, &child.base);
    scene_node_add_child(&root.base, &editor.base);

    ASSERT_A11Y(g_last_parent == root.base.a11y_handle);
    ASSERT_A11Y(g_last_child == editor.base.a11y_handle);

    scene_node_update_accessibility_tree(&root.base);

    root_node = test_scene_a11y_find(root.base.a11y_handle);
    child_node = test_scene_a11y_find(child.base.a11y_handle);
    editor_node = test_scene_a11y_find(editor.base.a11y_handle);
    ASSERT_A11Y(root_node != NULL);
    ASSERT_A11Y(child_node != NULL);
    ASSERT_A11Y(editor_node != NULL);
    ASSERT_A11Y(root_node->update_count == 1u);
    ASSERT_A11Y(root_node->x == 0.0f);
    ASSERT_A11Y(root_node->y == 0.0f);
    ASSERT_A11Y(root_node->width == 800.0f);
    ASSERT_A11Y(root_node->height == 600.0f);
    ASSERT_A11Y(child_node->update_count == 1u);
    ASSERT_A11Y(child_node->x == 70.0f);
    ASSERT_A11Y(child_node->y == 100.0f);
    ASSERT_A11Y(child_node->width == 200.0f);
    ASSERT_A11Y(child_node->height == 100.0f);
    ASSERT_A11Y(editor_node->role == CROFT_SCENE_A11Y_ROLE_TEXT_AREA);
    ASSERT_A11Y(editor_node->update_count == 1u);
    ASSERT_A11Y(strcmp(editor_node->label, "Code Editor. Ln 1, Col 1  Lines 2  Saved.") == 0);
    ASSERT_A11Y(strcmp(editor_node->value, "alpha\nbeta") == 0);
    ASSERT_A11Y(editor_node->label_update_count == 1u);
    ASSERT_A11Y(editor_node->value_update_count == 1u);

    text_editor_node_select_all(&editor);
    ASSERT_A11Y(text_editor_node_replace_selection_utf8(&editor,
                                                        replacement_text,
                                                        sizeof(replacement_text) - 1u)
                == 0);
    scene_node_update_accessibility_tree(&root.base);
    ASSERT_A11Y(strcmp(editor_node->label, "Code Editor. Ln 1, Col 6  Lines 1  Modified.") == 0);
    ASSERT_A11Y(strcmp(editor_node->value, "gamma") == 0);
    ASSERT_A11Y(editor_node->label_update_count == 2u);
    ASSERT_A11Y(editor_node->value_update_count == 2u);

    text_editor_node_dispose(&editor);
    croft_editor_document_destroy(document);
    croft_scene_a11y_reset_bridge();
    return 0;
}
