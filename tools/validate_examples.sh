#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="build"
AUTO_CLOSE_MS="${CROFT_EXAMPLE_SMOKE_AUTO_CLOSE_MS:-1500}"
TIMEOUT_MS="${CROFT_EXAMPLE_SMOKE_TIMEOUT_MS:-6000}"
KEEP_LOGS="${CROFT_EXAMPLE_SMOKE_KEEP:-40}"
LOG_ROOT="${ROOT_DIR}/local_logs/example_validation"
RUNS_DIR="${LOG_ROOT}/runs"
RUNNER_SRC="${ROOT_DIR}/tools/runtime_bench_runner.c"
RUNNER_BIN="${LOG_ROOT}/runtime_bench_runner"
TARGET_LIST_FILE=""

SMOKE_TARGETS=(
    example_wit_text_cli
    example_wit_text_wasm_host
    example_wit_window_wasm_host
    example_wit_json_viewer_window
    example_wit_json_viewer_wasm_host
    example_wit_window_events
    example_wit_gpu_canvas
    example_wit_text_window
    example_wit_textpad_window
    example_editor_text
    example_editor_text_appkit
    example_editor_text_metal_native
)

usage() {
    cat <<'USAGE'
Usage:
  tools/validate_examples.sh [options]

Build the full `croft_examples` target and smoke-run a representative subset of
example binaries. GUI targets are launched with short auto-close timeouts.

Options:
  --build-dir <dir>       Build directory to use (default: build)
  --auto-close-ms <ms>    Auto-close duration for GUI examples (default: 1500)
  --timeout-ms <ms>       Per-example timeout in milliseconds (default: 6000)
  --keep <count>          Keep newest N validation logs (default: 40)
  --help                  Show this help
USAGE
}

print_cmd() {
    printf '$'
    local arg
    for arg in "$@"; do
        printf ' %q' "$arg"
    done
    printf '\n'
}

sanitize() {
    printf '%s' "$1" | tr '/[:space:]' '__' | tr -cd '[:alnum:]_.-,'
}

ensure_runner() {
    mkdir -p "$LOG_ROOT"
    if [[ ! -x "$RUNNER_BIN" || "$RUNNER_SRC" -nt "$RUNNER_BIN" ]]; then
        print_cmd cc -std=c99 -O2 -Wall -Wextra -o "$RUNNER_BIN" "$RUNNER_SRC"
        cc -std=c99 -O2 -Wall -Wextra -o "$RUNNER_BIN" "$RUNNER_SRC"
    fi
}

cleanup_old_logs() {
    local keep="$1"
    local sorted=()
    local i

    [[ "$keep" =~ ^[0-9]+$ ]] || return
    mkdir -p "$RUNS_DIR"

    while IFS= read -r line; do
        sorted+=("$line")
    done < <(ls -1t "${RUNS_DIR}"/*.log 2>/dev/null || true)

    if (( ${#sorted[@]} <= keep )); then
        return
    fi

    for ((i = keep; i < ${#sorted[@]}; ++i)); do
        rm -f "${sorted[i]}"
    done
}

has_target() {
    local target="$1"
    grep -Fxq "$target" "$TARGET_LIST_FILE"
}

runtime_env_args() {
    local target="$1"
    case "$target" in
        example_wit_window_wasm_host)
            printf 'CROFT_WIT_WINDOW_WASM_HOST_AUTO_CLOSE_MS=%s\n' "$AUTO_CLOSE_MS"
            ;;
        example_wit_json_viewer_window)
            printf 'CROFT_WIT_JSON_VIEWER_AUTO_CLOSE_MS=%s\n' "$AUTO_CLOSE_MS"
            ;;
        example_wit_json_viewer_wasm_host)
            printf 'CROFT_WIT_JSON_VIEWER_WASM_HOST_AUTO_CLOSE_MS=%s\n' "$AUTO_CLOSE_MS"
            ;;
        example_wit_gpu_canvas)
            printf 'CROFT_WIT_GPU_AUTO_CLOSE_MS=%s\n' "$AUTO_CLOSE_MS"
            ;;
        example_wit_text_window)
            printf 'CROFT_WIT_TEXT_WINDOW_AUTO_CLOSE_MS=%s\n' "$AUTO_CLOSE_MS"
            ;;
        example_wit_textpad_window)
            printf 'CROFT_WIT_TEXTPAD_AUTO_CLOSE_MS=%s\n' "$AUTO_CLOSE_MS"
            ;;
        example_editor_text|example_editor_text_appkit|example_editor_text_metal_native)
            printf 'CROFT_EDITOR_AUTO_CLOSE_MS=%s\n' "$AUTO_CLOSE_MS"
            ;;
    esac
}

extract_frames() {
    local log_file="$1"
    sed -n 's/.*frames=\([0-9][0-9]*\).*/\1/p' "$log_file" | tail -n 1
}

extract_wall_ms() {
    local log_file="$1"
    sed -n 's/.*wall_ms=\([0-9][0-9]*\).*/\1/p' "$log_file" | tail -n 1
}

run_target() {
    local target="$1"
    local binary="${ROOT_DIR}/${BUILD_DIR}/${target}"
    local safe_target
    local log_file
    local env_lines=()
    local env_args=()
    local line
    local summary
    local status
    local rc
    local wall_ms
    local sample_wall_ms
    local frames

    if [[ ! -x "$binary" ]]; then
        echo "ERROR: built example binary not found: ${binary}" >&2
        return 1
    fi

    safe_target="$(sanitize "$target")"
    log_file="${RUNS_DIR}/${STAMP}--${SAFE_DESC}--${safe_target}.log"

    while IFS= read -r line; do
        [[ -n "$line" ]] || continue
        env_lines+=("$line")
    done < <(runtime_env_args "$target")

    if (( ${#env_lines[@]} > 0 )); then
        env_args=(env)
        env_args+=("${env_lines[@]}")
        print_cmd "${env_args[@]}" "$binary"
    else
        print_cmd "$binary"
    fi

    summary="$(
        if (( ${#env_lines[@]} > 0 )); then
            for line in "${env_lines[@]}"; do
                export "$line"
            done
        fi
        "$RUNNER_BIN" --timeout-ms "$TIMEOUT_MS" --log-file "$log_file" -- "$binary"
    )"

    status="$(printf '%s\n' "$summary" | awk -F= '/^status=/{print $2}')"
    rc="$(printf '%s\n' "$summary" | awk -F= '/^rc=/{print $2}')"
    wall_ms="$(printf '%s\n' "$summary" | awk -F= '/^wall_ms=/{print $2}')"
    sample_wall_ms="$(extract_wall_ms "$log_file")"
    frames="$(extract_frames "$log_file")"

    echo "-- ${target} --"
    echo "status=${status}"
    echo "rc=${rc}"
    echo "wall_ms=${wall_ms}"
    if [[ -n "$sample_wall_ms" ]]; then
        echo "sample_wall_ms=${sample_wall_ms}"
    fi
    if [[ -n "$frames" ]]; then
        echo "frames=${frames}"
    fi
    echo "log=${log_file}"

    if [[ "$status" != "ok" || "${rc:-1}" != "0" ]]; then
        echo "ERROR: example validation failed for ${target}" >&2
        return 1
    fi
}

while (( "$#" )); do
    case "$1" in
        --build-dir)
            BUILD_DIR="${2:-}"
            shift 2
            ;;
        --auto-close-ms)
            AUTO_CLOSE_MS="${2:-}"
            shift 2
            ;;
        --timeout-ms)
            TIMEOUT_MS="${2:-}"
            shift 2
            ;;
        --keep)
            KEEP_LOGS="${2:-}"
            shift 2
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

mkdir -p "$RUNS_DIR"
ensure_runner

print_cmd cmake --build "${ROOT_DIR}/${BUILD_DIR}" --target croft_examples
cmake --build "${ROOT_DIR}/${BUILD_DIR}" --target croft_examples

TARGET_LIST_FILE="${ROOT_DIR}/${BUILD_DIR}/croft-example-targets.txt"
if [[ ! -f "$TARGET_LIST_FILE" ]]; then
    echo "ERROR: example target list not found: ${TARGET_LIST_FILE}" >&2
    exit 1
fi

GIT_DESC="$(git -C "$ROOT_DIR" describe --always --dirty --abbrev=7 2>/dev/null || echo unknown)"
STAMP="$(date +"%Y-%m-%d-%H%M%S")"
SAFE_DESC="$(sanitize "$GIT_DESC")"

echo "=== Example Validation ==="
echo "build_dir=${BUILD_DIR}"
echo "auto_close_ms=${AUTO_CLOSE_MS}"
echo "timeout_ms=${TIMEOUT_MS}"

for target in "${SMOKE_TARGETS[@]}"; do
    if has_target "$target"; then
        run_target "$target"
    else
        echo "-- ${target} --"
        echo "status=skipped"
        echo "reason=target not available in current configure"
    fi
done

cleanup_old_logs "$KEEP_LOGS"
