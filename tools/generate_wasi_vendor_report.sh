#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 5 ] || [ "$#" -gt 6 ]; then
    echo "usage: $0 <repo-root> <vendor-proposals-dir> <overlay-root> <report-txt> <report-json> [external-proposals-dir|-]" >&2
    exit 1
fi

repo_root=$1
vendor_root=$2
overlay_root=$3
report_txt=$4
report_json=$5
external_root=${6:--}

cd "$repo_root"

mkdir -p "$(dirname "$report_txt")"
mkdir -p "$(dirname "$report_json")"

json_escape() {
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

json_array_from_lines() {
    local first=1
    printf '['
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        if [ "$first" -eq 0 ]; then
            printf ', '
        fi
        first=0
        printf '"%s"' "$(json_escape "$line")"
    done
    printf ']'
}

count_wit_files() {
    local dir=$1
    if [ -d "$dir" ]; then
        find "$dir" -type f -name '*.wit' | wc -l | tr -d ' '
    else
        printf '0'
    fi
}

list_subdirs() {
    local dir=$1
    if [ -d "$dir" ]; then
        find "$dir" -mindepth 1 -maxdepth 1 -type d -exec basename {} \; | sort
    fi
}

relative_wit_files() {
    local dir=$1
    if [ -d "$dir" ]; then
        find "$dir" -type f -name '*.wit' | sed "s#^$dir/##" | sort
    fi
}

join_lines_csv() {
    local first=1
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        if [ "$first" -eq 0 ]; then
            printf ', '
        fi
        first=0
        printf '%s' "$line"
    done
}

vendor_packages=$(list_subdirs "$vendor_root")
overlay_packages=$(list_subdirs "$overlay_root")

if [ "$external_root" != "-" ] && [ -d "$external_root" ]; then
    external_packages=$(list_subdirs "$external_root")
    external_enabled=1
    vendor_only_packages=$(comm -23 <(printf '%s\n' "$vendor_packages") <(printf '%s\n' "$external_packages"))
    external_only_packages=$(comm -13 <(printf '%s\n' "$vendor_packages") <(printf '%s\n' "$external_packages"))
else
    external_packages=""
    external_enabled=0
    vendor_only_packages=""
    external_only_packages=""
fi

{
    printf "# Croft WASI vendor drift report\n\n"
    printf "Vendor root: %s\n" "$vendor_root"
    printf "Overlay root: %s\n" "$overlay_root"
    if [ "$external_enabled" -eq 1 ]; then
        printf "External root: %s\n" "$external_root"
    else
        printf "External root: <not provided>\n"
    fi
    printf "\n"
    printf "Vendor packages (%s): %s\n" \
        "$(printf '%s\n' "$vendor_packages" | grep -c . || true)" \
        "$(printf '%s\n' "$vendor_packages" | join_lines_csv)"
    printf "Overlay packages (%s): %s\n" \
        "$(printf '%s\n' "$overlay_packages" | grep -c . || true)" \
        "$(printf '%s\n' "$overlay_packages" | join_lines_csv)"
    if [ "$external_enabled" -eq 1 ]; then
        printf "External packages (%s): %s\n" \
            "$(printf '%s\n' "$external_packages" | grep -c . || true)" \
            "$(printf '%s\n' "$external_packages" | join_lines_csv)"
        printf "Vendor-only packages: %s\n" "$(printf '%s\n' "$vendor_only_packages" | join_lines_csv)"
        printf "External-only packages: %s\n" "$(printf '%s\n' "$external_only_packages" | join_lines_csv)"
    fi
    printf "\n## Per-package summary\n"
} >"$report_txt"

json_packages_tmp=$(mktemp)
first_package=1
printf '[' >"$json_packages_tmp"

while IFS= read -r package; do
    [ -n "$package" ] || continue

    vendor_wit_dir="$vendor_root/$package/wit"
    overlay_wit_dir="$overlay_root/$package/wit"
    external_wit_dir="$external_root/$package/wit"

    vendor_count=$(count_wit_files "$vendor_wit_dir")
    overlay_count=$(count_wit_files "$overlay_wit_dir")
    overlay_files=$(relative_wit_files "$overlay_wit_dir")

    upstream_status="not-checked"
    upstream_diff_lines=""
    upstream_diff_count=0
    if [ "$external_enabled" -eq 1 ]; then
        if [ ! -d "$external_wit_dir" ]; then
            upstream_status="missing-upstream"
        else
            diff_tmp=$(mktemp)
            if diff -qr "$vendor_wit_dir" "$external_wit_dir" >"$diff_tmp" 2>&1; then
                upstream_status="match"
                upstream_diff_count=0
            else
                upstream_status="diff"
                upstream_diff_lines=$(sed "s#^$vendor_wit_dir#vendor/$package/wit#g; s#^$external_wit_dir#external/$package/wit#g" "$diff_tmp")
                upstream_diff_count=$(printf '%s\n' "$upstream_diff_lines" | grep -c . || true)
            fi
            rm -f "$diff_tmp"
        fi
    fi

    {
        printf -- "- %s: vendor=%s .wit files, overlay=%s .wit files, upstream=%s" \
            "$package" "$vendor_count" "$overlay_count" "$upstream_status"
        if [ "$upstream_diff_count" -gt 0 ]; then
            printf " (%s differing paths)" "$upstream_diff_count"
        fi
        printf "\n"
        if [ "$overlay_count" -gt 0 ]; then
            printf "  overlay files: %s\n" "$(printf '%s\n' "$overlay_files" | join_lines_csv)"
        fi
        if [ "$upstream_diff_count" -gt 0 ]; then
            printf "  upstream drift:\n"
            while IFS= read -r line; do
                [ -n "$line" ] || continue
                printf "    %s\n" "$line"
            done <<<"$upstream_diff_lines"
        fi
    } >>"$report_txt"

    if [ "$first_package" -eq 0 ]; then
        printf ',\n' >>"$json_packages_tmp"
    fi
    first_package=0
    printf '    {\n' >>"$json_packages_tmp"
    printf '      "name": "%s",\n' "$(json_escape "$package")" >>"$json_packages_tmp"
    printf '      "vendor_wit_count": %s,\n' "$vendor_count" >>"$json_packages_tmp"
    printf '      "overlay_wit_count": %s,\n' "$overlay_count" >>"$json_packages_tmp"
    printf '      "upstream_status": "%s",\n' "$(json_escape "$upstream_status")" >>"$json_packages_tmp"
    printf '      "upstream_difference_count": %s,\n' "$upstream_diff_count" >>"$json_packages_tmp"
    printf '      "overlay_files": %s,\n' "$(printf '%s\n' "$overlay_files" | json_array_from_lines)" >>"$json_packages_tmp"
    printf '      "upstream_differences": %s\n' "$(printf '%s\n' "$upstream_diff_lines" | json_array_from_lines)" >>"$json_packages_tmp"
    printf '    }' >>"$json_packages_tmp"
done <<<"$vendor_packages"

printf '\n  ]\n' >>"$json_packages_tmp"

{
    printf '{\n'
    printf '  "vendor_root": "%s",\n' "$(json_escape "$vendor_root")"
    if [ "$external_enabled" -eq 1 ]; then
        printf '  "external_root": "%s",\n' "$(json_escape "$external_root")"
    else
        printf '  "external_root": null,\n'
    fi
    printf '  "overlay_root": "%s",\n' "$(json_escape "$overlay_root")"
    printf '  "vendor_packages": %s,\n' "$(printf '%s\n' "$vendor_packages" | json_array_from_lines)"
    printf '  "overlay_packages": %s,\n' "$(printf '%s\n' "$overlay_packages" | json_array_from_lines)"
    printf '  "vendor_only_packages": %s,\n' "$(printf '%s\n' "$vendor_only_packages" | json_array_from_lines)"
    printf '  "external_only_packages": %s,\n' "$(printf '%s\n' "$external_only_packages" | json_array_from_lines)"
    printf '  "packages":\n'
    cat "$json_packages_tmp"
    printf '}\n'
} >"$report_json"

rm -f "$json_packages_tmp"
