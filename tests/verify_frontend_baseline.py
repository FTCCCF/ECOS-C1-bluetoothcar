"""Confirm optional precision/pooling changes retain the archived baseline output."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def linux(path):
    path=Path(path).resolve()
    return '/mnt/'+path.drive[0].lower()+path.as_posix()[2:]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    output=args.output or ROOT/'tests/results/baseline-infer-regression'
    output.mkdir(parents=True,exist_ok=False)
    fixture=ROOT/'tests/generated/reviewed-pcm.bin'
    original=ROOT/'tests/results/host-output.bin'
    host=output/'host';actual=output/'c-output.bin'
    subprocess.run(['wsl.exe','-d','Ubuntu','--','gcc','-O2','-std=c11','-Wall','-Wextra','-Werror',
                    '-I',linux(ROOT)]+[linux(ROOT/name) for name in
                    ('tests/host.c','voice/fixed.c','voice/frontend.c','voice/infer.c')]+['-o',linux(host)],check=True)
    subprocess.run(['wsl.exe','-d','Ubuntu','--',linux(host),linux(fixture),linux(actual)],check=True)
    expected=original.read_bytes();observed=actual.read_bytes()
    report=dict(original_output_sha256=hashlib.sha256(expected).hexdigest(),
                new_output_sha256=hashlib.sha256(observed).hexdigest(),bytes=len(expected),
                windows=len(expected)//2928,exact_match=expected==observed,
                baseline_precision=16,baseline_final_pool_bins=8,board_verified=False)
    (output/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report),flush=True)
    if not report['exact_match']:raise AssertionError('Original baseline output changed.')


if __name__=='__main__':main()
