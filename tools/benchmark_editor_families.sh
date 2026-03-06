#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCH_SCRIPT="${ROOT_DIR}/tools/benchmark_binary_size.sh"

"${BENCH_SCRIPT}" \
    --opt-dir "build-size-opt-metal" \
    --target "example_editor_text" \
    --target "example_editor_text_appkit" \
    --target "example_editor_text_metal_native" \
    --cmake-arg "-DTGFX_USE_METAL=ON" \
    --cmake-arg "-DTGFX_USE_OPENGL=OFF"
