"""Check current release binaries against portable, explicitly scoped evidence."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tool(name, path):
    if os.name == 'nt':
        linux = '/mnt/' + path.drive[0].lower() + path.as_posix()[2:]
        command = ['wsl.exe', '-d', 'Ubuntu', '--', name, linux]
    else:
        command = [name, str(path)]
    return subprocess.run(command, check=True, capture_output=True,
                          text=True, encoding='utf-8').stdout


def main():
    evidence = json.loads((ROOT/'validation/release/evidence.json').read_text('utf-8'))
    for name, expected in evidence['verified_source_hashes'].items():
        if sha256(ROOT/name) != expected:
            raise ValueError(f'Verified source changed; renew its evidence: {name}')
    if evidence['host_c']['command_decision_agreement'] != evidence['host_c']['windows']:
        raise ValueError('Incomplete C decision agreement.')
    if evidence['negative_c']['command_outputs'] or not evidence['rv32']['actual_rv32_execution']:
        raise ValueError('Negative replay or RV32 execution failed.')
    firmware = []
    for mode in ('model', 'car'):
        elf = ROOT/f'build/voice/c1_voice_{mode}.elf'
        binary = elf.with_suffix('.bin')
        symbols = {name: int(address, 16) for address, _, name in re.findall(
            r'^([0-9a-fA-F]+)\s+(\w)\s+(\S+)', tool('riscv64-unknown-elf-nm', elf), re.M)}
        soft_float = [name for name in symbols if re.search(r'^__.*(?:sf|df)\d*$', name)]
        size = tool('riscv64-unknown-elf-size', elf).splitlines()[1].split()
        if soft_float or symbols['_start'] != 0x30000000 or symbols['__ram_start'] != 0x1000:
            raise ValueError('Unexpected entry/SRAM layout or floating-point dependency.')
        if symbols['__bss_end'] > 0x1e000:
            raise ValueError('Less than 8KiB stack reserve.')
        raw = binary.read_bytes()
        if evidence['model_sha256'].encode('ascii') not in raw or b'C1 VOICE CAR v9' not in raw:
            raise ValueError('Firmware identity differs from accepted release.')
        artifact = dict(file=binary.relative_to(ROOT).as_posix(), bytes=len(raw),
            sha256=sha256(binary), text_and_constants_bytes=int(size[0]), bss_bytes=int(size[2]),
            bss_end=f"0x{symbols['__bss_end']:08x}",
            stack_bytes_available=0x1fff0-symbols['__bss_end'], soft_float_dependencies=soft_float)
        if mode == 'car' and artifact['sha256'] != evidence['download']['downloaded_file_sha256'].lower():
            raise ValueError('Release car binary differs from the flashed, accepted binary.')
        firmware.append(artifact)
    summary = dict(date='2026-10-10', source_repository='https://github.com/FTCCCF/ECOS-C1-bluetoothcar',
        upstream_commit='c12c7cab8c33b65c28a8ab5370155fa6c56c9911', firmware_version='v9',
        checkpoint_sha256=evidence['model_sha256'], final_pool_bins=1, dsp_precision=16,
        thresholds_changed=False, firmware=firmware,
        car_matches_flashed_accepted_binary=True, verified_source_hashes_checked=True,
        host_replay=evidence['host_c'], negative_replay=evidence['negative_c'],
        rv32_execution=evidence['rv32'], board=evidence['board'],
        user_acceptance=evidence['user_acceptance'], limitations=evidence['limitations'],
        evidence_file='validation/release/evidence.json',
        evidence_sha256=sha256(ROOT/'validation/release/evidence.json'))
    (ROOT/'validation/summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n',
                                               encoding='utf-8', newline='\n')
    print(json.dumps(dict(firmware=firmware, matches_accepted_car=True)))


if __name__ == '__main__':
    main()
