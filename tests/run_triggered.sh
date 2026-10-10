#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
make capturetest triggeredtest
python3 - <<'PY'
from pathlib import Path
import struct
for name in ('capturetest','triggeredtest'):
    raw=Path('build/voice/'+name+'.bin').read_bytes()
    raw+=bytes((-len(raw))%4)
    Path('tests/generated/'+name+'.hex').write_text(''.join(f'{word:08x}\n' for (word,) in struct.iter_unpack('<I',raw)))
PY
verilator --binary --timing --no-assert --top-module tb -Wno-fatal \
  -Wno-PINMISSING -Wno-WIDTHTRUNC -Wno-WIDTHEXPAND -Wno-INITIALDLY \
  -j 2 --Mdir tests/obj_dir tests/tb.sv tests/vendor/picorv32.v > tests/results/triggered-verilator-build.log 2>&1
for delay in 0 2; do
  ./tests/obj_dir/Vtb +IMAGE=tests/generated/capturetest.hex +CAPTURE=1 +WAIT="$delay" | tee "tests/results/capture-after-vad-wait$delay.log"
  ./tests/obj_dir/Vtb +IMAGE=tests/generated/triggeredtest.hex +CAPTURE=2 +WAIT="$delay" | tee "tests/results/triggered-capture-wait$delay.log"
done
./tests/obj_dir/Vtb +IMAGE=tests/generated/triggeredtest.hex +CAPTURE=2 +WAIT=2 +STOP_FRAME=2048 | tee tests/results/triggered-stop.log
./tests/obj_dir/Vtb +IMAGE=tests/generated/triggeredtest.hex +CAPTURE=2 +WAIT=2 +STOP_FRAME=5120 | tee tests/results/triggered-stop-during-capture.log
