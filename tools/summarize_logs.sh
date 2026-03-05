#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="${ROOT_DIR}/local_logs"
LIMIT=20

usage() {
    cat <<'USAGE'
Usage:
  tools/summarize_logs.sh [--limit <N>] [--log-dir <dir>]

Reads run logs from local_logs/ and prints a compact summary:
  timestamp | git describe | exit status | ctest summary (if present) | file
USAGE
}

while (($#)); do
    case "$1" in
        --limit)
            LIMIT="${2:?missing value for --limit}"
            shift 2
            ;;
        --log-dir)
            LOG_DIR="${2:?missing value for --log-dir}"
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

if [[ ! -d "$LOG_DIR" ]]; then
    echo "No log directory found: $LOG_DIR"
    exit 0
fi

if ! [[ "$LIMIT" =~ ^[0-9]+$ ]]; then
    echo "--limit must be a non-negative integer" >&2
    exit 2
fi

shopt -s nullglob
all_logs=("${LOG_DIR}"/*.log)
shopt -u nullglob

if ((${#all_logs[@]} == 0)); then
    echo "No logs found in: $LOG_DIR"
    exit 0
fi

sorted=()
while IFS= read -r line; do
    sorted+=("$line")
done < <(ls -1t "${LOG_DIR}"/*.log)

printf '%-22s  %-16s  %-6s  %-44s  %s\n' "timestamp" "git" "exit" "ctest" "file"
printf '%-22s  %-16s  %-6s  %-44s  %s\n' "----------------------" "----------------" "------" "--------------------------------------------" "----"

count=0
for log in "${sorted[@]}"; do
    timestamp="$(grep -m1 '^RUN_TIMESTAMP=' "$log" | cut -d'=' -f2- || true)"
    git_desc="$(grep -m1 '^GIT_DESCRIBE=' "$log" | cut -d'=' -f2- || true)"
    exit_status="$(grep -m1 '^EXIT_STATUS=' "$log" | cut -d'=' -f2- || true)"
    ctest_line="$(grep -E '^[0-9]+% tests passed, [0-9]+ tests failed out of [0-9]+' "$log" | tail -n1 || true)"

    [[ -n "$timestamp" ]] || timestamp="n/a"
    [[ -n "$git_desc" ]] || git_desc="n/a"
    [[ -n "$exit_status" ]] || exit_status="n/a"
    [[ -n "$ctest_line" ]] || ctest_line="(no ctest summary)"

    printf '%-22s  %-16s  %-6s  %-44s  %s\n' "$timestamp" "$git_desc" "$exit_status" "$ctest_line" "$(basename "$log")"

    count=$((count + 1))
    if ((count >= LIMIT)); then
        break
    fi
done
