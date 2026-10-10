"""Replay existing reviewed PCM through the complete board C implementation.

The frozen weights, feature definition and decision thresholds are unchanged.
This verifies a port, and does not constitute another independent model test.
"""
import argparse
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[1]


def linux(path):
    path=Path(path).resolve()
    return '/mnt/'+path.drive[0].lower()+path.as_posix()[2:]


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--workspace', type=Path, default=ROOT.parent)
    args=parser.parse_args()
    training=args.workspace/'training'
    sys.path.insert(0, str(training/'kws'))
    from data import annotations, crop, CLASSES
    from quantize_mfcc import collect, predict_quantized, decide
    current=json.loads((training/'models/kws/current.json').read_text('utf-8'))
    model=Path(current['checkpoint'])
    freeze=json.loads(Path(current['freeze_file']).read_text('utf-8'))
    digest=hashlib.sha256(model.read_bytes()).hexdigest()
    if digest!=freeze['int8_sha256']:
        raise ValueError('Model freeze mismatch.')
    rows,runs=annotations(args.workspace/'datasets/voice/annotations-development-20261008-04.csv')
    selected=[r for r in rows if r['split']=='val' or r['group']==freeze['fresh_test_group']]
    state=dict(np.load(model, allow_pickle=False))
    reference_features=collect(selected, runs)
    reference_probability=predict_quantized(state, reference_features)
    expected=decide(reference_probability)
    generated=ROOT/'tests/generated'; generated.mkdir(parents=True, exist_ok=True)
    results=ROOT/'tests/results'; results.mkdir(parents=True, exist_ok=True)
    fixture=generated/'reviewed-pcm.bin'
    with fixture.open('wb') as stream:
        stream.write(b'VOICE001'+struct.pack('<I',len(selected)))
        for row in selected:
            meta,_=runs[row['run_json']]
            if meta['canonical_gain']!=1:
                raise ValueError('This board replay expects shift8 recordings.')
            pcm=Path(meta['raw_file']).read_bytes()
            stream.write(struct.pack('<III', meta['frames'], round(meta['nominal_sample_rate']*65536), round(float(row['start_seconds'])*16000)))
            stream.write(pcm)
    common=['voice/fixed.c','voice/frontend.c','voice/infer.c','voice/motor.c']
    host=generated/'host'
    gcc=['wsl.exe','-d','Ubuntu','--','gcc','-O2','-std=c11','-Wall','-Wextra','-Werror','-I',linux(ROOT)]
    subprocess.run(gcc+[linux(ROOT/'tests/host.c')]+[linux(ROOT/p) for p in common]+['-o',linux(host)], check=True)
    control=generated/'control'
    subprocess.run(gcc+[linux(ROOT/'tests/control.c')]+[linux(ROOT/p) for p in common]+['-o',linux(control)], check=True)
    subprocess.run(['wsl.exe','-d','Ubuntu','--',linux(control)],check=True)
    output=results/'host-output.bin'
    subprocess.run(['wsl.exe','-d','Ubuntu','--',linux(host),linux(fixture),linux(output)],check=True)
    raw=output.read_bytes(); stride=576+576*4+5*4+5*4+8
    if len(raw)!=stride*len(selected):
        raise ValueError('Invalid C result length.')
    c_features=[]; c_inputs=[]; c_probability=[]; c_decisions=[]; c_classes=[]
    for n in range(len(selected)):
        b=raw[n*stride:(n+1)*stride]
        c_inputs.append(np.frombuffer(b[:576],'<i1').reshape(12,48).T)
        c_features.append(np.frombuffer(b[576:576+2304],'<i4').reshape(48,12)/65536)
        c_probability.append(np.frombuffer(b[2900:2920],'<u4')/(1<<24))
        class_id,decision=struct.unpack('<ii', b[-8:]); c_classes.append(class_id)
        c_decisions.append(decision if decision>=0 else class_id if class_id>=3 else CLASSES.index('unknown'))
    c_probability=np.asarray(c_probability); c_decisions=np.asarray(c_decisions)
    ref_input=np.rint((reference_features-state['mean'])/state['std']/float(state['input_scale'])).clip(-127,127).astype(np.int8)
    probability_error=float(np.abs(reference_probability-c_probability).max())
    disagreement=np.flatnonzero(expected!=c_decisions)
    records=[dict(run_id=runs[r['run_json']][0]['run_id'], start_seconds=float(r['start_seconds']),
                  split=r['split'], actual=r['label'], reference=CLASSES[int(e)],
                  port_decision=CLASSES[int(c)], port_probabilities=p.tolist())
             for r,e,c,p in zip(selected,expected,c_decisions,c_probability)]
    report=dict(checkpoint_sha256=digest, weights_changed=False, thresholds_changed=False,
                windows=len(selected), validation_windows=sum(r['split']=='val' for r in selected),
                fresh_test_replay_windows=sum(r['group']==freeze['fresh_test_group'] for r in selected),
                command_decision_agreement=len(selected)-len(disagreement),
                maximum_probability_error=probability_error,
                maximum_mfcc_error=float(np.abs(reference_features-np.asarray(c_features)).max()),
                differing_input_quantization_values=int(np.count_nonzero(ref_input!=np.asarray(c_inputs))),
                port_correct=int(sum(r['actual']==r['port_decision'] for r in records)),
                false_commands=int(sum(r['actual'] in ('unknown','noise') and r['port_decision'] in CLASSES[:3] for r in records)),
                host_c_full_frontend_executed=True, rv32_runtime_verified=False,
                board_inference_verified=False, records=records)
    (results/'port-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='records'}))
    for i in disagreement:
        print(json.dumps(records[int(i)]))
    # A real reviewed forward window, resampled once by the established host
    # frontend, is a separate RV32 execution fixture (no labels are invented).
    index=next(i for i,r in enumerate(selected) if r['group']==freeze['fresh_test_group'] and r['label']=='forward')
    row=selected[index]
    pcm=np.rint(crop(runs[row['run_json']][1], row['start_seconds'])*32768).clip(-32768,32767).astype(np.int16)
    lines=[', '.join(str(int(x)) for x in pcm[i:i+16]) for i in range(0,len(pcm),16)]
    text='#define SELFTEST_SAMPLES 16000u\n#define SELFTEST_RATE_Q16 (16000u*65536u)\n#define SELFTEST_START_16K 0u\n'
    text+='static const int16_t selftest_pcm[16000] __attribute__((section(".fixture"))) = {\n'+',\n'.join(lines)+'\n};\n'
    (generated/'selftest_pcm.h').write_text(text,encoding='utf-8')
    (generated/'selftest-source.json').write_text(json.dumps(dict(run_id=runs[row['run_json']][0]['run_id'],
        start_seconds=float(row['start_seconds']), actual=row['label'],
        fixture='host-resampled real PCM16, full C frontend and inference execute on RV32'),indent=2)+'\n',encoding='utf-8')
    if len(disagreement) or probability_error>.005:
        raise AssertionError('Port does not yet match the frozen reference; see port-verification.json.')
    # Also replay the already assessed five complete negative recordings.
    # Overlapping windows are correlated diagnostics, not new independent data.
    negative_source=json.loads((model.parent/'sliding-negative-test04.json').read_text('utf-8'))
    diagnostic=[]
    for source in negative_source['results']:
        key=str(Path(source['run_json']).resolve())
        meta,audio=runs[key]
        positions=list(range(0,len(audio)-16000+1,1600))
        if positions[-1]!=len(audio)-16000:
            positions.append(len(audio)-16000)
        for start in positions:
            diagnostic.append(dict(run_json=key,start_seconds=start/16000))
    negative_fixture=generated/'negative-pcm.bin'
    with negative_fixture.open('wb') as stream:
        stream.write(b'VOICE001'+struct.pack('<I',len(diagnostic)))
        for row in diagnostic:
            meta,_=runs[row['run_json']]
            stream.write(struct.pack('<III',meta['frames'],round(meta['nominal_sample_rate']*65536),round(row['start_seconds']*16000)))
            stream.write(Path(meta['raw_file']).read_bytes())
    negative_output=results/'negative-output.bin'
    subprocess.run(['wsl.exe','-d','Ubuntu','--',linux(host),linux(negative_fixture),linux(negative_output)],check=True)
    negative_raw=negative_output.read_bytes()
    if len(negative_raw)!=len(diagnostic)*stride:
        raise ValueError('Invalid negative replay output.')
    negative_p=np.stack([np.frombuffer(negative_raw[i*stride+2900:i*stride+2920],'<u4')/(1<<24) for i in range(len(diagnostic))])
    negative_decision=np.asarray([struct.unpack('<i',negative_raw[(i+1)*stride-4:(i+1)*stride])[0] for i in range(len(diagnostic))])
    negative_ref=predict_quantized(state,collect(diagnostic,runs))
    negative_report=dict(checkpoint_sha256=digest,recordings=len(negative_source['results']),
        correlated_overlapping_windows=len(diagnostic),unique_audio_seconds=negative_source['unique_audio_seconds'],
        not_independent_sample_count=True,command_outputs=int(np.sum(negative_decision>=0)),
        maximum_probability_error=float(np.abs(negative_p-negative_ref).max()),
        board_inference_verified=False)
    (results/'negative-port-verification.json').write_text(json.dumps(negative_report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(negative_report))
    if negative_report['command_outputs']:
        raise AssertionError('A negative recording triggered a command in the port.')


if __name__=='__main__':
    main()
