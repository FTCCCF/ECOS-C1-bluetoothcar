"""Exercise real C1 inference and export that same input for diagnosis.

No direction command or host model result is sent to the board. A capture is
followed by a stopped PCM export. User observation remains separate evidence.
"""
import argparse
from datetime import datetime
import json
from pathlib import Path
import re
import time

import serial
from board_diag import transaction, microphone_result

ROOT = Path(__file__).resolve().parents[1]


class TimedPort:
    """Record actual UART arrival times without storing each PCM row twice."""
    def __init__(self, port, events):
        self.port = port
        self.events = events
        self.started = time.monotonic()

    def __getattr__(self, name):
        return getattr(self.port, name)

    def write(self, data):
        self.events.append(dict(elapsed_seconds=time.monotonic() - self.started,
                                timestamp=datetime.now().astimezone().isoformat(),
                                direction="send", text=data.decode("ascii")))
        return self.port.write(data)

    def readline(self):
        raw = self.port.readline()
        if raw and not raw.startswith(b"PCM "):
            self.events.append(dict(elapsed_seconds=time.monotonic() - self.started,
                                    timestamp=datetime.now().astimezone().isoformat(),
                                    direction="receive", text=raw.decode("ascii").rstrip("\r\n")))
        return raw


def pause(port, lines):
    until = time.monotonic() + 24
    last_send = 0
    acknowledged = False
    while time.monotonic() < until:
        if time.monotonic() - last_send > 3:
            port.write(b"s"); port.flush(); last_send = time.monotonic()
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("ascii").rstrip("\r\n")
        lines.append(line)
        if not line.startswith("PCM "):
            print(line, flush=True)
        if line == "PAUSED motors_stopped":
            acknowledged = True
        if acknowledged and line == "READY":
            return
    raise TimeoutError("No stopped/pause acknowledgement.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--command", choices=["prepare", "capture", "capture-no-lcd", "dump", "resume", "sequence"], default="prepare")
    parser.add_argument("--label", default="voice-board")
    parser.add_argument("--port", default="COM5")
    parser.add_argument("--firmware-version", default="v9")
    parser.add_argument("--expected-model-sha256", default="a04434631d6eadee122c5c6d0c0fed2eb7b32609ffa127e3e13fb8002f30063d")
    parser.add_argument("--cepstral-center", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--final-pool-bins",type=int,choices=(1,8),default=1)
    parser.add_argument("--dsp-precision",type=int,choices=(16,),default=16)
    parser.add_argument("--sequence-start", choices=["forward", "backward"], default="forward")
    parser.add_argument("--motor-hold-seconds", type=float, default=3,
                        help="Host test hold before stopped export; use 0 for calibration capture.")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.label):
        parser.error("Use only letters, digits, underscores or hyphens in --label.")
    if not 0 <= args.motor_hold_seconds <= 10:
        parser.error("--motor-hold-seconds must be between 0 and 10.")
    if not re.fullmatch(r"[0-9a-f]{64}", args.expected_model_sha256) or not re.fullmatch(r"v[0-9]+", args.firmware_version):
        parser.error("Invalid expected firmware or model identity.")
    base = ROOT / "tests/results/diagnostic" / (datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + args.label)
    lines = []
    result = dict(command=args.command, port=args.port, label=args.label,
                  inference_executed_on="C1", manual_direction_command_sent=False,
                  physical_motion_verified=False)
    port = serial.Serial(port=None, baudrate=115200, timeout=.3, write_timeout=2,
                         dsrdtr=False, rtscts=False)
    port.port = args.port; port.dtr = False; port.rts = False
    result["started_at"] = datetime.now().astimezone().isoformat()
    result["uart_events"] = []
    port = TimedPort(port, result["uart_events"])
    try:
        port.open(); time.sleep(.2); port.reset_input_buffer()
        if args.command == "prepare":
            pause(port, lines); result["pause_acknowledged"] = True
        for attempt in range(3):
            try:
                identity = transaction(port, "i", 2, lines)
                break
            except TimeoutError:
                if attempt == 2:
                    raise
                time.sleep(.25)
        expected = ["C1 VOICE CAR " + args.firmware_version, "motor_duty=full", "motor_wiring=left_A,right_B",
                    "capture=voice_onset pre_samples=2048 voice_blocks=6 model_start_16k=0",
                    "MODEL_SHA256=" + args.expected_model_sha256]
        if not all(item in identity for item in expected):
            raise ValueError("Voice firmware identity/model/profile not confirmed.")
        result.update(firmware_id=expected[0], model_sha256=expected[-1].split("=")[1], model_start_16k=0)
        if args.cepstral_center and "cepstral_center=1" not in identity:
            raise ValueError("Centered frontend profile not confirmed.")
        result["cepstral_center"] = args.cepstral_center
        for name in ('final_pool_bins','dsp_precision'):
            value=getattr(args,name)
            if value is not None:
                if f'{name}={value}' not in identity:raise ValueError(f'{name} not confirmed.')
                result[name]=value
        if args.command == "resume":
            port.write(b"r"); port.flush(); result["continuous_requested"] = True
            until=time.monotonic()+3
            while time.monotonic()<until:
                raw=port.readline()
                if not raw:continue
                line=raw.decode('ascii').rstrip('\r\n');lines.append(line);print(line,flush=True)
                if line=='LISTEN warmup_then_wait_for_voice':
                    result['continuous_acknowledged']=True
                    break
            if not result.get('continuous_acknowledged'):
                raise TimeoutError('Continuous request sent but no listening acknowledgement received.')
        if args.command in ("capture", "capture-no-lcd"):
            if args.command == "capture-no-lcd":
                reply = transaction(port, "q", 3, lines)
                if "LCD_DISABLED GPIO2_LOW" not in reply:
                    raise ValueError("LCD disable not acknowledged.")
            started = time.monotonic()
            reply = transaction(port, "a", 60, lines)
            audio_line = next(s for s in reply if s.startswith("AUDIO "))
            result.update({k: int(v) for k, v in re.findall(r"(model_start_16k|trigger_abs|noise_abs|wait_frames)=(\d+)", audio_line)})
            line = next(s for s in reply if s.startswith("RESULT "))
            result["voice_result"] = dict(re.findall(r"(\w+)=(\w+)", line))
            result["capture_and_inference_seconds"] = time.monotonic() - started
            result["test_motor_hold_seconds"] = args.motor_hold_seconds
            time.sleep(args.motor_hold_seconds)
        if args.command == "sequence":
            # No s/d/manual direction is sent between the two neural results.
            # The operator switches to saying stop after the first result.
            result["sequence"] = []
            for expected_decision in (args.sequence_start, "stop"):
                port.write(b"a"); port.flush()
                print("WAITING_FOR_NEURAL_COMMAND=" + expected_decision, flush=True)
                deadline = time.monotonic() + (60 if expected_decision != "stop" else 35)
                reply = []
                ready = False
                while time.monotonic() < deadline:
                    raw = port.readline()
                    if not raw:
                        continue
                    line = raw.decode("ascii").rstrip("\r\n")
                    reply.append(line); lines.append(line); print(line, flush=True)
                    if line == "READY":
                        ready = True; break
                if not ready:
                    raise TimeoutError("Neural sequence timed out; stopping through finally.")
                line = next(s for s in reply if s.startswith("RESULT "))
                voice = dict(re.findall(r"(\w+)=(\w+)", line))
                result["sequence"].append(voice)
                if voice.get("decision") != expected_decision:
                    raise ValueError("Expected neural " + expected_decision + ", got " + voice.get("decision", "missing"))
                if expected_decision != "stop" and (voice.get("CR1_A") != "0" or voice.get("CR2_B") != "0"):
                    raise ValueError("Neural drive did not enable both full-duty PWM outputs.")
                if expected_decision == "stop" and (voice.get("CR1_A") != "20000" or voice.get("CR2_B") != "20000"):
                    raise ValueError("Neural stop did not clear both PWM outputs.")
            result["neural_stop_transition_verified"] = True
        if args.command in ("capture", "capture-no-lcd", "dump", "sequence"):
            reply = transaction(port, "d", 24, lines)
            result.update(microphone_result(reply, base))
            result["stopped_export_completed"] = True
    except Exception as error:
        result["error"] = str(error)
        if port.is_open:
            try:
                pause(port, lines); result["pause_acknowledged"] = True
                if args.command == "sequence" and result.get("sequence"):
                    reply = transaction(port, "d", 24, lines)
                    result.update(microphone_result(reply, base))
                    result["failure_audio_exported"] = True
                    result["stopped_export_completed"] = True
            except Exception as stop_error:
                result["stop_error"] = str(stop_error)
        raise
    finally:
        if port.is_open and args.command != "resume" and not (result.get("stopped_export_completed") or result.get("pause_acknowledged")):
            try:
                pause(port, lines); result["pause_acknowledged"] = True
            except Exception as stop_error:
                result["stop_error"] = str(stop_error)
        if port.is_open:
            port.close()
        base.with_suffix(".log").write_text("\n".join(lines) + "\n", encoding="utf-8")
        base.with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(result), flush=True)
        print("Saved: " + str(base.with_suffix(".json")), flush=True)


if __name__ == "__main__":
    main()
