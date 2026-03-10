#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "usage: $0 <repo-root> <report-txt> <report-tsv>" >&2
    exit 1
fi

repo_root=$1
report_txt=$2
report_tsv=$3

cd "$repo_root"

mkdir -p "$(dirname "$report_txt")"
mkdir -p "$(dirname "$report_tsv")"

if command -v rg >/dev/null 2>&1; then
    use_rg=1
else
    use_rg=0
fi

scan_paths=(
    src
    include
    tools
    CMakeLists.txt
)

categories=(
    "libc-memory|\\b(malloc|calloc|realloc|free)\\s*\\("
    "libc-stdio|#include <stdio\\.h>|\\b(FILE\\s*\\*|fopen|freopen|fdopen|fclose|fread|fwrite|fflush|fprintf|snprintf|vsnprintf)\\s*\\("
    "libc-strings|#include <string\\.h>|\\b(memcpy|memmove|memcmp|memset|strlen|strcmp|strncmp|strchr|strrchr)\\s*\\("
    "posix-filesystem|#include <(unistd|fcntl|dirent|sys/stat|sys/types)\\.h>|\\b(open|close|read|write|lseek|stat|fstat|mkdir|rmdir|unlink|opendir|readdir|closedir|getcwd)\\s*\\("
    "posix-process-env|\\benviron\\b|\\b(getenv|setenv|unsetenv)\\s*\\("
    "system-random|#include <sys/random\\.h>|\\b(arc4random_buf|getrandom)\\s*\\("
    "posix-threads|#include <pthread\\.h>|\\bpthread_[A-Za-z0-9_]+\\s*\\("
    "posix-time|#include <time\\.h>|\\b(clock_gettime|clock_getres|nanosleep|gettimeofday|localtime_r|gmtime_r|tzset)\\s*\\("
    "apple-frameworks|#import <(AppKit|Metal|QuartzCore|Foundation|CoreText|CoreGraphics)/[^>]+>|-framework (AppKit|Metal|QuartzCore|OpenGL)"
)

{
    printf "# Croft libc and platform dependency audit\n"
    printf "\n"
    printf "Scope: src/, include/, tools/, and CMakeLists.txt\n"
    printf "Tests are intentionally excluded so this reflects the build/runtime surface.\n"
    printf "\n"
    printf "Category summary:\n"
} >"$report_txt"

printf "category\tfile\tline\tsnippet\n" >"$report_tsv"

for entry in "${categories[@]}"; do
    category=${entry%%|*}
    pattern=${entry#*|}
    tmp=$(mktemp)

    if [ "$use_rg" -eq 1 ]; then
        if rg -n -H --no-heading -e "$pattern" "${scan_paths[@]}" >"$tmp" 2>/dev/null; then
            :
        else
            : >"$tmp"
        fi
    elif grep -RInH -E "$pattern" "${scan_paths[@]}" >"$tmp" 2>/dev/null; then
        :
    else
        : >"$tmp"
    fi

    match_count=$(wc -l <"$tmp" | tr -d ' ')
    if [ "$match_count" -eq 0 ]; then
        rm -f "$tmp"
        continue
    fi

    file_count=$(cut -d: -f1 "$tmp" | sort -u | wc -l | tr -d ' ')
    printf -- "- %s: %s matches across %s files\n" "$category" "$match_count" "$file_count" >>"$report_txt"

    {
        printf "\n## %s\n" "$category"
        cat "$tmp"
    } >>"$report_txt"

    while IFS= read -r line; do
        file_path=${line%%:*}
        remainder=${line#*:}
        line_no=${remainder%%:*}
        snippet=${remainder#*:}
        snippet=$(printf '%s' "$snippet" | tr '\t' ' ' | tr '\r' ' ' | sed 's/[[:space:]]\+/ /g')
        printf "%s\t%s\t%s\t%s\n" "$category" "$file_path" "$line_no" "$snippet" >>"$report_tsv"
    done <"$tmp"

    rm -f "$tmp"
done
