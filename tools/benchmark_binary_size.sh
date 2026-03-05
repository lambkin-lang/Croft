#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_ROOT="${ROOT_DIR}/local_logs/size_bench"
RUNS_DIR="${LOG_ROOT}/runs"
HISTORY_CSV="${LOG_ROOT}/history.csv"

TARGET="test_editor_standalone"
DEBUG_BUILD_DIR="build-size-debug"
OPT_BUILD_DIR="build-size-opt"
KEEP_LOGS="${CROFT_SIZE_BENCH_KEEP:-80}"
OPT_LEVEL="${CROFT_SIZE_BENCH_OPT_LEVEL:-Os}"

usage() {
    cat <<'USAGE'
Usage:
  tools/benchmark_binary_size.sh [options]

Builds one GUI target in:
  1) Debug
  2) Aggressive size-oriented optimized build

Then records binary sizes for tracking over time.

Options:
  --target <cmake-target>     Target to benchmark (default: test_editor_standalone)
  --debug-dir <dir>           Build dir for Debug profile (default: build-size-debug)
  --opt-dir <dir>             Build dir for optimized profile (default: build-size-opt)
  --keep <count>              Keep newest N run logs (default: 80)
  --opt-level <Os|Oz|O2|O3>   Compile optimization level for optimized profile (default: Os)
  --help                      Show this help

Environment overrides:
  CROFT_SIZE_BENCH_KEEP
  CROFT_SIZE_BENCH_OPT_LEVEL
USAGE
}

sanitize() {
    printf '%s' "$1" | tr '/[:space:]' '__' | tr -cd '[:alnum:]_.-'
}

bytes_of() {
    local f="$1"
    if stat -f%z "$f" >/dev/null 2>&1; then
        stat -f%z "$f"
    else
        wc -c < "$f" | tr -d '[:space:]'
    fi
}

cleanup_old_logs() {
    local keep="$1"
    if [[ ! "$keep" =~ ^[0-9]+$ ]]; then
        return
    fi
    shopt -s nullglob
    local all_logs=("${RUNS_DIR}"/*.log)
    shopt -u nullglob
    if (( ${#all_logs[@]} <= keep )); then
        return
    fi

    local sorted=()
    while IFS= read -r line; do
        sorted+=("$line")
    done < <(ls -1t "${RUNS_DIR}"/*.log)
    local i
    for ((i=keep; i<${#sorted[@]}; i++)); do
        rm -f "${sorted[i]}"
    done
}

print_cmd() {
    printf '$'
    local arg
    for arg in "$@"; do
        printf ' %q' "$arg"
    done
    printf '\n'
}

run_cmd() {
    print_cmd "$@"
    set +e
    "$@"
    local rc=$?
    set -e
    return "$rc"
}

append_history_header_if_missing() {
    if [[ -f "$HISTORY_CSV" ]]; then
        return
    fi
    cat >"$HISTORY_CSV" <<'CSV'
timestamp,git_describe,target,profile,build_dir,build_type,opt_level,lto,dead_strip,binary_relpath,binary_bytes,stripped_bytes,log_file
CSV
}

append_history_row() {
    local timestamp="$1"
    local git_desc="$2"
    local target="$3"
    local profile="$4"
    local build_dir="$5"
    local build_type="$6"
    local opt_level="$7"
    local lto="$8"
    local dead_strip="$9"
    local binary_relpath="${10}"
    local binary_bytes="${11}"
    local stripped_bytes="${12}"
    local log_file="${13}"

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$timestamp" \
        "$git_desc" \
        "$target" \
        "$profile" \
        "$build_dir" \
        "$build_type" \
        "$opt_level" \
        "$lto" \
        "$dead_strip" \
        "$binary_relpath" \
        "$binary_bytes" \
        "$stripped_bytes" \
        "$log_file" >>"$HISTORY_CSV"
}

build_and_measure() {
    local profile="$1"
    local build_dir="$2"
    local build_type="$3"
    local opt_level="$4"
    local lto="$5"
    local dead_strip="$6"
    shift 6
    local extra_cmake_args=("$@")

    local abs_build_dir="${ROOT_DIR}/${build_dir}"
    local binary_path="${abs_build_dir}/${TARGET}"
    local binary_relpath="${build_dir}/${TARGET}"

    echo
    echo "=== Profile: ${profile} ==="
    echo "build_dir=${build_dir}"
    echo "build_type=${build_type}"
    echo "opt_level=${opt_level}"
    echo "lto=${lto}"
    echo "dead_strip=${dead_strip}"

    local cmake_config_cmd=(
        cmake -S "$ROOT_DIR" -B "$abs_build_dir"
        -DCROFT_BUILD_TESTS=ON
        -DCROFT_ENABLE_UI=ON
        -DCROFT_ENABLE_WASM=OFF
        -DCROFT_ENABLE_AUDIO=OFF
        -DCROFT_ENABLE_ACCESSIBILITY=ON
        -DCMAKE_BUILD_TYPE="$build_type"
    )
    if (( ${#extra_cmake_args[@]} > 0 )); then
        cmake_config_cmd+=("${extra_cmake_args[@]}")
    fi
    run_cmd "${cmake_config_cmd[@]}"

    run_cmd cmake --build "$abs_build_dir" --target clean
    run_cmd cmake --build "$abs_build_dir" --target "$TARGET"

    if [[ ! -f "$binary_path" ]]; then
        echo "ERROR: target binary not found: ${binary_path}" >&2
        return 1
    fi

    local binary_bytes
    binary_bytes="$(bytes_of "$binary_path")"

    local stripped_copy="${RUNS_DIR}/${STAMP}--${SAFE_DESC}--${SAFE_TARGET}--${profile}.stripped"
    cp "$binary_path" "$stripped_copy"
    if command -v strip >/dev/null 2>&1; then
        strip -S -x "$stripped_copy" >/dev/null 2>&1 || true
    fi
    local stripped_bytes
    stripped_bytes="$(bytes_of "$stripped_copy")"

    echo "binary=${binary_relpath}"
    echo "binary_bytes=${binary_bytes}"
    echo "stripped_bytes=${stripped_bytes}"

    if command -v file >/dev/null 2>&1; then
        echo "-- file output --"
        file "$binary_path" || true
    fi
    if command -v size >/dev/null 2>&1; then
        echo "-- size output --"
        size "$binary_path" || true
        size -m "$binary_path" || true
    fi

    append_history_row \
        "$STAMP" \
        "$GIT_DESC" \
        "$TARGET" \
        "$profile" \
        "$build_dir" \
        "$build_type" \
        "$opt_level" \
        "$lto" \
        "$dead_strip" \
        "$binary_relpath" \
        "$binary_bytes" \
        "$stripped_bytes" \
        "$(basename "$LOG_FILE")"
}

while (($#)); do
    case "$1" in
        --target)
            TARGET="${2:?missing value for --target}"
            shift 2
            ;;
        --debug-dir)
            DEBUG_BUILD_DIR="${2:?missing value for --debug-dir}"
            shift 2
            ;;
        --opt-dir)
            OPT_BUILD_DIR="${2:?missing value for --opt-dir}"
            shift 2
            ;;
        --keep)
            KEEP_LOGS="${2:?missing value for --keep}"
            shift 2
            ;;
        --opt-level)
            OPT_LEVEL="${2:?missing value for --opt-level}"
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

case "$OPT_LEVEL" in
    Os|Oz|O2|O3) ;;
    *)
        echo "Unsupported --opt-level: ${OPT_LEVEL} (expected Os|Oz|O2|O3)" >&2
        exit 2
        ;;
esac

mkdir -p "$RUNS_DIR"

GIT_DESC="$(git -C "$ROOT_DIR" describe --always --dirty --abbrev=7 2>/dev/null || echo no-git)"
STAMP="$(date +%Y-%B-%d-%H%M%S)"
SAFE_DESC="$(sanitize "$GIT_DESC")"
SAFE_TARGET="$(sanitize "$TARGET")"
LOG_FILE="${RUNS_DIR}/${STAMP}--${SAFE_DESC}--${SAFE_TARGET}.log"

exec > >(tee -a "$LOG_FILE") 2>&1

echo "RUN_TIMESTAMP=${STAMP}"
echo "GIT_DESCRIBE=${GIT_DESC}"
echo "TARGET=${TARGET}"
echo "LOG_FILE=${LOG_FILE}"
echo

append_history_header_if_missing

status=0

if build_and_measure "debug" "$DEBUG_BUILD_DIR" "Debug" "debug" "off" "off"; then
    :
else
    status=$?
fi

if ((status == 0)); then
    OPT_CFLAGS="-${OPT_LEVEL} -DNDEBUG -ffunction-sections -fdata-sections"
    OPT_CXXFLAGS="-${OPT_LEVEL} -DNDEBUG -ffunction-sections -fdata-sections"
    OPT_LDFLAGS="-Wl,-dead_strip -Wl,-x"
    if build_and_measure "optimized" "$OPT_BUILD_DIR" "MinSizeRel" "$OPT_LEVEL" "on" "on" \
        -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
        -DCMAKE_C_FLAGS_MINSIZEREL="$OPT_CFLAGS" \
        -DCMAKE_CXX_FLAGS_MINSIZEREL="$OPT_CXXFLAGS" \
        -DCMAKE_EXE_LINKER_FLAGS_MINSIZEREL="$OPT_LDFLAGS"; then
        :
    else
        status=$?
    fi
fi

echo
echo "EXIT_STATUS=${status}"
echo "END_TIMESTAMP=$(date +%Y-%B-%d-%H%M%S)"

cleanup_old_logs "$KEEP_LOGS"
echo "History CSV: ${HISTORY_CSV}"
echo "Run log: ${LOG_FILE}"
exit "$status"
