"""Package an explicit source allowlist and the two verified production images."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def sha256(raw):
    return hashlib.sha256(raw).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT.parent/'output/c1-voicecar-v9-20261010.zip')
    output = parser.parse_args().output.resolve()
    if output.exists():
        raise FileExistsError(output)
    summary = json.loads((ROOT/'validation/summary.json').read_text('utf-8'))
    evidence = json.loads((ROOT/'validation/release/evidence.json').read_text('utf-8'))
    if sha256((ROOT/summary['evidence_file']).read_bytes()) != summary['evidence_sha256']:
        raise ValueError('Evidence changed after validation summary.')
    names = (ROOT/'scripts/package-files.txt').read_text('utf-8').splitlines()
    expected_images = {'build/voice/c1_voice_model.bin', 'build/voice/c1_voice_car.bin'}
    if {item['file'] for item in summary['firmware']} != expected_images:
        raise ValueError('Only the model/car production images are distributable.')
    for item in summary['firmware']:
        if sha256((ROOT/item['file']).read_bytes()) != item['sha256']:
            raise ValueError('Firmware differs from summary; rebuild/revalidate first.')
        names.append(item['file'])
        names.append(str(Path(item['file']).with_suffix('.elf')).replace('\\', '/'))
        names.append(str(Path(item['file']).with_suffix('.map')).replace('\\', '/'))
    for name, expected in evidence['verified_source_hashes'].items():
        if sha256((ROOT/name).read_bytes()) != expected:
            raise ValueError(f'Verified source changed: {name}')
    names = sorted(set(names))
    for name in names:
        path = (ROOT/name).resolve()
        if path == ROOT or ROOT not in path.parents or not path.is_file():
            raise ValueError(f'Invalid allowlist path: {name}')
        if path.suffix in ('.wav', '.pcm', '.m4a') or 'tests/results/' in name:
            raise ValueError('Private audio/reports cannot be packaged.')
        if path.suffix == '.bin' and name not in expected_images:
            raise ValueError('Unexpected simulation or historical firmware.')
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in names:
            archive.write(ROOT/name, 'ECOS-C1-voicecar/'+name)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:
            raise ValueError('ZIP CRC failure.')
        for item in summary['firmware']:
            if sha256(archive.read('ECOS-C1-voicecar/'+item['file'])) != item['sha256']:
                raise ValueError('Packaged firmware mismatch.')
    print(json.dumps(dict(package=str(output), files=len(names), bytes=output.stat().st_size,
        sha256=sha256(output.read_bytes()), crc_and_firmware_hashes_verified=True)))


if __name__ == '__main__':
    main()
