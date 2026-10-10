"""Read the real-board diagnostic firmware; verify PCM transport, save WAV.

This client does not run the speech model or infer that register readback
proves a physical wheel moved. The diagnostic firmware stops after each pulse.
"""
import argparse
from datetime import datetime
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import time
import wave

import serial

ROOT = Path(__file__).resolve().parents[1]


def transaction(port, command, timeout, lines):
    port.write(command.encode("ascii"))
    port.flush()
    deadline = time.monotonic() + timeout
    received = []
    while time.monotonic() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("ascii", errors="strict").rstrip("\r\n")
        lines.append(line)
        received.append(line)
        if not line.startswith("PCM "):
            print(line, flush=True)
        if line == "READY":
            return received
    raise TimeoutError(f"No READY after diagnostic command {command!r}.")


def microphone_result(lines, base):
    header = next(line for line in lines if line.startswith("MIC_BEGIN "))
    fields = {key: int(value) for key, value in re.findall(r"(\w+)=(-?\d+)", header)}
    samples = []
    for line in lines:
        if not line.startswith("PCM "):
            continue
        match = re.fullmatch(r"PCM index=(\d+) data=([0-9A-F]+)", line)
        if not match or int(match[1]) != len(samples) or len(match[2]) % 4:
            raise ValueError("PCM row is damaged or out of sequence.")
        for offset in range(0, len(match[2]), 4):
            value = int(match[2][offset:offset + 4], 16)
            samples.append(value if value < 32768 else value - 65536)
    if len(samples) != fields["samples"]:
        raise ValueError("PCM sample count differs from the board header.")
    packed = struct.pack("<" + "h" * len(samples), *samples)
    fnv = 2166136261
    for value in packed:
        fnv = ((fnv ^ value) * 16777619) & 0xffffffff
    end = next(line for line in lines if line.startswith("MIC_END fnv="))
    if fnv != int(end.split("=")[1], 16):
        raise ValueError("PCM FNV differs from the board checksum.")
    mean = sum(samples) / len(samples)
    variance = sum((value - mean) ** 2 for value in samples) / len(samples)
    fields.update(
        pcm_fnv=f"{fnv:08X}", transport_verified=True,
        pcm_sha256=hashlib.sha256(packed).hexdigest(),
        minimum=min(samples), maximum=max(samples), dc_mean=mean,
        ac_rms=math.sqrt(variance), distinct_values=len(set(samples)),
        clipped_samples=sum(value in (-32768, 32767) for value in samples),
        duration_seconds=len(samples) * 1000 / fields["rate_millihz"],
    )
    base.with_suffix(".pcm").write_bytes(packed)
    wav_path = base.with_suffix(".wav")
    with wave.open(str(wav_path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(round(fields["rate_millihz"] / 1000))
        output.writeframes(packed)
    fields["wav"] = str(wav_path)
    return fields


def motor_result(lines, command):
    states = [line for line in lines if line.startswith("STATE ")]
    if len(states) != 2 or not any(line.startswith("MOTOR_END stopped=1") for line in lines):
        raise ValueError("Motor pulse did not report its automatic stop.")
    active = dict(re.findall(r"(\w+)=(\w+)", states[0]))
    stopped = dict(re.findall(r"(\w+)=(\w+)", states[1]))
    expected = {
        "l": (1, 7307, 20000), "h": (64, 20000, 6667),
        "t": (65, 7307, 6667), "b": (130, 6667, 6667),
        "L": (1, 0, 20000), "H": (64, 20000, 0),
        "T": (65, 0, 0), "B": (130, 0, 0),
        "r": (128, 20000, 6667), "R": (128, 20000, 0),
    }[command]
    mask, left, right = expected
    active_matches = (
        int(active["GPIO_shadow"], 16) & 0xC3 == mask
        and int(active["GPIO_live"], 16) & 0xC3 == mask
        and int(active["PWM_CTRL"]) == 3 and int(active["PWM_CMP"]) == 20000
        and int(active["CR2_left"]) == left and int(active["CR1_right"]) == right
    )
    stopped_matches = (
        int(stopped["GPIO_live"], 16) & 0xC3 == 0
        and int(stopped["CR2_left"]) == 20000 and int(stopped["CR1_right"]) == 20000
    )
    end = next(line for line in lines if line.startswith("MOTOR_END "))
    counts = dict(re.findall(r"(\w+)=(\d+)", end))
    return dict(
        commanded_channels=["B"] if command in "lL" else ["A"] if command in "hHrR" else ["A", "B"],
        active_readback=active, stop_readback=stopped,
        register_readback_matches=active_matches and stopped_matches,
        # The C1-era RTL does not implement a CNT read case.
        counter_readback_supported=False,
        counter_readback=dict(before=int(counts["cnt_before"]),after=int(counts["cnt_after"])),
        physical_motion_verified=False,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM5")
    parser.add_argument("--command", choices=["i", "l", "h", "t", "b", "L", "H", "T", "B", "r", "R", "m", "s"], default="i")
    parser.add_argument("--label", default="diagnostic")
    parser.add_argument("--timeout", type=float, default=35)
    args = parser.parse_args()
    if not re.fullmatch(r"[a-zA-Z0-9_-]+", args.label):
        parser.error("--label must contain only letters, digits, underscores or hyphens.")
    output = ROOT / "tests/results/diagnostic"
    output.mkdir(parents=True, exist_ok=True)
    base = output / (datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + args.label)
    lines = []
    try:
        with serial.Serial(args.port, 115200, timeout=1, write_timeout=2, dsrdtr=False, rtscts=False) as port:
            time.sleep(0.2)
            port.reset_input_buffer()
            for attempt in range(3):
                try:
                    identity = transaction(port, "i", 2, lines)
                    break
                except TimeoutError:
                    if attempt == 2:
                        raise
                    print("No query reply; retrying identity only.", flush=True)
                    time.sleep(0.25)
            firmware_id=next((line for line in identity if line.startswith("C1 HARDWARE DIAGNOSTIC v")),None)
            if firmware_id not in ("C1 HARDWARE DIAGNOSTIC v1","C1 HARDWARE DIAGNOSTIC v2","C1 HARDWARE DIAGNOSTIC v3"):
                raise ValueError("Expected diagnostic firmware is not running; no test command sent.")
            if args.command in "LHTBrR" and firmware_id != "C1 HARDWARE DIAGNOSTIC v3":
                raise ValueError("Full-duty and right-reverse tests need diagnostic v3; no motor command sent.")
            if args.command == "i":
                result = dict(identity_verified=True)
            else:
                time.sleep(0.2)
                response = transaction(port, args.command, args.timeout, lines)
                if args.command == "m":
                    result = microphone_result(response, base)
                elif args.command in "lhtbLHTBrR":
                    result = motor_result(response, args.command)
                else:
                    result = dict(stop_acknowledged="STOPPED" in response)
        result.update(command=args.command, label=args.label, port=args.port,firmware_id=firmware_id)
        base.with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(result), flush=True)
        print(f"Saved: {base.with_suffix('.json')}", flush=True)
    finally:
        base.with_suffix(".log").write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
