#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
AWB=$(cd "$HERE/.." && pwd)
OUT=${TMPDIR:-/tmp}/fh8626_test_publish_commit_state.$$
CA4=${TMPDIR:-/tmp}/fh8626_test_ca4f4_partial_grid.$$
CAF=${TMPDIR:-/tmp}/fh8626_test_cafc0_controls.$$
WORD=${TMPDIR:-/tmp}/fh8626_test_awb_words.$$
DISPATCH=${TMPDIR:-/tmp}/fh8626_test_awb_dispatch.$$
trap 'rm -f "$OUT" "$CA4" "$CAF" "$WORD" "$DISPATCH"' EXIT

cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=undefined \
  -I"$AWB" \
  "$HERE/test_publish_commit_state.c" \
  "$AWB/fh8626_stock_awb_mode0_pipeline.c" \
  "$AWB/fh8626_stock_awb_mode0_ref.c" \
  "$AWB/fh8626_stock_c9f68_ref.c" \
  "$AWB/../diagnostics/fh8626_stock_ca4f4_diag.c" \
  -o "$OUT"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$OUT"

cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=undefined \
  -I"$AWB" -I"$AWB/../diagnostics" \
  "$HERE/test_ca4f4_partial_grid.c" \
  "$AWB/../diagnostics/fh8626_stock_ca4f4_diag.c" \
  "$AWB/fh8626_stock_awb_mode0_ref.c" \
  -o "$CA4"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$CA4"

cc -std=c11 -O2 -Wall -Wextra -Werror -fno-strict-aliasing -fsanitize=undefined \
  -I"$AWB" -I"$AWB/../sensor/gc1054" \
  "$HERE/test_cafc0_controls.c" "$AWB/fh8626_stock_awb_mode0_ref.c" \
  "$AWB/../sensor/gc1054/fh8626_sensor_gc1054.c" -ldl -o "$CAF"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$CAF"

cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=undefined \
  -I"$AWB" "$HERE/test_awb_word_arithmetic.c" \
  "$AWB/fh8626_stock_awb_mode0_pipeline.c" "$AWB/fh8626_stock_awb_mode0_ref.c" \
  "$AWB/fh8626_stock_c9f68_ref.c" \
  "$AWB/../diagnostics/fh8626_stock_ca4f4_diag.c" -o "$WORD"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$WORD"

cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=undefined \
  -I"$AWB" "$HERE/test_awb_dispatch.c" \
  "$AWB/fh8626_stock_awb_mode0_pipeline.c" "$AWB/fh8626_stock_awb_mode0_ref.c" \
  "$AWB/fh8626_stock_c9f68_ref.c" \
  "$AWB/../diagnostics/fh8626_stock_ca4f4_diag.c" -o "$DISPATCH"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$DISPATCH"
