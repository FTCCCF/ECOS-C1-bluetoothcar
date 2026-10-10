#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
make selftest capturetest
python3 - <<'PY'
from pathlib import Path
import struct
for name in ('selftest','capturetest'):
    p=Path('build/voice/'+name+'.bin').read_bytes()
    p+=bytes((-len(p))%4)
    Path('tests/generated/'+name+'.hex').write_text(''.join(f'{v:08x}\n' for (v,) in struct.iter_unpack('<I',p)))
PY
core=${PICORV32_SOURCE:-tests/vendor/picorv32.v}
verilator --binary --timing --no-assert --top-module tb -Wno-fatal \
  -Wno-PINMISSING -Wno-WIDTHTRUNC -Wno-WIDTHEXPAND -Wno-INITIALDLY \
  -j 2 --Mdir tests/obj_dir tests/tb.sv "$core" > tests/results/verilator-build.log 2>&1
./tests/obj_dir/Vtb +IMAGE=tests/generated/capturetest.hex +CAPTURE=1 +WAIT=0 | tee tests/results/rv32-capture-wait0.log
./tests/obj_dir/Vtb +IMAGE=tests/generated/capturetest.hex +CAPTURE=1 +WAIT=2 | tee tests/results/rv32-capture-wait2.log
./tests/obj_dir/Vtb +IMAGE=tests/generated/selftest.hex +WAIT=0 | tee tests/results/rv32-model.log
