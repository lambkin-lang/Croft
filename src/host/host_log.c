/*
 * host_log.c — Default logging implementation (stderr sink).
 */

#include "croft/host_log.h"
#include <stdio.h>
#include <string.h>

/* ── State ────────────────────────────────────────────────────────── */

static int              g_min_level = CROFT_LOG_TRACE;
static host_log_sink_fn g_sink      = NULL;
static void            *g_sink_ud   = NULL;

/* ── Level labels ─────────────────────────────────────────────────── */

static const char *level_label(int level)
{
    switch (level) {
    case CROFT_LOG_TRACE: return "TRACE";
    case CROFT_LOG_DEBUG: return "DEBUG";
    case CROFT_LOG_INFO:  return "INFO ";
    case CROFT_LOG_WARN:  return "WARN ";
    case CROFT_LOG_ERROR: return "ERROR";
    default:              return "?????";
    }
}

/* ── Public API ───────────────────────────────────────────────────── */

void host_log(int level, const char *ptr, uint32_t len)
{
    if (level < g_min_level)
        return;

    if (g_sink) {
        g_sink(level, ptr, len, g_sink_ud);
        return;
    }

    /* Default: write to stderr. */
    fprintf(stderr, "[%s] %.*s\n", level_label(level), (int)len, ptr);
}

void host_log_set_level(int min_level)
{
    g_min_level = min_level;
}

void host_log_set_sink(host_log_sink_fn sink, void *userdata)
{
    g_sink    = sink;
    g_sink_ud = userdata;
}
