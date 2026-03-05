#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="${ROOT_DIR}/local_logs"
BUILD_DIR="build"
KEEP_LOGS="${CROFT_LOG_KEEP:-40}"
LABEL="local-run"

usage() {
    cat <<'USAGE'
Usage:
  tools/run_logged.sh [options]
  tools/run_logged.sh [options] -- <command> [args...]

Options:
  --build-dir <dir>   Build directory for default workflow (default: build)
  --label <name>      Label segment in the log filename (default: local-run)
  --keep <count>      Keep newest N log files, delete older ones (default: 40)
  --help              Show this help

Default workflow (when no command is provided after --):
  1) cmake -B <build-dir> -DCROFT_BUILD_TESTS=ON
  2) cmake --build <build-dir>
  3) ctest --test-dir <build-dir> --output-on-failure

Examples:
  tools/run_logged.sh
  tools/run_logged.sh --label full-suite
  tools/run_logged.sh -- ctest --test-dir build -R sapling_test_seq --output-on-failure
USAGE
}

sanitize() {
    printf '%s' "$1" | tr '/[:space:]' '__' | tr -cd '[:alnum:]_.-'
}

run_cmd() {
    local log_file="$1"
    shift
    {
        printf '$'
        for arg in "$@"; do
            printf ' %q' "$arg"
        done
        printf '\n'
    } | tee -a "$log_file"
    set +e
    "$@" 2>&1 | tee -a "$log_file"
    local rc=${PIPESTATUS[0]}
    set -e
    return "$rc"
}

cleanup_old_logs() {
    local keep="$1"
    if [[ "$keep" =~ ^[0-9]+$ ]] && (( keep >= 0 )); then
        shopt -s nullglob
        local all_logs=("${LOG_DIR}"/*.log)
        shopt -u nullglob
        if (( ${#all_logs[@]} > keep )); then
            local sorted=()
            while IFS= read -r line; do
                sorted+=("$line")
            done < <(ls -1t "${LOG_DIR}"/*.log)
            for ((i=keep; i<${#sorted[@]}; i++)); do
                rm -f "${sorted[i]}"
            done
        fi
    fi
}

CUSTOM_CMD=()
while (($#)); do
    case "$1" in
        --build-dir)
            BUILD_DIR="${2:?missing value for --build-dir}"
            shift 2
            ;;
        --label)
            LABEL="${2:?missing value for --label}"
            shift 2
            ;;
        --keep)
            KEEP_LOGS="${2:?missing value for --keep}"
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        --)
            shift
            CUSTOM_CMD=("$@")
            break
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

mkdir -p "$LOG_DIR"

GIT_DESC="$(git -C "$ROOT_DIR" describe --always --dirty --abbrev=7 2>/dev/null || echo no-git)"
STAMP="$(date +%Y-%B-%d-%H%M%S)"
SAFE_LABEL="$(sanitize "$LABEL")"
SAFE_DESC="$(sanitize "$GIT_DESC")"
LOG_FILE="${LOG_DIR}/${STAMP}--${SAFE_DESC}--${SAFE_LABEL}.log"

{
    echo "RUN_TIMESTAMP=${STAMP}"
    echo "GIT_DESCRIBE=${GIT_DESC}"
    echo "WORKDIR=${ROOT_DIR}"
    if ((${#CUSTOM_CMD[@]} > 0)); then
        echo "MODE=custom"
    else
        echo "MODE=default"
    fi
    echo "LOG_FILE=${LOG_FILE}"
    echo
} | tee -a "$LOG_FILE"

status=0
if ((${#CUSTOM_CMD[@]} > 0)); then
    if run_cmd "$LOG_FILE" "${CUSTOM_CMD[@]}"; then
        :
    else
        status=$?
    fi
else
    if run_cmd "$LOG_FILE" cmake -B "$BUILD_DIR" -DCROFT_BUILD_TESTS=ON; then
        :
    else
        status=$?
    fi
    if ((status == 0)); then
        if run_cmd "$LOG_FILE" cmake --build "$BUILD_DIR"; then
            :
        else
            status=$?
        fi
    fi
    if ((status == 0)); then
        if run_cmd "$LOG_FILE" ctest --test-dir "$BUILD_DIR" --output-on-failure; then
            :
        else
            status=$?
        fi
    fi
fi

{
    echo
    echo "EXIT_STATUS=${status}"
    echo "END_TIMESTAMP=$(date +%Y-%B-%d-%H%M%S)"
} | tee -a "$LOG_FILE"

cleanup_old_logs "$KEEP_LOGS"
echo "Log saved: ${LOG_FILE}"
exit "$status"
