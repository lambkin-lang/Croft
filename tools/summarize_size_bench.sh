#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HISTORY_CSV="${ROOT_DIR}/local_logs/size_bench/history.csv"
LIMIT=20
TARGET=""

usage() {
    cat <<'USAGE'
Usage:
  tools/summarize_size_bench.sh [options]

Options:
  --limit <N>      Show latest N rows (default: 20)
  --target <name>  Filter by target name
  --help           Show help
USAGE
}

while (($#)); do
    case "$1" in
        --limit)
            LIMIT="${2:?missing value for --limit}"
            shift 2
            ;;
        --target)
            TARGET="${2:?missing value for --target}"
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

if [[ ! -f "$HISTORY_CSV" ]]; then
    echo "No size benchmark history found: $HISTORY_CSV"
    exit 0
fi

if ! [[ "$LIMIT" =~ ^[0-9]+$ ]]; then
    echo "--limit must be a non-negative integer" >&2
    exit 2
fi

tmp_rows="$(mktemp)"
trap 'rm -f "$tmp_rows"' EXIT

if [[ -n "$TARGET" ]]; then
    awk -F, -v t="$TARGET" 'NR==1 || $3==t' "$HISTORY_CSV" >"$tmp_rows"
else
    cat "$HISTORY_CSV" >"$tmp_rows"
fi

line_count="$(wc -l <"$tmp_rows" | tr -d '[:space:]')"
if [[ "$line_count" -le 1 ]]; then
    echo "No matching rows."
    exit 0
fi

echo "History file: $HISTORY_CSV"
if [[ -n "$TARGET" ]]; then
    echo "Filter target: $TARGET"
fi
echo

printf '%-22s  %-16s  %-24s  %-10s  %-11s  %-12s  %s\n' \
    "timestamp" "git" "target" "profile" "bytes" "stripped" "log"
printf '%-22s  %-16s  %-24s  %-10s  %-11s  %-12s  %s\n' \
    "----------------------" "----------------" "------------------------" "----------" "-----------" "------------" "---"

tail -n "$LIMIT" "$tmp_rows" | awk -F, 'NR>1 {
    printf "%-22s  %-16s  %-24s  %-10s  %-11s  %-12s  %s\n",
        $1, $2, $3, $4, $11, $12, $13
}'

echo
echo "Latest by profile:"
awk -F, '
NR==1 { next }
{
    key = $4;
    ts[key] = $1;
    git[key] = $2;
    target[key] = $3;
    bytes[key] = $11 + 0;
    stripped[key] = $12 + 0;
}
END {
    for (k in bytes) {
        printf "  profile=%s target=%s git=%s ts=%s bytes=%d stripped=%d\n",
            k, target[k], git[k], ts[k], bytes[k], stripped[k];
    }
}
' "$tmp_rows"
