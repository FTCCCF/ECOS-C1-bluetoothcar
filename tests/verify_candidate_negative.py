"""Replay full original negative sources through the candidate's compiled C frontend."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parent/'training/kws'))
from data import load_run
from quantize_mfcc import collect,predict_quantized


def linux(path):
    path=Path(path).resolve()
    return '/mnt/'+path.drive[0].lower()+path.as_posix()[2:]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--checkpoint',type=Path,required=True)
    p.add_argument('--negative-sources',type=Path,required=True)
    p.add_argument('--port-directory',type=Path)
    args=p.parse_args()
    digest=hashlib.sha256(args.checkpoint.read_bytes()).hexdigest()
    output=args.port_directory or ROOT/'tests/results/candidates'/digest[:16]
    port=json.loads((output/'port-verification.json').read_text('utf-8'))
    if port['checkpoint_sha256']!=digest:raise ValueError('C verifier/model identity mismatch.')
    sources=json.loads(args.negative_sources.read_text('utf-8'))
    rows=[];runs={};fixtures=[]
    for source in sources['results']:
        key=str(Path(source['run_json']).resolve());meta,audio=load_run(key);runs[key]=(meta,audio)
        if meta['canonical_gain']!=1:raise ValueError('Expected preserved shift8 PCM.')
        starts=list(range(0,len(audio)-16000+1,1600))
        if starts[-1]!=len(audio)-16000:starts.append(len(audio)-16000)
        raw=Path(meta['raw_file']).read_bytes()
        for start in starts:
            rows.append(dict(run_json=key,start_seconds=start/16000))
            fixtures.append((meta['frames'],round(meta['nominal_sample_rate']*65536),start,raw))
    fixture=output/'negative-pcm.bin';c_output=output/'negative-c-output.bin'
    if fixture.exists() or c_output.exists():raise FileExistsError('Preserve previous negative evidence.')
    with fixture.open('wb') as f:
        f.write(b'VOICE001'+struct.pack('<I',len(fixtures)))
        for count,rate,start,raw in fixtures:
            if len(raw)!=count*2:raise ValueError('Invalid raw source length.')
            f.write(struct.pack('<III',count,rate,start));f.write(raw)
    subprocess.run(['wsl.exe','-d','Ubuntu','--',linux(output/'host'),linux(fixture),linux(c_output)],check=True)
    raw=c_output.read_bytes();stride=2928
    if len(raw)!=len(fixtures)*stride:raise ValueError('Invalid C output length.')
    decisions=np.array([struct.unpack('<i',raw[(i+1)*stride-4:(i+1)*stride])[0] for i in range(len(rows))])
    probability=np.stack([np.frombuffer(raw[i*stride+2900:i*stride+2920],'<u4')/(1<<24) for i in range(len(rows))])
    state=dict(np.load(args.checkpoint,allow_pickle=False))
    reference=predict_quantized(state,collect(rows,runs,bool(state.get('cepstral_center',False))))
    report=dict(checkpoint_sha256=digest,recordings=len(sources['results']),correlated_windows=len(rows),
                unique_audio_seconds=sources['unique_audio_seconds'],command_outputs=int(np.sum(decisions>=0)),
                maximum_probability_error=float(np.abs(reference-probability).max()),
                host_c_full_frontend_executed=True,board_verified=False,independent_test=False)
    (output/'negative-c-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report),flush=True)
    if report['command_outputs']:raise AssertionError('Candidate emitted a command on full negative sources.')


if __name__=='__main__':main()
