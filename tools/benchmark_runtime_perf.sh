#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_ROOT="${ROOT_DIR}/local_logs/runtime_bench"
RUNS_DIR="${LOG_ROOT}/runs"
HISTORY_CSV="${LOG_ROOT}/history.csv"
RUNNER_SRC="${ROOT_DIR}/tools/runtime_bench_runner.c"
RUNNER_BIN="${LOG_ROOT}/runtime_bench_runner"
BUILD_DIR="build"
ITERATIONS="${CROFT_RUNTIME_BENCH_ITERATIONS:-3}"
TIMEOUT_SECS="${CROFT_RUNTIME_BENCH_TIMEOUT_SECS:-3.0}"
AUTO_CLOSE_MS="${CROFT_RUNTIME_BENCH_AUTO_CLOSE_MS:-400}"
KEEP_LOGS="${CROFT_RUNTIME_BENCH_KEEP:-80}"

TARGETS=()

usage() {
    cat <<'USAGE'
Usage:
  tools/benchmark_runtime_perf.sh [options]

Builds and runs a selected example set repeatedly, recording wall-clock runtime
and any `frames=` telemetry emitted by the sample. GUI samples use short
auto-close timeouts where supported. On this macOS host, windowed GUI samples
must be run as direct top-level terminal commands; the shell harness prints the
exact command instead of attempting a wrapped launch.

Options:
  --target <cmake-target>      Benchmark one target. May be repeated.
  --targets <a,b,c>            Benchmark a comma-separated list of targets.
  --build-dir <dir>            Build directory to use (default: build)
  --iterations <count>         Number of runs per target (default: 3)
  --timeout <seconds>          Per-run timeout (default: 3.0)
  --auto-close-ms <ms>         Auto-close duration for GUI samples (default: 400)
  --keep <count>               Keep newest N run logs (default: 80)
  --help                       Show this help
USAGE
}

append_target() {
    local target="$1"
    local existing

    [[ -n "$target" ]] || return
    for existing in "${TARGETS[@]:-}"; do
        if [[ "$existing" == "$target" ]]; then
            return
        fi
    done
    TARGETS+=("$target")
}

sanitize() {
    printf '%s' "$1" | tr '/[:space:]' '__' | tr -cd '[:alnum:]_.-,'
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

append_history_header_if_missing() {
    if [[ -f "$HISTORY_CSV" ]]; then
        return
    fi

    mkdir -p "$LOG_ROOT"
    cat >"$HISTORY_CSV" <<'CSV'
timestamp,git_describe,target,iteration,status,rc,wall_ms,frames,log_file
CSV
}

append_history_row() {
    local timestamp="$1"
    local git_desc="$2"
    local target="$3"
    local iteration="$4"
    local status="$5"
    local rc="$6"
    local wall_ms="$7"
    local frames="$8"
    local log_file="$9"

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$timestamp" "$git_desc" "$target" "$iteration" "$status" "$rc" "$wall_ms" "$frames" "$log_file" \
        >>"$HISTORY_CSV"
}

runtime_env_args() {
    local target="$1"
    case "$target" in
        example_wit_gpu_canvas)
            printf 'CROFT_WIT_GPU_AUTO_CLOSE_MS=%s\n' "$AUTO_CLOSE_MS"
            ;;
        example_wit_text_window)
            printf 'CROFT_WIT_TEXT_WINDOW_AUTO_CLOSE_MS=%s\n' "$AUTO_CLOSE_MS"
            ;;
        example_wit_textpad_window)
            printf 'CROFT_WIT_TEXTPAD_AUTO_CLOSE_MS=%s\n' "$AUTO_CLOSE_MS"
            ;;
        example_editor_text_metal_native)
            printf 'CROFT_EDITOR_AUTO_CLOSE_MS=%s\n' "$AUTO_CLOSE_MS"
            ;;
    esac
}

target_needs_terminal() {
    local target="$1"
    case "$target" in
        example_wit_gpu_canvas|example_wit_text_window|example_wit_textpad_window|example_editor_text_metal_native)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

print_cmd() {
    printf '$'
    local arg
    for arg in "$@"; do
        printf ' %q' "$arg"
    done
    printf '\n'
}

ensure_runner() {
    mkdir -p "$LOG_ROOT"
    if [[ ! -x "$RUNNER_BIN" || "$RUNNER_SRC" -nt "$RUNNER_BIN" ]]; then
        print_cmd cc -std=c99 -O2 -Wall -Wextra -o "$RUNNER_BIN" "$RUNNER_SRC"
        cc -std=c99 -O2 -Wall -Wextra -o "$RUNNER_BIN" "$RUNNER_SRC"
    fi
}

timeout_millis() {
    awk "BEGIN { printf \"%d\", (${TIMEOUT_SECS} * 1000.0) + 0.5 }"
}

extract_frames() {
    local log_file="$1"

    if [[ ! -f "$log_file" ]]; then
        return
    fi

    sed -n 's/.*frames=\([0-9][0-9]*\).*/\1/p' "$log_file" | tail -n 1
}

extract_wall_ms() {
    local log_file="$1"

    if [[ ! -f "$log_file" ]]; then
        return
    fi

    sed -n 's/.*wall_ms=\([0-9][0-9]*\).*/\1/p' "$log_file" | tail -n 1
}

print_gui_direct_command() {
    local binary="$1"
    local env_args=()
    local line

    while IFS= read -r line; do
        [[ -n "$line" ]] || continue
        env_args+=("$line")
    done < <(runtime_env_args "$(basename "$binary")")

    if (( ${#env_args[@]} > 0 )); then
        print_cmd env "${env_args[@]}" "$binary"
    else
        print_cmd "$binary"
    fi
}

run_iteration() {
    local target="$1"
    local iteration="$2"
    local binary="${ROOT_DIR}/${BUILD_DIR}/${target}"
    local safe_target
    local log_file
    local env_lines=()
    local env_args=()
    local status rc wall_ms frames
    local line
    local timeout_ms

    if [[ ! -x "$binary" ]]; then
        echo "ERROR: target binary not found: ${binary}" >&2
        return 1
    fi

    safe_target="$(sanitize "$target")"
    log_file="${RUNS_DIR}/${STAMP}--${SAFE_DESC}--${safe_target}--run${iteration}.log"

    while IFS= read -r line; do
        [[ -n "$line" ]] || continue
        env_lines+=("$line")
    done < <(runtime_env_args "$target")

    if target_needs_terminal "$target"; then
        {
            echo "manual_direct_terminal_required=1"
            echo "reason=wrapped_shell_launches_do_not_close_reliably_for_${target}_on_this_host"
        } >"$log_file"
        echo "ERROR: ${target} must be run as a direct top-level terminal command on this macOS host." >&2
        echo "Run this command directly from your terminal:" >&2
        print_gui_direct_command "$binary" >&2
        return 1
    fi

    timeout_ms="$(timeout_millis)"

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
        "$RUNNER_BIN" --timeout-ms "$timeout_ms" --log-file "$log_file" -- "$binary"
    )"

    status="$(printf '%s\n' "$summary" | awk -F= '/^status=/{print $2}')"
    rc="$(printf '%s\n' "$summary" | awk -F= '/^rc=/{print $2}')"
    wall_ms="$(printf '%s\n' "$summary" | awk -F= '/^wall_ms=/{print $2}')"

    wall_ms="${wall_ms:-}"
    frames="$(extract_frames "$log_file")"
    frames="${frames:-}"

    echo "-- ${target} run ${iteration} --"
    echo "status=${status}"
    echo "rc=${rc}"
    echo "wall_ms=${wall_ms}"
    if [[ -n "$frames" ]]; then
        echo "frames=${frames}"
    fi
    echo "log=${log_file}"

    append_history_row "$STAMP" "$GIT_DESC" "$target" "$iteration" "$status" "$rc" "$wall_ms" "$frames" "$log_file"
}

while (( "$#" )); do
    case "$1" in
        --target)
            append_target "${2:-}"
            shift 2
            ;;
        --targets)
            IFS=',' read -r -a parts <<< "${2:-}"
            for part in "${parts[@]}"; do
                append_target "$part"
            done
            shift 2
            ;;
        --build-dir)
            BUILD_DIR="${2:-}"
            shift 2
            ;;
        --iterations)
            ITERATIONS="${2:-}"
            shift 2
            ;;
        --timeout)
            TIMEOUT_SECS="${2:-}"
            shift 2
            ;;
        --auto-close-ms)
            AUTO_CLOSE_MS="${2:-}"
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

if (( ${#TARGETS[@]} == 0 )); then
    append_target "example_wit_text_cli"
    append_target "example_wit_text_wasm_host"
fi

if [[ ! "$ITERATIONS" =~ ^[0-9]+$ ]] || (( ITERATIONS < 1 )); then
    echo "ERROR: --iterations must be a positive integer" >&2
    exit 1
fi

mkdir -p "$RUNS_DIR"
append_history_header_if_missing
ensure_runner

GIT_DESC="$(git -C "$ROOT_DIR" describe --always --dirty --abbrev=7 2>/dev/null || echo unknown)"
STAMP="$(date +"%Y-%m-%d-%H%M%S")"
SAFE_DESC="$(sanitize "$GIT_DESC")"

print_cmd cmake --build "${ROOT_DIR}/${BUILD_DIR}" --target "${TARGETS[@]}"
cmake --build "${ROOT_DIR}/${BUILD_DIR}" --target "${TARGETS[@]}"

echo
echo "=== Runtime Benchmark ==="
echo "build_dir=${BUILD_DIR}"
echo "iterations=${ITERATIONS}"
echo "timeout_secs=${TIMEOUT_SECS}"
echo "auto_close_ms=${AUTO_CLOSE_MS}"
echo "targets=${TARGETS[*]}"

for target in "${TARGETS[@]}"; do
    for ((iteration = 1; iteration <= ITERATIONS; ++iteration)); do
        run_iteration "$target" "$iteration"
    done
done

cleanup_old_logs "$KEEP_LOGS"
