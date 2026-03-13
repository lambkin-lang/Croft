#ifndef CROFT_JSON_VIEWER_WINDOW_APP_H
#define CROFT_JSON_VIEWER_WINDOW_APP_H

#include "croft/json_viewer_state.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*CroftJsonViewerWindowDispatchFn)(void *ctx,
                                               const void *command,
                                               void *reply_out);

typedef int (*CroftJsonViewerGpuDispatchFn)(void *ctx,
                                            const void *command,
                                            void *reply_out);

typedef int (*CroftJsonViewerClockDispatchFn)(void *ctx,
                                              const void *command,
                                              void *reply_out);

typedef struct {
    void *dispatch_ctx;
    CroftJsonViewerWindowDispatchFn window_dispatch;
    CroftJsonViewerGpuDispatchFn gpu_dispatch;
    CroftJsonViewerClockDispatchFn clock_dispatch;
    const uint8_t *title_data;
    uint32_t title_len;
} CroftJsonViewerWindowAppConfig;

typedef struct {
    CroftJsonViewerState viewer;
    float cursor_x;
    float cursor_y;
} CroftJsonViewerWindowAppState;

int croft_json_viewer_window_app_run(CroftJsonViewerWindowAppState *state,
                                     const CroftJsonViewerWindowAppConfig *config,
                                     const uint8_t *json,
                                     uint32_t json_len,
                                     uint32_t auto_close_ms,
                                     uint32_t *frame_count_out);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_JSON_VIEWER_WINDOW_APP_H */
