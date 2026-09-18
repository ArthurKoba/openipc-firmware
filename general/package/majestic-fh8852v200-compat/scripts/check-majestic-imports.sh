#!/bin/sh
set -eu

READELF="$1"
MAJESTIC="$2"
shift 2

tmp="${TMPDIR:-/tmp}/majestic-imports.$$"
trap 'rm -f "$tmp" "$tmp.providers"' EXIT HUP INT TERM

"$READELF" -Ws "$MAJESTIC" |
awk '$7 == "UND" && $8 != "" {
    n=$8
    sub(/@.*/, "", n)
    if (n ~ /^(FH_|FHAdv_|API_ISP_|_JPEG_|_VENC_|_fh_sys_|mipi_)/)
        print n
}' | sort -u > "$tmp"

: > "$tmp.providers"
for lib in "$@"; do
    [ -r "$lib" ] || {
        echo "Majestic ABI check: provider missing: $lib" >&2
        exit 2
    }
    "$READELF" -Ws "$lib" |
    awk '$7 != "UND" && $8 != "" {
        n=$8
        sub(/@.*/, "", n)
        print n
    }' >> "$tmp.providers"
done
sort -u -o "$tmp.providers" "$tmp.providers"

missing=0
while IFS= read -r sym; do
    [ -n "$sym" ] || continue
    if ! grep -Fxq "$sym" "$tmp.providers"; then
        echo "Majestic ABI check: unresolved Fullhan import: $sym" >&2
        missing=1
    fi
done < "$tmp"

if [ "$missing" -ne 0 ]; then
    echo "Majestic ABI check FAILED: current binary exceeds FH8626 compatibility closure" >&2
    exit 1
fi

echo "Majestic ABI check: all direct Fullhan imports have selected providers"
