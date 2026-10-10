"""Freeze the existing temporal CNN and its DSP constants for RV32IM.

No training, threshold selection, or use of test clips takes place here.
Requires only NumPy/SciPy in the already established training environment.
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from scipy.signal import butter, sosfilt_zi

ROOT = Path(__file__).resolve().parents[1]


def array(name, values, dtype):
    values = np.asarray(values).ravel()
    lines = [', '.join(str(int(v)) for v in values[i:i+12])
             for i in range(0, len(values), 12)]
    return f'static const {dtype} {name}[{len(values)}] = {{\n    ' + ',\n    '.join(lines) + '\n};\n'


def fixed(value, bits):
    value = np.rint(np.asarray(value) * (1 << bits)).astype(np.int64)
    if np.any(value < -(1 << 31)) or np.any(value >= 1 << 31):
        raise OverflowError('Constant exceeds signed int32.')
    return value


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--training', type=Path, default=ROOT.parent/'training')
    parser.add_argument('--checkpoint', type=Path, help='Explicit frozen candidate; never replaces current.json.')
    parser.add_argument('--freeze-file', type=Path)
    parser.add_argument('--output', type=Path, help='Separate generated-header directory for a candidate.')
    args = parser.parse_args()
    current = json.loads((args.training/'models/kws/current.json').read_text('utf-8'))
    if bool(args.checkpoint) != bool(args.freeze_file):
        parser.error('--checkpoint and --freeze-file must be provided together.')
    if args.checkpoint and (args.output is None or args.output.resolve() == (ROOT/'voice/generated').resolve()):
        parser.error('A candidate requires a separate --output directory.')
    checkpoint = args.checkpoint or Path(current['checkpoint'])
    freeze_path = args.freeze_file or Path(current['freeze_file'])
    freeze = json.loads(freeze_path.read_text('utf-8'))
    digest = hashlib.sha256(checkpoint.read_bytes()).hexdigest()
    if digest != freeze['int8_sha256']:
        raise ValueError('Weights differ from the frozen model.')
    state = dict(np.load(checkpoint, allow_pickle=False))
    if str(state['architecture']) != 'temporal_conv' or state['classes'].tolist() != freeze['decision_rule']['classes']:
        raise ValueError('Unexpected architecture or class order.')
    rule = freeze['decision_rule']
    if rule['command_min_probability'] != .75 or rule['command_min_margin'] != .15:
        raise ValueError('Unexpected frozen decision rule.')
    output = args.output or ROOT/'voice/generated'
    centered = bool(state.get('cepstral_center', False))
    bins=int(state.get('final_pool_bins',8))
    precision=int(freeze.get('dsp_precision',16))
    if precision!=16:raise ValueError('Only the verified original DSP precision is supported.')
    if bins not in (1,8) or state['w3'].shape!=(5,32*bins):
        raise ValueError('Unexpected temporal classifier head.')
    if int(freeze.get('final_pool_bins',8))!=bins:
        raise ValueError('Classifier head differs from the freeze.')
    if bool(freeze.get('cepstral_center', False)) != centered:
        raise ValueError('Feature profile differs from the freeze.')
    output.mkdir(parents=True, exist_ok=True)
    header = '#ifndef VOICE_MODEL_H\n#define VOICE_MODEL_H\n#include <stdint.h>\n'
    header += f'#define VOICE_MODEL_SHA256 "{digest}"\n'
    if centered:
        header += '#define VOICE_CEPSTRAL_CENTER 1\n'
    if bins!=8:
        header += f'#define VOICE_FINAL_POOL_BINS {bins}\n'
    for name in ('w1', 'w2', 'w3', 'b1', 'b2', 'b3'):
        header += array(name, state[name], 'int8_t' if name.startswith('w') else 'int32_t')
    header += array('feature_mean_q16', fixed(state['mean'], 16), 'int32_t')
    gain = 1 / (state['std'].ravel() * float(state['input_scale']))
    header += array('feature_gain_q24', fixed(gain, 24), 'int32_t')
    header += array('requant1_q32', fixed(state['first_scale']/float(state['activation_scale1']), 32), 'int32_t')
    header += array('requant2_q32', fixed(state['second_scale']/float(state['activation_scale2']), 32), 'int32_t')
    header += array('logit_scale_q40', fixed(state['third_scale'], 40), 'int32_t')
    header += '#endif\n'
    (output/'model.h').write_text(header, encoding='utf-8')

    header = '#ifndef VOICE_DSP_TABLES_H\n#define VOICE_DSP_TABLES_H\n#include <stdint.h>\n'
    # Continuous Kaiser(5) polyphase kernel, same 10-sample radius as
    # scipy.signal.resample_poly. The measured source rate selects its cutoff.
    x = np.arange(2561)/256
    kernel = np.sinc(x)*np.i0(5*np.sqrt(np.maximum(0, 1-(x/10)**2)))/np.i0(5)
    header += array('resample_kernel_q30', fixed(kernel, 30), 'int32_t')
    header += array('hann_q15', np.rint(np.hanning(400)*32768), 'uint16_t')
    angle = -2*np.pi*np.arange(256)/512
    header += array('twiddle_real_q30', fixed(np.cos(angle), 30), 'int32_t')
    header += array('twiddle_imag_q30', fixed(np.sin(angle), 30), 'int32_t')
    header += array('ln_mantissa_q20', fixed(np.log1p(np.arange(257)/256), 20), 'int32_t')
    dct = np.sqrt(2/40)*np.cos(np.pi*np.arange(1, 13)[:, None]*(np.arange(40)+.5)/40)
    header += array('dct_q24', fixed(dct, 24), 'int32_t')
    hz = 700*(10**(np.linspace(2595*np.log10(1+20/700), 2595*np.log10(1+7600/700), 42)/2595)-1)
    freq = np.arange(257)*16000/512
    weights, first, count, offsets = [], [], [], []
    for left, center, right in zip(hz[:-2], hz[1:-1], hz[2:]):
        triangle = np.maximum(0, np.minimum((freq-left)/(center-left), (right-freq)/(right-center)))
        indices = np.flatnonzero(triangle > 0)
        first.append(int(indices[0])); count.append(len(indices)); offsets.append(len(weights))
        weights.extend(np.rint(triangle[indices]*32768).astype(int))
    for name, value in (('mel_first', first), ('mel_count', count), ('mel_offset', offsets), ('mel_weight_q15', weights)):
        header += array(name, value, 'uint16_t')
    sos = butter(2, 80, fs=16000, btype='highpass', output='sos')
    header += array('hp_q29', fixed(sos[0, [0, 1, 2, 4, 5]], 29), 'int32_t')
    header += array('hp_zi_q29', fixed(sosfilt_zi(sos)[0], 29), 'int32_t')
    header += array('exp_negative_q24', np.rint(np.exp(-np.arange(1025)/64)*(1 << 24)), 'uint32_t')
    header += '#endif\n'
    (output/'dsp.h').write_text(header, encoding='utf-8')
    provenance = dict(source_repository='FTCCCF/ECOS-C1-bluetoothcar',
                      upstream_commit='c12c7cab8c33b65c28a8ab5370155fa6c56c9911',
                      checkpoint_sha256=digest, architecture='MFCC12 temporal CNN',
                      classes=state['classes'].tolist(), decision_rule=rule,
                      weights_changed=digest!='3961dfa25718a90dfdac45f60cc896e2165162302887856590af9787392ec154',
                      thresholds_changed=False, cepstral_center=centered,
                      final_pool_bins=bins,
                      dsp_precision=precision,
                      checkpoint=str(checkpoint.resolve()), freeze_file=str(freeze_path.resolve()),
                      arithmetic='fixed point frontend, INT8 MAC / INT32 accumulators',
                      model_header_sha256=hashlib.sha256((output/'model.h').read_bytes()).hexdigest(),
                      dsp_header_sha256=hashlib.sha256((output/'dsp.h').read_bytes()).hexdigest(),
                      board_inference_verified=False)
    (output/'provenance.json').write_text(json.dumps(provenance, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(provenance))


if __name__ == '__main__':
    main()
