#!/bin/sh
set -eu

READELF="$1"
MAJESTIC="$2"
COMPAT_SOURCE="$3"
shift 3

tmp="${TMPDIR:-/tmp}/majestic-imports.$"
unsupported="$tmp.unsupported"
providers="$tmp.providers"
transitive="$tmp.transitive"
trap 'rm -f "$tmp" "$unsupported" "$providers" "$transitive"' EXIT HUP INT TERM

fullhan_undefined()
{
    "$READELF" -Ws "$1" |
    awk '$7 == "UND" && $8 != "" {
        n=$8
        sub(/@.*/, "", n)
        if (n ~ /^(FH_|FHAdv_|API_ISP_|_JPEG_|_VENC_|_fh_sys_|mipi_)/)
            print n
    }'
}

fullhan_undefined "$MAJESTIC" | sort -u > "$tmp"

# Exported compatibility stubs are loader-compatible but not feature support.
# Keep them visible for ABI diagnostics, but fail the build if either Majestic
# itself or any selected donor feature library starts depending on one.
sed -n     -e 's/.*SIMPLE_STUB0(\([A-Za-z_][A-Za-z0-9_]*\)).*/\1/p'     -e 's/.*unsupported_feature("\([A-Za-z_][A-Za-z0-9_]*\)").*/\1/p'     "$COMPAT_SOURCE" | sort -u > "$unsupported"

: > "$providers"
: > "$transitive"
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

    # Optional feature libraries (motion/OSD/ISP/etc.) are part of the actual
    # runtime closure even when Majestic does not directly import their lower
    # Fullhan symbols. Guard those dependencies as well.
    fullhan_undefined "$lib" >> "$transitive"
done
sort -u -o "$providers" "$providers"
sort -u -o "$transitive" "$transitive"

missing=0
check_imports()
{
    origin="$1"
    file="$2"

    while IFS= read -r sym; do
        [ -n "$sym" ] || continue

        if grep -Fxq "$sym" "$unsupported"; then
            echo "Majestic ABI check: $origin reached unsupported FH8626 API: $sym" >&2
            missing=1
            continue
        fi

        if ! grep -Fxq "$sym" "$providers"; then
            echo "Majestic ABI check: unresolved Fullhan import from $origin: $sym" >&2
            missing=1
        fi
    done < "$file"
}

check_imports "Majestic direct import" "$tmp"
check_imports "selected donor closure" "$transitive"

if [ "$missing" -ne 0 ]; then
    echo "Majestic ABI check FAILED: runtime feature closure exceeds FH8626 support" >&2
    exit 1
fi

echo "Majestic ABI check: direct and transitive Fullhan imports are supported"
