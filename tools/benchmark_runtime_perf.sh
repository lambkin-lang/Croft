#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_ROOT="${ROOT_DIR}/local_logs/runtime_bench"
RUNS_DIR="${LOG_ROOT}/runs"
HISTORY_CSV="${LOG_ROOT}/history.csv"
HISTORY_HEADER="timestamp,git_describe,target,iteration,status,rc,wall_ms,sample_wall_ms,frames,log_file"
RUNNER_SRC="${ROOT_DIR}/tools/runtime_bench_runner.c"
RUNNER_BIN="${LOG_ROOT}/runtime_bench_runner"
BUILD_DIR="build"
ITERATIONS="${CROFT_RUNTIME_BENCH_ITERATIONS:-3}"
TIMEOUT_SECS="${CROFT_RUNTIME_BENCH_TIMEOUT_SECS:-3.0}"
AUTO_CLOSE_MS="${CROFT_RUNTIME_BENCH_AUTO_CLOSE_MS:-400}"
KEEP_LOGS="${CROFT_RUNTIME_BENCH_KEEP:-80}"
EDITOR_DOC_PATH="${CROFT_RUNTIME_BENCH_EDITOR_DOC:-}"
EDITOR_LINE_COUNT="${CROFT_RUNTIME_BENCH_EDITOR_LINES:-0}"
EDITOR_PROFILE="${CROFT_RUNTIME_BENCH_EDITOR_PROFILE:-0}"
EDITOR_DOC_DIR="${LOG_ROOT}/editor_docs"

TARGETS=()

usage() {
    cat <<'USAGE'
Usage:
  tools/benchmark_runtime_perf.sh [options]

Builds and runs a selected example set repeatedly, recording wall-clock runtime
and any `frames=` telemetry emitted by the sample. GUI samples use short
auto-close timeouts where supported. Editor-family targets can also open a
shared benchmark document so runtime comparisons use the same content.

Options:
  --target <cmake-target>      Benchmark one target. May be repeated.
  --targets <a,b,c>            Benchmark a comma-separated list of targets.
  --build-dir <dir>            Build directory to use (default: build)
  --iterations <count>         Number of runs per target (default: 3)
  --timeout <seconds>          Per-run timeout (default: 3.0)
  --auto-close-ms <ms>         Auto-close duration for GUI samples (default: 400)
  --editor-doc <path>          Document path for editor-family targets.
  --editor-lines <count>       Generate a shared editor fixture with this many lines.
  --editor-profile             Enable scene editor profiling telemetry.
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
    mkdir -p "$LOG_ROOT"

    if [[ ! -f "$HISTORY_CSV" ]]; then
        printf '%s\n' "$HISTORY_HEADER" >"$HISTORY_CSV"
        return
    fi

    {
        IFS= read -r header || header=""
    } <"$HISTORY_CSV"

    if [[ "$header" == "$HISTORY_HEADER" ]]; then
        return
    fi

    if [[ "$header" == "timestamp,git_describe,target,iteration,status,rc,wall_ms,frames,log_file" ]]; then
        local tmp_csv
        tmp_csv="${HISTORY_CSV}.tmp"
        {
            printf '%s\n' "$HISTORY_HEADER"
            tail -n +2 "$HISTORY_CSV" | awk -F, 'BEGIN { OFS="," } NF >= 9 { print $1, $2, $3, $4, $5, $6, $7, "", $8, $9 }'
        } >"$tmp_csv"
        mv "$tmp_csv" "$HISTORY_CSV"
        return
    fi

    echo "ERROR: unsupported runtime benchmark history schema in ${HISTORY_CSV}" >&2
    exit 1
}

append_history_row() {
    local timestamp="$1"
    local git_desc="$2"
    local target="$3"
    local iteration="$4"
    local status="$5"
    local rc="$6"
    local wall_ms="$7"
    local sample_wall_ms="$8"
    local frames="$9"
    local log_file="${10}"

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$timestamp" "$git_desc" "$target" "$iteration" "$status" "$rc" "$wall_ms" "$sample_wall_ms" "$frames" "$log_file" \
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
        example_render_canvas_opengl|example_render_canvas_metal|example_render_canvas_metal_native)
            printf 'CROFT_RENDER_AUTO_CLOSE_MS=%s\n' "$AUTO_CLOSE_MS"
            if [[ "$EDITOR_PROFILE" == "1" ]]; then
                printf 'CROFT_RENDER_PROFILE=1\n'
            fi
            ;;
        example_editor_text|example_editor_text_appkit|example_editor_text_metal_native)
            printf 'CROFT_EDITOR_AUTO_CLOSE_MS=%s\n' "$AUTO_CLOSE_MS"
            if [[ "$EDITOR_PROFILE" == "1" ]]; then
                printf 'CROFT_EDITOR_PROFILE=1\n'
            fi
            ;;
    esac
}

target_is_editor_family() {
    local target="$1"
    case "$target" in
        example_editor_text|example_editor_text_appkit|example_editor_text_metal_native)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

ensure_editor_doc() {
    local line_count="$1"
    local doc_path
    local tmp_path
    local line_index

    if [[ -n "$EDITOR_DOC_PATH" ]]; then
        if [[ ! -f "$EDITOR_DOC_PATH" ]]; then
            echo "ERROR: --editor-doc file not found: ${EDITOR_DOC_PATH}" >&2
            exit 1
        fi
        printf '%s\n' "$EDITOR_DOC_PATH"
        return
    fi

    if [[ ! "$line_count" =~ ^[0-9]+$ ]] || (( line_count < 1 )); then
        return
    fi

    mkdir -p "$EDITOR_DOC_DIR"
    doc_path="${EDITOR_DOC_DIR}/generated-${line_count}.txt"
    if [[ -f "$doc_path" ]]; then
        printf '%s\n' "$doc_path"
        return
    fi

    tmp_path="${doc_path}.tmp"
    {
        printf 'Croft editor runtime benchmark fixture\n'
        printf 'Generated for cross-family runtime comparison.\n\n'
        for ((line_index = 1; line_index <= line_count; ++line_index)); do
            case $(((line_index - 1) % 6)) in
                0)
                    printf 'section %04d alpha beta gamma\n' "$line_index"
                    ;;
                1)
                    printf '    item %04d delta epsilon zeta\n' "$line_index"
                    ;;
                2)
                    printf '        detail %04d brackets () [] {}\n' "$line_index"
                    ;;
                3)
                    printf '    note %04d search alpha indent\n' "$line_index"
                    ;;
                4)
                    printf 'tail %04d plain text for scrolling\n' "$line_index"
                    ;;
                5)
                    printf '\n'
                    ;;
            esac
        done
    } >"$tmp_path"
    mv "$tmp_path" "$doc_path"
    printf '%s\n' "$doc_path"
}

runtime_target_args() {
    local target="$1"
    local editor_doc

    if ! target_is_editor_family "$target"; then
        return
    fi

    editor_doc="$(ensure_editor_doc "$EDITOR_LINE_COUNT")"
    if [[ -n "$editor_doc" ]]; then
        printf '%s\n' "$editor_doc"
    fi
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

extract_profile_lines() {
    local log_file="$1"

    if [[ ! -f "$log_file" ]]; then
        return
    fi

    sed -n '/^editor-scene-frame /p;/^editor-render-profile /p;/^editor-scene-profile /p;/^render-profile /p' "$log_file"
}

run_iteration() {
    local target="$1"
    local iteration="$2"
    local binary="${ROOT_DIR}/${BUILD_DIR}/${target}"
    local safe_target
    local log_file
    local env_lines=()
    local env_args=()
    local target_args=()
    local status rc wall_ms frames sample_wall_ms
    local profile_lines
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

    while IFS= read -r line; do
        [[ -n "$line" ]] || continue
        target_args+=("$line")
    done < <(runtime_target_args "$target")

    timeout_ms="$(timeout_millis)"

    if (( ${#env_lines[@]} > 0 )); then
        env_args=(env)
        env_args+=("${env_lines[@]}")
        if (( ${#target_args[@]} > 0 )); then
            print_cmd "${env_args[@]}" "$binary" "${target_args[@]}"
        else
            print_cmd "${env_args[@]}" "$binary"
        fi
    else
        if (( ${#target_args[@]} > 0 )); then
            print_cmd "$binary" "${target_args[@]}"
        else
            print_cmd "$binary"
        fi
    fi

    summary="$(
        if (( ${#env_lines[@]} > 0 )); then
            for line in "${env_lines[@]}"; do
                export "$line"
            done
        fi
        if (( ${#target_args[@]} > 0 )); then
            "$RUNNER_BIN" --timeout-ms "$timeout_ms" --log-file "$log_file" -- "$binary" "${target_args[@]}"
        else
            "$RUNNER_BIN" --timeout-ms "$timeout_ms" --log-file "$log_file" -- "$binary"
        fi
    )"

    status="$(printf '%s\n' "$summary" | awk -F= '/^status=/{print $2}')"
    rc="$(printf '%s\n' "$summary" | awk -F= '/^rc=/{print $2}')"
    wall_ms="$(printf '%s\n' "$summary" | awk -F= '/^wall_ms=/{print $2}')"

    wall_ms="${wall_ms:-}"
    frames="$(extract_frames "$log_file")"
    frames="${frames:-}"
    sample_wall_ms="$(extract_wall_ms "$log_file")"
    sample_wall_ms="${sample_wall_ms:-}"
    profile_lines="$(extract_profile_lines "$log_file")"

    echo "-- ${target} run ${iteration} --"
    echo "status=${status}"
    echo "rc=${rc}"
    echo "wall_ms=${wall_ms}"
    if [[ -n "$sample_wall_ms" ]]; then
        echo "sample_wall_ms=${sample_wall_ms}"
    fi
    if [[ -n "$frames" ]]; then
        echo "frames=${frames}"
    fi
    if [[ -n "$profile_lines" ]]; then
        printf '%s\n' "$profile_lines"
    fi
    echo "log=${log_file}"

    append_history_row "$STAMP" "$GIT_DESC" "$target" "$iteration" "$status" "$rc" "$wall_ms" "$sample_wall_ms" "$frames" "$log_file"
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
        --editor-doc)
            EDITOR_DOC_PATH="${2:-}"
            shift 2
            ;;
        --editor-lines)
            EDITOR_LINE_COUNT="${2:-}"
            shift 2
            ;;
        --editor-profile)
            EDITOR_PROFILE="1"
            shift
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
if [[ ! "$EDITOR_LINE_COUNT" =~ ^[0-9]+$ ]]; then
    echo "ERROR: --editor-lines must be a non-negative integer" >&2
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
