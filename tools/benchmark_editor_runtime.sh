#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNTIME_BENCH="${ROOT_DIR}/tools/benchmark_runtime_perf.sh"

exec "${RUNTIME_BENCH}" \
    --target "example_editor_text" \
    --target "example_editor_text_appkit" \
    --target "example_editor_text_metal_native" \
    --editor-lines "${CROFT_EDITOR_RUNTIME_BENCH_LINES:-1200}" \
    --auto-close-ms "${CROFT_EDITOR_RUNTIME_BENCH_AUTO_CLOSE_MS:-1200}" \
    --timeout "${CROFT_EDITOR_RUNTIME_BENCH_TIMEOUT_SECS:-6.0}" \
    "$@"
