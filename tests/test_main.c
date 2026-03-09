/*
 * test_main.c — Tier 0 test suite.
 *
 * A deliberately simple test harness: each test is a function that
 * returns 0 on success.  No external test framework is required.
 */

#include "croft/platform.h"
#include "croft/host_log.h"
#include "croft/host_time.h"
#include "croft/host_thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Minimal test macros ──────────────────────────────────────────── */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define RUN_TEST(fn)                                                 \
    do {                                                             \
        g_tests_run++;                                               \
        printf("  %-40s ", #fn);                                     \
        int _rc = fn();                                              \
        if (_rc != 0) { g_tests_failed++; printf("FAIL\n"); }       \
        else { printf("ok\n"); }                                     \
    } while (0)

#define ASSERT(cond)                                                 \
    do {                                                             \
        if (!(cond)) {                                               \
            fprintf(stderr, "    ASSERT failed: %s  (%s:%d)\n",     \
                    #cond, __FILE__, __LINE__);                      \
            return 1;                                                \
        }                                                            \
    } while (0)

/* ═══════════════════════════════════════════════════════════════════ *
 *  host_time tests                                                   *
 * ═══════════════════════════════════════════════════════════════════ */

static int test_time_nonzero(void)
{
    uint64_t t = host_time_millis();
    ASSERT(t > 0);
    return 0;
}

static int test_time_monotonic(void)
{
    uint64_t a = host_time_millis();
    /* Burn a tiny amount of time. */
    for (volatile int i = 0; i < 100000; i++) { }
    uint64_t b = host_time_millis();
    ASSERT(b >= a);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════ *
 *  host_log tests                                                    *
 * ═══════════════════════════════════════════════════════════════════ */

/* A small capture buffer used by the custom sink. */
static char  g_log_buf[256];
static int   g_log_level;
static uint32_t g_log_len;

static void capture_sink(int level, const char *ptr, uint32_t len,
                          void *userdata)
{
    (void)userdata;
    g_log_level = level;
    g_log_len   = len < sizeof(g_log_buf) ? len : (uint32_t)(sizeof(g_log_buf) - 1);
    memcpy(g_log_buf, ptr, g_log_len);
    g_log_buf[g_log_len] = '\0';
}

static int test_log_capture(void)
{
    host_log_set_sink(capture_sink, NULL);
    host_log_set_level(CROFT_LOG_TRACE);

    const char *msg = "hello croft";
    host_log(CROFT_LOG_INFO, msg, (uint32_t)strlen(msg));

    ASSERT(g_log_level == CROFT_LOG_INFO);
    ASSERT(g_log_len == strlen(msg));
    ASSERT(memcmp(g_log_buf, msg, g_log_len) == 0);

    /* Restore defaults. */
    host_log_set_sink(NULL, NULL);
    return 0;
}

static int test_log_level_filter(void)
{
    host_log_set_sink(capture_sink, NULL);
    host_log_set_level(CROFT_LOG_WARN);

    g_log_buf[0] = '\0';
    g_log_len    = 0;

    const char *msg = "should be filtered";
    host_log(CROFT_LOG_DEBUG, msg, (uint32_t)strlen(msg));

    /* The message should NOT have reached the sink. */
    ASSERT(g_log_len == 0);

    /* But a WARN message should. */
    const char *msg2 = "warning!";
    host_log(CROFT_LOG_WARN, msg2, (uint32_t)strlen(msg2));
    ASSERT(g_log_len == strlen(msg2));

    host_log_set_sink(NULL, NULL);
    host_log_set_level(CROFT_LOG_TRACE);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════ *
 *  host_thread tests                                                 *
 * ═══════════════════════════════════════════════════════════════════ */

static void *increment_thread(void *arg)
{
    int *counter = (int *)arg;
    (*counter)++;
    return NULL;
}

static int test_thread_create_join(void)
{
    int counter = 0;
    host_thread_t t;
    int rc = host_thread_create(&t, increment_thread, &counter);
    ASSERT(rc == 0);

    rc = host_thread_join(t, NULL);
    ASSERT(rc == 0);
    ASSERT(counter == 1);
    return 0;
}

static int test_mutex_lock_unlock(void)
{
    host_mutex_t m;
    ASSERT(host_mutex_init(&m) == 0);
    ASSERT(host_mutex_lock(&m) == 0);
    ASSERT(host_mutex_unlock(&m) == 0);
    host_mutex_destroy(&m);
    return 0;
}

/* Shared state for the mutex‑contention test. */
struct counter_ctx {
    host_mutex_t mutex;
    int          value;
};

static void *mutex_incrementer(void *arg)
{
    struct counter_ctx *ctx = (struct counter_ctx *)arg;
    for (int i = 0; i < 10000; i++) {
        host_mutex_lock(&ctx->mutex);
        ctx->value++;
        host_mutex_unlock(&ctx->mutex);
    }
    return NULL;
}

static int test_mutex_contention(void)
{
    struct counter_ctx ctx;
    ctx.value = 0;
    ASSERT(host_mutex_init(&ctx.mutex) == 0);

    enum { N = 4 };
    host_thread_t threads[N];
    for (int i = 0; i < N; i++)
        ASSERT(host_thread_create(&threads[i], mutex_incrementer, &ctx) == 0);

    for (int i = 0; i < N; i++)
        ASSERT(host_thread_join(threads[i], NULL) == 0);

    ASSERT(ctx.value == N * 10000);
    host_mutex_destroy(&ctx.mutex);
    return 0;
}

static int test_cond_signal(void)
{
    host_mutex_t m;
    host_cond_t  c;
    ASSERT(host_mutex_init(&m) == 0);
    ASSERT(host_cond_init(&c) == 0);

    /* Simple signal/broadcast — just verify no errors. */
    ASSERT(host_cond_signal(&c) == 0);
    ASSERT(host_cond_broadcast(&c) == 0);

    host_cond_destroy(&c);
    host_mutex_destroy(&m);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════ *
 *  Queue Tests                                                       *
 * ═══════════════════════════════════════════════════════════════════ */

extern int test_queue_basic(void);
extern int test_queue_multi_producer(void);
extern int test_queue_backpressure(void);
extern int test_queue_unbind(void);
extern int test_editor_text_model_offsets(void);
extern int test_editor_text_model_multibyte(void);
extern int test_editor_text_model_selection(void);
extern int test_editor_text_model_word_ranges(void);
extern int test_editor_syntax_language_from_path(void);
extern int test_editor_syntax_json_tokens(void);
extern int test_editor_syntax_markdown_tokens(void);
extern int test_editor_syntax_python_tokens(void);
extern int test_editor_syntax_yaml_tokens(void);
extern int test_editor_syntax_yaml_refinements(void);
extern int test_editor_syntax_javascript_tokens(void);
extern int test_editor_syntax_javascript_refinements(void);
extern int test_editor_syntax_css_tokens(void);
extern int test_editor_syntax_html_tokens(void);
extern int test_editor_syntax_xml_tokens(void);
extern int test_editor_syntax_lambkin_tokens(void);
extern int test_editor_syntax_wit_tokens(void);
extern int test_editor_syntax_wat_tokens(void);
extern int test_editor_syntax_invalid_inputs(void);
extern int test_editor_brackets_nested_pairs(void);
extern int test_editor_brackets_near_cursor(void);
extern int test_editor_brackets_unmatched_or_invalid(void);
extern int test_editor_folding_nested_regions(void);
extern int test_editor_folding_blank_lines_and_tabs(void);
extern int test_editor_folding_invalid_lines(void);
extern int test_editor_whitespace_describe_lines(void);
extern int test_editor_whitespace_markers(void);
extern int test_editor_whitespace_invalid_inputs(void);
extern int test_editor_search_next_previous(void);
extern int test_editor_search_multibyte(void);
extern int test_editor_search_invalid_queries(void);
extern int test_editor_status_line_number_digits(void);
extern int test_editor_status_format(void);
extern int test_editor_document_undo_redo_coalesced_insert(void);
extern int test_editor_document_coalescing_barrier(void);
extern int test_editor_document_delete_coalescing_and_redo_invalidation(void);
extern int test_editor_document_replace_range_with_utf8(void);
extern int test_editor_document_fs_open_save_roundtrip(void);
extern int test_editor_commands_word_moves(void);
extern int test_editor_commands_word_deletes(void);
extern int test_editor_commands_vertical_column_memory(void);
extern int test_editor_commands_shift_word_selection(void);
extern int test_editor_commands_word_part_moves(void);
extern int test_editor_commands_word_part_deletes(void);
extern int test_editor_commands_shift_word_part_selection(void);
extern int test_editor_commands_tab_insert(void);
extern int test_editor_commands_indent_lines(void);
extern int test_editor_commands_outdent_lines(void);
extern int test_editor_scene_runtime_bounds_invalidate(void);
extern int test_editor_scene_runtime_cursor_blink_invalidate(void);
extern int test_editor_scene_runtime_auto_close(void);
extern int test_wit_resource_open_command_roundtrip(void);
extern int test_wit_resource_handle_roundtrip(void);
extern int test_wit_text_runtime_roundtrip(void);
extern int test_wit_text_runtime_invalid_handle(void);
extern int test_wit_text_program_prepend(void);
extern int test_wit_store_runtime_roundtrip(void);
extern int test_wit_store_runtime_readonly_put_rejected(void);
extern int test_wit_mailbox_runtime_roundtrip(void);
extern int test_wit_mailbox_runtime_drop_busy(void);
extern int test_wit_host_fs_runtime_read_fixture(void);
extern int test_wit_host_clock_runtime_monotonic(void);
extern int test_wit_host_editor_input_runtime_shortcuts(void);
extern int test_wit_host_editor_input_runtime_motion_modes(void);
extern int test_wit_host_editor_input_runtime_indent_actions(void);
extern int test_wit_host_editor_input_runtime_fold_actions(void);

extern void run_test_fs(int argc, char **argv);

static int run_tier2_fs_tests(void) {
    run_test_fs(0, NULL);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════ *
 *  Wasm Guest Tests                                                  *
 * ═══════════════════════════════════════════════════════════════════ */

extern void run_test_wasm_guest(int argc, char **argv);

static int run_tier4_wasm_tests(void) {
#ifdef CROFT_TEST_HAS_WASM
    run_test_wasm_guest(0, NULL);
#endif
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════ *
 *  main                                                              *
 * ═══════════════════════════════════════════════════════════════════ */

int main(void)
{
    printf("Croft Tier 0 — test suite\n");

    printf("\n[host_time]\n");
    RUN_TEST(test_time_nonzero);
    RUN_TEST(test_time_monotonic);

    printf("\n[host_log]\n");
    RUN_TEST(test_log_capture);
    RUN_TEST(test_log_level_filter);

    printf("\n[host_thread]\n");
    RUN_TEST(test_thread_create_join);
    RUN_TEST(test_mutex_lock_unlock);
    RUN_TEST(test_mutex_contention);
    RUN_TEST(test_cond_signal);

    printf("\n[host_queue]\n");
    RUN_TEST(test_queue_basic);
    RUN_TEST(test_queue_multi_producer);
    RUN_TEST(test_queue_backpressure);
    RUN_TEST(test_queue_unbind);

    printf("\n[editor_text_model]\n");
    RUN_TEST(test_editor_text_model_offsets);
    RUN_TEST(test_editor_text_model_multibyte);
    RUN_TEST(test_editor_text_model_selection);
    RUN_TEST(test_editor_text_model_word_ranges);

    printf("\n[editor_syntax]\n");
    RUN_TEST(test_editor_syntax_language_from_path);
    RUN_TEST(test_editor_syntax_json_tokens);
    RUN_TEST(test_editor_syntax_markdown_tokens);
    RUN_TEST(test_editor_syntax_python_tokens);
    RUN_TEST(test_editor_syntax_yaml_tokens);
    RUN_TEST(test_editor_syntax_yaml_refinements);
    RUN_TEST(test_editor_syntax_javascript_tokens);
    RUN_TEST(test_editor_syntax_javascript_refinements);
    RUN_TEST(test_editor_syntax_css_tokens);
    RUN_TEST(test_editor_syntax_html_tokens);
    RUN_TEST(test_editor_syntax_xml_tokens);
    RUN_TEST(test_editor_syntax_lambkin_tokens);
    RUN_TEST(test_editor_syntax_wit_tokens);
    RUN_TEST(test_editor_syntax_wat_tokens);
    RUN_TEST(test_editor_syntax_invalid_inputs);

    printf("\n[editor_brackets]\n");
    RUN_TEST(test_editor_brackets_nested_pairs);
    RUN_TEST(test_editor_brackets_near_cursor);
    RUN_TEST(test_editor_brackets_unmatched_or_invalid);

    printf("\n[editor_folding]\n");
    RUN_TEST(test_editor_folding_nested_regions);
    RUN_TEST(test_editor_folding_blank_lines_and_tabs);
    RUN_TEST(test_editor_folding_invalid_lines);

    printf("\n[editor_whitespace]\n");
    RUN_TEST(test_editor_whitespace_describe_lines);
    RUN_TEST(test_editor_whitespace_markers);
    RUN_TEST(test_editor_whitespace_invalid_inputs);

    printf("\n[editor_search]\n");
    RUN_TEST(test_editor_search_next_previous);
    RUN_TEST(test_editor_search_multibyte);
    RUN_TEST(test_editor_search_invalid_queries);

    printf("\n[editor_status]\n");
    RUN_TEST(test_editor_status_line_number_digits);
    RUN_TEST(test_editor_status_format);

    printf("\n[editor_document]\n");
    RUN_TEST(test_editor_document_undo_redo_coalesced_insert);
    RUN_TEST(test_editor_document_coalescing_barrier);
    RUN_TEST(test_editor_document_delete_coalescing_and_redo_invalidation);
    RUN_TEST(test_editor_document_replace_range_with_utf8);
    RUN_TEST(test_editor_document_fs_open_save_roundtrip);

    printf("\n[editor_commands]\n");
    RUN_TEST(test_editor_commands_word_moves);
    RUN_TEST(test_editor_commands_word_deletes);
    RUN_TEST(test_editor_commands_vertical_column_memory);
    RUN_TEST(test_editor_commands_shift_word_selection);
    RUN_TEST(test_editor_commands_word_part_moves);
    RUN_TEST(test_editor_commands_word_part_deletes);
    RUN_TEST(test_editor_commands_shift_word_part_selection);
    RUN_TEST(test_editor_commands_tab_insert);
    RUN_TEST(test_editor_commands_indent_lines);
    RUN_TEST(test_editor_commands_outdent_lines);

    printf("\n[editor_scene_runtime]\n");
    RUN_TEST(test_editor_scene_runtime_bounds_invalidate);
    RUN_TEST(test_editor_scene_runtime_cursor_blink_invalidate);
    RUN_TEST(test_editor_scene_runtime_auto_close);

    printf("\n[wit_common_core]\n");
    RUN_TEST(test_wit_resource_open_command_roundtrip);
    RUN_TEST(test_wit_resource_handle_roundtrip);
    RUN_TEST(test_wit_text_runtime_roundtrip);
    RUN_TEST(test_wit_text_runtime_invalid_handle);
    RUN_TEST(test_wit_text_program_prepend);
    RUN_TEST(test_wit_store_runtime_roundtrip);
    RUN_TEST(test_wit_store_runtime_readonly_put_rejected);
    RUN_TEST(test_wit_mailbox_runtime_roundtrip);
    RUN_TEST(test_wit_mailbox_runtime_drop_busy);
    RUN_TEST(test_wit_host_fs_runtime_read_fixture);
    RUN_TEST(test_wit_host_clock_runtime_monotonic);
    RUN_TEST(test_wit_host_editor_input_runtime_shortcuts);
    RUN_TEST(test_wit_host_editor_input_runtime_motion_modes);
    RUN_TEST(test_wit_host_editor_input_runtime_indent_actions);
    RUN_TEST(test_wit_host_editor_input_runtime_fold_actions);

    printf("\n[host_fs]\n");
    RUN_TEST(run_tier2_fs_tests);

#ifdef CROFT_TEST_HAS_WASM
    printf("\n[host_wasm]\n");
    RUN_TEST(run_tier4_wasm_tests);
#endif

    printf("\n%d/%d tests passed.\n",
           g_tests_run - g_tests_failed, g_tests_run);

    return g_tests_failed > 0 ? 1 : 0;
}
