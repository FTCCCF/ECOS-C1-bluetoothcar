"""Verify explicit candidate headers against their host reference on reviewed audio.

All replays are development/port evidence. Original datasets, generated fixtures,
exports and previous reports remain untouched. Nothing is sent to the car.

The fixed-point frontend is an approximation of the float frontend; a tiny
feature difference can cross an INT8 rounding boundary. Verify the network
arithmetic with exactly the C input integers, then require the complete audio
path's frozen decisions and reviewed functional labels to agree. Report the
cross-frontend probability difference separately instead of conflating it with
network arithmetic error. The command rule remains 0.75 probability/0.15 margin.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent/'training/kws'))
from aligned import aligned_features
from data import annotations, crop, CLASSES, RATE
from quantize_mfcc import collect, predict_quantized, decide
from scipy.signal import resample_poly
from fractions import Fraction


def linux(path):
    path=Path(path).resolve()
    return '/mnt/'+path.drive[0].lower()+path.as_posix()[2:]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--checkpoint',type=Path,required=True)
    p.add_argument('--headers',type=Path,required=True)
    p.add_argument('--annotations',type=Path,required=True)
    p.add_argument('--field',nargs=2,action='append',default=[])
    p.add_argument('--output',type=Path,help='Separate evidence for a different DSP precision.')
    args=p.parse_args()
    digest=hashlib.sha256(args.checkpoint.read_bytes()).hexdigest()
    provenance=json.loads((args.headers/'provenance.json').read_text('utf-8'))
    if provenance['checkpoint_sha256'] != digest:
        raise ValueError('Header/checkpoint identity mismatch.')
    state=dict(np.load(args.checkpoint,allow_pickle=False))
    centered=bool(state.get('cepstral_center',False))
    rows,runs=annotations(args.annotations)
    selected=[r for r in rows if r['split']=='val' or (r['split']=='train' and
              runs[r['run_json']][0].get('recording_source') in ('phone_audio','c1_calibration'))]
    features=list(collect(selected,runs,centered))
    records=[dict(source=r['run_json'],start_seconds=float(r['start_seconds']),actual=r['label'],
                  split=r['split'],independent_test=False) for r in selected]
    fixtures=[]
    for r in selected:
        meta,audio=runs[r['run_json']]
        rate=meta['nominal_sample_rate']
        if meta['canonical_gain']==1 and 15500 <= rate <= 18000 and meta['frames']<=100000:
            fixtures.append((meta['frames'],round(rate*65536),round(float(r['start_seconds'])*RATE),
                             Path(meta['raw_file']).read_bytes()))
        else:
            pcm=np.rint(crop(audio,r['start_seconds'])*32768).clip(-32768,32767).astype('<i2')
            fixtures.append((RATE,RATE*65536,0,pcm.tobytes()))
            records[len(fixtures)-1]['fixture_host_resampled']=True
    for name,label in args.field:
        if label not in CLASSES:
            raise ValueError('Unknown field label.')
        path=Path(name);meta=json.loads(path.read_text('utf-8-sig'));raw=path.with_suffix('.pcm').read_bytes()
        if len(raw)!=meta['samples']*2 or hashlib.sha256(raw).hexdigest()!=meta['pcm_sha256']:
            raise ValueError('Field PCM identity mismatch.')
        rate=meta['rate_millihz']/1000
        ratio=Fraction(RATE/rate).limit_denominator(4096)
        wave=resample_poly(np.frombuffer(raw,'<i2').astype(float)/32768,ratio.numerator,ratio.denominator).astype('float32')
        start=meta['model_start_16k']
        features.append(aligned_features(wave[start:start+RATE],'mfcc12',False,cepstral_center=centered)[0])
        fixtures.append((meta['samples'],round(rate*65536),start,raw))
        records.append(dict(source=str(path.resolve()),actual=label,split='reused_field_diagnostic',independent_test=False))
    features=np.asarray(features,dtype='float32')
    reference=predict_quantized(state,features);expected=decide(reference)
    output=args.output or ROOT/'tests/results/candidates'/digest[:16]
    output.mkdir(parents=True,exist_ok=False)
    fixture=output/'pcm.bin'
    with fixture.open('wb') as f:
        f.write(b'VOICE001'+struct.pack('<I',len(fixtures)))
        for count,rate,start,raw in fixtures:
            if len(raw)!=count*2:raise ValueError('Fixture length mismatch.')
            f.write(struct.pack('<III',count,rate,start));f.write(raw)
    host=output/'host';c_output=output/'c-output.bin'
    cmd=['wsl.exe','-d','Ubuntu','--','gcc','-O2','-std=c11','-Wall','-Wextra','-Werror','-I',linux(ROOT),
         '-DVOICE_MODEL_HEADER="'+linux(args.headers/'model.h')+'"',
         '-DVOICE_DSP_HEADER="'+linux(args.headers/'dsp.h')+'"']
    cmd += [linux(ROOT/name) for name in ('tests/host.c','voice/fixed.c','voice/frontend.c','voice/infer.c')]
    subprocess.run(cmd+['-o',linux(host)],check=True)
    subprocess.run(['wsl.exe','-d','Ubuntu','--',linux(host),linux(fixture),linux(c_output)],check=True)
    raw=c_output.read_bytes();stride=2928
    if len(raw)!=stride*len(fixtures):raise ValueError('C output length mismatch.')
    probabilities=[];decisions=[];c_features=[];c_inputs=[]
    for i,record in enumerate(records):
        b=raw[i*stride:(i+1)*stride]
        probability=np.frombuffer(b[2900:2920],'<u4')/(1<<24)
        class_id,decision=struct.unpack('<ii',b[-8:])
        final=decision if decision>=0 else class_id if class_id>=3 else 3
        probabilities.append(probability);decisions.append(final)
        c_inputs.append(np.frombuffer(b[:576],'<i1').reshape(12,48).T)
        c_features.append(np.frombuffer(b[576:2880],'<i4').reshape(48,12)/65536)
        record.update(reference=CLASSES[int(expected[i])],port_decision=CLASSES[final],
                      port_probabilities=dict(zip(CLASSES,probability.tolist())))
    probabilities=np.asarray(probabilities);decisions=np.asarray(decisions)
    disagreements=int(np.sum(expected!=decisions));error=float(np.abs(probabilities-reference).max())
    # Dequantize only to enter the reference interface; its input quantizer
    # reconstructs these exact integers. No parameters are fitted here.
    same_input_features=np.asarray(c_inputs)*float(state['input_scale'])*state['std']+state['mean']
    same_input_probability=predict_quantized(state,same_input_features)
    network_error=float(np.abs(probabilities-same_input_probability).max())
    network_disagreements=int(np.sum(decide(same_input_probability)!=decisions))
    actual=np.asarray([CLASSES.index(r['actual']) for r in records])
    correct=np.where(actual<3,decisions==actual,decisions>=3)
    report=dict(checkpoint_sha256=digest,cepstral_center=centered,windows=len(records),
                model_header_sha256=provenance['model_header_sha256'],dsp_header_sha256=provenance['dsp_header_sha256'],
                final_pool_bins=int(state.get('final_pool_bins',8)),dsp_precision=provenance.get('dsp_precision',16),
                command_decision_agreement=len(records)-disagreements,maximum_probability_error=error,
                same_integer_input_maximum_probability_error=network_error,
                same_integer_input_decision_agreement=len(records)-network_disagreements,
                network_probability_tolerance=1e-5,
                frontend_probability_difference_is_diagnostic=True,
                maximum_mfcc_error=float(np.abs(features-np.asarray(c_features)).max()),
                functional_correct=int(correct.sum()),false_commands=int(np.sum((actual>=3)&(decisions<3))),
                host_c_full_frontend_executed=True,rv32_runtime_verified=False,board_verified=False,
                not_a_new_independent_score=True,records=records)
    (output/'port-verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='records'},ensure_ascii=False),flush=True)
    if disagreements or network_disagreements or network_error>1e-5 or not np.all(correct):
        raise AssertionError('Candidate port does not match its frozen host reference.')


if __name__=='__main__':
    main()
