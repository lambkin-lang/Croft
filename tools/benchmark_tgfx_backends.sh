#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCH_SCRIPT="${ROOT_DIR}/tools/benchmark_binary_size.sh"

run_variant() {
    local name="$1"
    local build_dir="$2"
    local render_target="$3"
    local ui_target="$4"
    local menu_target="$5"
    shift 5
    local cmake_args=("$@")
    local cmd=(
        "$BENCH_SCRIPT"
        --opt-dir "$build_dir"
        --target "$ui_target"
        --target "$menu_target"
        --target "$render_target"
    )
    local arg

    for arg in "${cmake_args[@]}"; do
        cmd+=(--cmake-arg "$arg")
    done

    echo
    echo "=== ${name} ==="
    "${cmd[@]}"
}

run_variant \
    "OpenGL tgfx backend" \
    "build-size-opt-opengl" \
    "example_render_canvas_opengl" \
    "example_ui_window_opengl" \
    "example_window_menu_opengl" \
    -DTGFX_USE_METAL=OFF \
    -DTGFX_USE_OPENGL=ON

run_variant \
    "Metal tgfx backend" \
    "build-size-opt-metal" \
    "example_render_canvas_metal" \
    "example_ui_window_metal" \
    "example_window_menu_metal" \
    -DTGFX_USE_METAL=ON \
    -DTGFX_USE_OPENGL=OFF

run_variant \
    "Metal native-direct backend" \
    "build-size-opt-metal" \
    "example_render_canvas_metal_native" \
    "example_ui_window_metal" \
    "example_window_menu_metal" \
    -DTGFX_USE_METAL=ON \
    -DTGFX_USE_OPENGL=OFF
