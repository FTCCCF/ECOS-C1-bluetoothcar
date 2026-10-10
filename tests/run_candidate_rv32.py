"""Execute a separately built candidate selftest on the actual PicoRV32 RTL.

Selftest images are simulation fixtures and must never be downloaded to C1.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def linux(path):
    path=Path(path).resolve()
    return '/mnt/'+path.drive[0].lower()+path.as_posix()[2:]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    if args.binary.name!='selftest.bin':raise ValueError('Only a simulation selftest may be executed here.')
    args.output.mkdir(parents=True,exist_ok=False)
    raw=args.binary.read_bytes();raw+=bytes((-len(raw))%4)
    image=args.output/'selftest.hex'
    image.write_text(''.join(f'{v:08x}\n' for (v,) in struct.iter_unpack('<I',raw)),encoding='utf-8')
    objects=args.output/'obj_dir'
    with (args.output/'verilator-build.log').open('w',encoding='utf-8') as log:
        subprocess.run(['wsl.exe','-d','Ubuntu','--','verilator','--binary','--timing','--no-assert',
                        '--top-module','tb','-Wno-fatal','-Wno-PINMISSING','-Wno-WIDTHTRUNC',
                        '-Wno-WIDTHEXPAND','-Wno-INITIALDLY','-j','2','--Mdir',linux(objects),
                        linux(ROOT/'tests/tb.sv'),linux(ROOT/'tests/vendor/picorv32.v')],
                       stdout=log,stderr=subprocess.STDOUT,check=True)
    with (args.output/'rv32-model.log').open('w',encoding='utf-8') as log:
        subprocess.run(['wsl.exe','-d','Ubuntu','--',linux(objects/'Vtb'),'+IMAGE='+linux(image),'+WAIT=0'],
                       stdout=log,stderr=subprocess.STDOUT,check=True)
    lines=(args.output/'rv32-model.log').read_text('utf-8').splitlines()
    if not any('RV32 MODEL PASS' in s for s in lines):raise ValueError('No actual RV32 completion evidence.')
    report=dict(selftest_binary_sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),
                testbench_sha256=hashlib.sha256((ROOT/'tests/tb.sv').read_bytes()).hexdigest(),
                cpu_rtl_sha256=hashlib.sha256((ROOT/'tests/vendor/picorv32.v').read_bytes()).hexdigest(),
                actual_rv32_execution=True,model_only_drive_disabled=True,board_verified=False,
                summary=[s for s in lines if s.startswith('UART RESULT') or 'RV32 MODEL PASS' in s])
    (args.output/'rv32-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report),flush=True)


if __name__=='__main__':
    main()
