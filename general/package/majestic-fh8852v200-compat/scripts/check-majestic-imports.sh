#!/bin/sh
set -eu

READELF="$1"
MAJESTIC="$2"
COMPAT_SOURCE="$3"
shift 3

tmp="${TMPDIR:-/tmp}/majestic-imports.$$"
unsupported="$tmp.unsupported"
providers="$tmp.providers"
trap 'rm -f "$tmp" "$unsupported" "$providers"' EXIT HUP INT TERM

"$READELF" -Ws "$MAJESTIC" |
awk '$7 == "UND" && $8 != "" {
    n=$8
    sub(/@.*/, "", n)
    if (n ~ /^(FH_|FHAdv_|API_ISP_|_JPEG_|_VENC_|_fh_sys_|mipi_)/)
        print n
}' | sort -u > "$tmp"

# Exported compatibility stubs are loader-compatible but not feature support.
# Keep them visible for ABI diagnostics, but make a moving Majestic master
# fail the build if it starts directly depending on one.
sed -n     -e 's/.*SIMPLE_STUB0(\([A-Za-z_][A-Za-z0-9_]*\)).*/\1/p'     -e 's/.*unsupported_feature("\([A-Za-z_][A-Za-z0-9_]*\)").*/\1/p'     "$COMPAT_SOURCE" | sort -u > "$unsupported"

: > "$providers"
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
    }' >> "$providers"
done
sort -u -o "$providers" "$providers"

missing=0
while IFS= read -r sym; do
    [ -n "$sym" ] || continue

    if grep -Fxq "$sym" "$unsupported"; then
        echo "Majestic ABI check: direct import reached unsupported FH8626 API: $sym" >&2
        missing=1
        continue
    fi

    if ! grep -Fxq "$sym" "$providers"; then
        echo "Majestic ABI check: unresolved Fullhan import: $sym" >&2
        missing=1
    fi
done < "$tmp"

if [ "$missing" -ne 0 ]; then
    echo "Majestic ABI check FAILED: current binary exceeds FH8626 supported closure" >&2
    exit 1
fi

echo "Majestic ABI check: all direct Fullhan imports are supported by selected providers"
