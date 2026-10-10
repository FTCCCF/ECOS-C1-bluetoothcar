# C1 voice car

Based on FTCCCF/ECOS-C1-bluetoothcar c12c7cab. Current accepted release is v9.

- C1 PicoRV32 RV32IM, nominal 72MHz, no hardware float. Keep DSP, inference and acceptance arithmetic integer.
- `make model car` builds current v9 headers under `voice/models/mfcc-cmn-global-20261010-11`. `model` disables PWM/directions; `car` enables drive. Model SHA256 a04434631d6eadee122c5c6d0c0fed2eb7b32609ffa127e3e13fb8002f30063d.
- SRAM starts at 0x1000; linker reserves at least 8KiB stack. Software I2S loop must stay in SRAM, not flash or PSRAM.
- Current verified wiring A=left/B=right; GPIO6/7=AIN1/2, GPIO0/1=BIN1/2. PWMA=PWM1/U58.2; PWMB=PWM2/U58.5. Active-low PWM CR=0 drives full duty, CR=20000 stops. Defaults select left A and full duty.
- Preserve original selectable speed4000/max6000/forward trim952/1000. Remove duty before reversing. Reject retains the accepted action; stop clears both directions/duties. No production motion timeout.
- INMP441 SCK=GPIO9/H55.3, WS=GPIO8/H55.5, SD=GPIO5/H55.4. GPIO7 belongs to AIN2. Power H55.30=3.3V, H55.28/29=GND; pin2 is GND. Preserve motor/LCD bits with an output shadow when toggling I2S clocks.
- Decode complete left I2S slots with shift8; clock both channels. Keep six consecutive 128-frame voice blocks, 2048-sample circular prefix and the first resampled one-second model window.
- Current model uses coefficient-wise MFCC centering and global temporal pooling. Original eight-bin model stays archived/selectable in voice/generated. Keep probability>=0.75, margin>=0.15 and DSP precision16. Do not train on reused diagnostics or tune thresholds against them.
- 2026-10-10 user accepted independent forward-stop-backward-stop self-test. It occurred before the passive UART observer; never attribute those four steps to its later logs. UART identity/inference and user wheel observations are separate proof layers.
- Board inference is about5.6 seconds at nominal72MHz, with microphone-clock/listening gaps. Do not claim continuous acquisition or immediate stop.
- Preserve private recordings, raw results and historical builds locally. Do not publish absolute user paths or recordings. Public evidence is validation/release/evidence.json, current artifacts validation/summary.json.
- Simulation selftest/capturetest/triggeredtest binaries must never be flashed or packaged. Independent diagnostics/board_main.c starts stopped and provides bounded motor pulses/PCM export; diagnostic commands identify channels, not legacy wheel labels.
- Verification: bash tests/run_control.sh, make model car, public summary/hash checks. Audio replay tools require the separate private training/data workspace. Preserve tests/vendor/picorv32.v permission notice.
