#include "croft/host_log.h"
#include "croft/host_thread.h"
#include "croft/host_time.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct foundation_example_state {
    host_mutex_t mutex;
    host_cond_t cond;
    int done;
    uint64_t started_ms;
    uint64_t finished_ms;
    uint64_t checksum;
} foundation_example_state;

static void log_message(int level, const char* message) {
    host_log(level, message, (uint32_t)strlen(message));
}

static void* foundation_worker(void* arg) {
    foundation_example_state* state = (foundation_example_state*)arg;
    uint64_t checksum = 0;

    state->started_ms = host_time_millis();
    for (uint64_t i = 0; i < 500000; ++i) {
        checksum += (i ^ 0x5Au) & 0xFFu;
    }
    state->finished_ms = host_time_millis();
    state->checksum = checksum;

    host_mutex_lock(&state->mutex);
    state->done = 1;
    host_cond_signal(&state->cond);
    host_mutex_unlock(&state->mutex);

    return NULL;
}

int main(void) {
    foundation_example_state state;
    host_thread_t worker;
    int rc;
    char line[160];

    memset(&state, 0, sizeof(state));
    host_log_set_level(CROFT_LOG_INFO);

    rc = host_mutex_init(&state.mutex);
    if (rc != 0) {
        fprintf(stderr, "example_foundation_threads: host_mutex_init failed (%d)\n", rc);
        return 1;
    }

    rc = host_cond_init(&state.cond);
    if (rc != 0) {
        fprintf(stderr, "example_foundation_threads: host_cond_init failed (%d)\n", rc);
        host_mutex_destroy(&state.mutex);
        return 1;
    }

    log_message(CROFT_LOG_INFO, "example_foundation_threads: starting worker thread");
    rc = host_thread_create(&worker, foundation_worker, &state);
    if (rc != 0) {
        fprintf(stderr, "example_foundation_threads: host_thread_create failed (%d)\n", rc);
        host_cond_destroy(&state.cond);
        host_mutex_destroy(&state.mutex);
        return 1;
    }

    host_mutex_lock(&state.mutex);
    while (!state.done) {
        host_cond_wait(&state.cond, &state.mutex);
    }
    host_mutex_unlock(&state.mutex);

    rc = host_thread_join(worker, NULL);
    if (rc != 0) {
        fprintf(stderr, "example_foundation_threads: host_thread_join failed (%d)\n", rc);
        host_cond_destroy(&state.cond);
        host_mutex_destroy(&state.mutex);
        return 1;
    }

    host_cond_destroy(&state.cond);
    host_mutex_destroy(&state.mutex);

    snprintf(line, sizeof(line),
             "example_foundation_threads: worker finished in %llu ms (checksum=%llu)",
             (unsigned long long)(state.finished_ms - state.started_ms),
             (unsigned long long)state.checksum);
    log_message(CROFT_LOG_INFO, line);
    printf("%s\n", line);
    return 0;
}
