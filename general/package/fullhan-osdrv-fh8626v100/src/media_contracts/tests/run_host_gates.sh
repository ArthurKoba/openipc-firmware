#!/usr/bin/env bash
set -euo pipefail

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/.." && pwd)
WORKSPACE="$ROOT"
OBJDIR=$(mktemp -d "${TMPDIR:-/tmp}/fh8626-host-gates.XXXXXX")
REPORT="$WORKSPACE/process/test_evidence/BASELINE.json"

cleanup() {
    make -C "$HERE" clean >/dev/null 2>&1 || true
    rm -rf "$OBJDIR"
}
trap cleanup EXIT INT TERM

INC=("-I$ROOT/bgm" "-I$ROOT/h264" "-I$ROOT/nr3d" "-I$ROOT/integration" "-I$ROOT/jpeg" "-I$ROOT/mux" "-I$ROOT/audio" "-I$ROOT/transport" "-I$ROOT/timing" "-I$ROOT/observability")
SOURCES=(
    "$ROOT/bgm/fh8626_bgm_adapter.c"
    "$ROOT/bgm/fh8626_bgm_linux.c"
    "$ROOT/bgm/fh8626_bgm_stock_route.c"
    "$ROOT/h264/fh8626_h264_control.c"
    "$ROOT/nr3d/fh8626_nr3d_kernel.c"
    "$ROOT/nr3d/fh8626_nr3d_lifecycle.c"
    "$ROOT/integration/fh8626_ghosting_policy.c"
    "$ROOT/h264/fh8626_h264_app_rc.c"
    "$ROOT/h264/fh8626_h264_stream.c"
    "$ROOT/jpeg/fh8626_jpeg_stream.c"
    "$ROOT/jpeg/fh8626_jpeg_config.c"
    "$ROOT/jpeg/fh8626_jpeg_lifecycle.c"
    "$ROOT/jpeg/fh8626_jpeg_owned_image.c"
    "$ROOT/mux/fh8626_mp4_sample.c"
    "$ROOT/integration/fh8626_mp4_generation.c"
    "$ROOT/h264/fh8626_h264_txn.c"
    "$ROOT/audio/fh8626_audio_fanout.c"
    "$ROOT/integration/fh8626_flv_generation.c"
    "$ROOT/transport/fh8626_rtmp_session.c"
    "$ROOT/timing/fh8626_timestamp.c"
    "$ROOT/observability/fh8626_bitrate_observer.c"
    "$ROOT/transport/fh8626_http_client.c"
)
SUITES=(
    test_bgm_adapter
    test_bgm_stock_route
    test_bgm_linux_wire
    test_h264_control
    test_nr3d_kernel
    test_nr3d_lifecycle
    test_ghosting_policy
    test_lifecycle_pass_a
    test_lifecycle_pass_b
    test_h264_app_rc
    test_h264_stream
    test_jpeg_stream
    test_jpeg_config
    test_jpeg_lifecycle
    test_mp4_sample
    test_mp4_generation
    test_h264_txn
    test_audio_fanout
    test_flv_generation
    test_rtmp_session
    test_timestamp
    test_bitrate_observer
    test_http_client
)

printf '%s\n' '[1/4] strict standalone object build'
for src in "${SOURCES[@]}"; do
    obj="$OBJDIR/$(basename "${src%.c}").o"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 -pthread "${INC[@]}" -c "$src" -o "$obj"
done

printf '%s\n' '[2/4] normal host suites'
make -C "$HERE" clean
make -C "$HERE" check
make -C "$HERE" clean

printf '%s\n' '[3/4] ASan + UBSan host suites'
export ASAN_OPTIONS='detect_leaks=1:halt_on_error=1'
export UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'
make -C "$HERE" CFLAGS='-std=c11 -Wall -Wextra -Werror -O1 -g -pthread -fno-omit-frame-pointer -fsanitize=address,undefined' check
make -C "$HERE" clean

printf '%s\n' '[4/4] post-gate binary cleanup check'
for bin in "${SUITES[@]}"; do
    if [[ -e "$HERE/$bin" ]]; then
        printf 'unexpected leftover test binary: %s\n' "$HERE/$bin" >&2
        exit 1
    fi
done

mkdir -p "$(dirname "$REPORT")"
CC_BIN=${CC:-cc} ROOT="$ROOT" REPORT="$REPORT" ASAN_OPTIONS="$ASAN_OPTIONS" UBSAN_OPTIONS="$UBSAN_OPTIONS" python3 - <<'PY'
from pathlib import Path
from datetime import datetime, timezone
import hashlib, json, os, platform, subprocess
root=Path(os.environ['ROOT'])
report=Path(os.environ['REPORT'])

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):
            h.update(chunk)
    return h.hexdigest()

files=[]
for p in sorted(root.rglob('*')):
    if not p.is_file():
        continue
    rel=p.relative_to(root).as_posix()
    if rel.startswith('tests/test_') and p.suffix == '':
        continue
    files.append((rel,sha(p)))
h=hashlib.sha256()
for rel,d in files:
    h.update(rel.encode()); h.update(b'\0'); h.update(d.encode()); h.update(b'\n')
cc=os.environ.get('CC_BIN','cc')
try:
    cc_version=subprocess.check_output([cc,'--version'],text=True,stderr=subprocess.STDOUT).splitlines()[0]
except Exception as ex:
    cc_version=f'{cc}: version unavailable: {ex}'
suites=['test_bgm_adapter','test_bgm_stock_route','test_bgm_linux_wire','test_h264_control','test_nr3d_kernel','test_nr3d_lifecycle','test_ghosting_policy','test_lifecycle_pass_a','test_lifecycle_pass_b','test_h264_app_rc','test_h264_stream','test_jpeg_stream','test_jpeg_config','test_jpeg_lifecycle','test_mp4_sample','test_mp4_generation','test_h264_txn','test_audio_fanout','test_flv_generation','test_rtmp_session','test_timestamp','test_bitrate_observer','test_http_client']
data={
  'schema':1,
  'generated_utc':datetime.now(timezone.utc).isoformat(),
  'scope':'standalone host verification; no target hardware validation',
  'standalone_tree_sha256':h.hexdigest(),
  'standalone_indexed_files':len(files),
  'compiler':cc_version,
  'platform':platform.platform(),
  'strict_compile':{'status':'PASS','flags':'-std=c11 -Wall -Wextra -Werror -O2 -pthread','implementation_c_count':22},
  'normal_suites':{'status':'PASS','count':23,'suites':suites},
  'sanitizer_suites':{'status':'PASS','count':23,'sanitizers':['address','undefined'],'flags':'-std=c11 -Wall -Wextra -Werror -O1 -g -pthread -fno-omit-frame-pointer -fsanitize=address,undefined','ASAN_OPTIONS':os.environ['ASAN_OPTIONS'],'UBSAN_OPTIONS':os.environ['UBSAN_OPTIONS'],'suites':suites},
  'lifecycle_passes':['test_lifecycle_pass_a','test_lifecycle_pass_b'],
  'cleanup':{'status':'PASS','leftover_test_binaries':0},
}
report.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
PY

if [[ -f "$WORKSPACE/tools/handoff.py" ]]; then
    python3 "$WORKSPACE/tools/handoff.py" index >/dev/null
fi
printf '%s\n' 'FH8626 standalone host gates: PASS'
printf 'CANONICAL_TEST_EVIDENCE=%s\n' "$REPORT"
