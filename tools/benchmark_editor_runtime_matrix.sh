#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDITOR_BENCH="${ROOT_DIR}/tools/benchmark_editor_runtime.sh"
ITERATIONS="${CROFT_EDITOR_RUNTIME_MATRIX_ITERATIONS:-2}"
KEEP="${CROFT_EDITOR_RUNTIME_MATRIX_KEEP:-40}"
AUTO_CLOSE_MS="${CROFT_EDITOR_RUNTIME_MATRIX_AUTO_CLOSE_MS:-1200}"
TIMEOUT_SECS="${CROFT_EDITOR_RUNTIME_MATRIX_TIMEOUT_SECS:-6.0}"
LINE_COUNTS=(${CROFT_EDITOR_RUNTIME_MATRIX_LINES:-200 1200 5000})

for line_count in "${LINE_COUNTS[@]}"; do
    echo
    echo "=== Editor Runtime Matrix: ${line_count} lines ==="
    "${EDITOR_BENCH}" \
        --editor-lines "${line_count}" \
        --iterations "${ITERATIONS}" \
        --keep "${KEEP}" \
        --auto-close-ms "${AUTO_CLOSE_MS}" \
        --timeout "${TIMEOUT_SECS}" \
        "$@"
done
