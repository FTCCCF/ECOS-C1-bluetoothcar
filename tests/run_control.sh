#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p tests/results/control
for left in 0 1; do
  for full in 0 1; do
    binary="tests/results/control/left${left}-full${full}"
    gcc -O2 -std=c11 -Wall -Wextra -Werror -I. \
      -DVOICE_LEFT_CHANNEL_A="$left" -DVOICE_MOTOR_FULL_DUTY="$full" \
      '-DVOICE_MODEL_HEADER="voice/models/mfcc-cmn-global-20261010-11/model.h"' \
      '-DVOICE_DSP_HEADER="voice/models/mfcc-cmn-global-20261010-11/dsp.h"' \
      tests/control.c voice/fixed.c voice/frontend.c voice/infer.c voice/motor.c -o "$binary"
    "$binary"
  done
done
