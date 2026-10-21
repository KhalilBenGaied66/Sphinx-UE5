"""Record the Darija commands, command by command, into 16 kHz mono WAV files.

    uv run --extra tunisian python record.py --list-devices
    uv run --extra tunisian python record.py --speaker khalil
    uv run --extra tunisian python record.py --speaker khalil --takes 3 --seconds 2.5 --device 1

Each take rotates through a command's wordings and shuffles the order of the
commands, so the list is not read in one breath. Distractors (chat that is not a
command, and one silent clip) are recorded after the commands. Files go to
recordings/<speaker>/ and one row per file is appended to recordings/manifest.csv.
Run it again with the same speaker to resume: takes already in the manifest are
skipped.

At each prompt: Enter = record, r = redo the last clip, s = skip, q = quit.
"""

from __future__ import annotations

import argparse
import csv
import random
import sys
import time
import wave
from datetime import datetime
from pathlib import Path

import numpy as np

import commands as cl

RATE = 16000
HERE = Path(__file__).parent
FIELDS = ["speaker", "command", "kind", "wording", "take", "file", "seconds",
          "peak_dbfs", "source", "device", "recorded_at", "text"]


def write_wav(path: Path, audio: np.ndarray, rate: int = RATE) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    pcm = np.clip(audio, -1.0, 1.0)
    pcm = (pcm * 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def read_wav(path: Path) -> np.ndarray:
    """16-bit PCM WAV -> mono float32 at 16 kHz."""
    with wave.open(str(path), "rb") as w:
        rate, channels, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
        raw = w.readframes(w.getnframes())
    if width != 2:
        raise ValueError(f"{path}: expected 16-bit PCM, got {8 * width}-bit")
    audio = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    if channels > 1:
        audio = audio.reshape(-1, channels).mean(axis=1)
    return resample(audio, rate, RATE)


def resample(audio: np.ndarray, src: int, dst: int) -> np.ndarray:
    """Windowed-sinc low-pass then linear interpolation; enough for speech."""
    if src == dst:
        return audio.astype(np.float32)
    if src > dst:
        cutoff = 0.45 * dst / src
        taps = np.arange(-64, 65)
        kernel = 2 * cutoff * np.sinc(2 * cutoff * taps) * np.hamming(len(taps))
        audio = np.convolve(audio, kernel / kernel.sum(), mode="same")
    n = int(round(len(audio) * dst / src))
    x = np.linspace(0, len(audio) - 1, n)
    return np.interp(x, np.arange(len(audio)), audio).astype(np.float32)


def peak_dbfs(audio: np.ndarray) -> float:
    peak = float(np.max(np.abs(audio))) if len(audio) else 0.0
    return round(20 * np.log10(peak), 1) if peak > 0 else -120.0


def capture(seconds: float, device: int | None) -> np.ndarray:
    import sounddevice as sd

    try:
        audio = sd.rec(int(seconds * RATE), samplerate=RATE, channels=1,
                       dtype="float32", device=device)
        sd.wait()
        return audio[:, 0]
    except sd.PortAudioError:
        # some Windows drivers refuse 16 kHz; record at the device rate instead
        rate = int(sd.query_devices(device, "input")["default_samplerate"])
        audio = sd.rec(int(seconds * rate), samplerate=rate, channels=1,
                       dtype="float32", device=device)
        sd.wait()
        return resample(audio[:, 0], rate, RATE)


def load_manifest(path: Path) -> list[dict]:
    if not path.exists():
        return []
    with path.open(encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def append_manifest(path: Path, row: dict) -> None:
    new = not path.exists()
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDS)
        if new:
            writer.writeheader()
        writer.writerow(row)


def plan(clist: cl.CommandList, takes: int, distractor_takes: int, seed: int) -> list[dict]:
    """Every clip to record, in prompt order."""
    rng = random.Random(seed)
    items = []
    for take in range(1, takes + 1):
        order = list(clist.commands)
        rng.shuffle(order)
        for cmd in order:
            widx = (take - 1) % len(cmd.wordings)
            w = cmd.wordings[widx]
            items.append(dict(command=cmd.id, kind="command", wording=widx, take=take,
                              en=cmd.en, ar=w.ar, latin=w.arabizi[0], loan=w.loan))
    for take in range(1, distractor_takes + 1):
        for d in clist.distractors:
            items.append(dict(command=d.id, kind="distractor", wording=0, take=take,
                              en="not a command", ar=d.ar, latin=d.arabizi, loan=""))
    return items


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--speaker", help="short id, letters and digits (e.g. khalil, s02)")
    ap.add_argument("--takes", type=int, default=4, help="takes per command (default 4: one per wording, at most four wordings)")
    ap.add_argument("--distractor-takes", type=int, default=2)
    ap.add_argument("--seconds", type=float, default=2.5, help="clip length (default 2.5)")
    ap.add_argument("--device", type=int, default=None, help="input device index")
    ap.add_argument("--list-devices", action="store_true")
    ap.add_argument("--commands", type=Path, default=cl.DEFAULT_COMMANDS)
    ap.add_argument("--out", type=Path, default=HERE / "recordings")
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()

    if args.list_devices:
        import sounddevice as sd
        print(sd.query_devices())
        print(f"\ndefault input: {sd.default.device[0]}")
        return 0
    if not args.speaker or not args.speaker.isalnum():
        ap.error("--speaker is required (letters and digits only)")

    try:  # Arabic prompts on a Windows console
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass

    clist = cl.load(args.commands)
    manifest = args.out / "manifest.csv"
    done = {(r["speaker"], r["command"], r["take"]) for r in load_manifest(manifest)}
    items = [i for i in plan(clist, args.takes, args.distractor_takes, args.seed)
             if (args.speaker, i["command"], str(i["take"])) not in done]
    import sounddevice as sd
    device_name = sd.query_devices(args.device if args.device is not None else sd.default.device[0])["name"]
    print(f"speaker {args.speaker}, {len(items)} clips to record, {args.seconds} s each, mic: {device_name}")
    print("Say the line in your normal voice, as you would to a game. Enter = record, "
          "r = redo last, s = skip, q = quit.\n")

    last: dict | None = None
    i = 0
    while i < len(items):
        it = items[i]
        tag = "DISTRACTOR (not a command)" if it["kind"] == "distractor" else it["en"]
        loan = f"  [loan: {it['loan']}]" if it["loan"] else ""
        print(f"[{i + 1}/{len(items)}] take {it['take']}  {tag}{loan}")
        print(f"      {it['ar']}      {it['latin']}")
        key = input("      > ").strip().lower()
        if key == "q":
            break
        if key == "s":
            i += 1
            continue
        if key == "r" and last is None:
            key = ""
        if key == "r":
            it = last
            i -= 1
        print("      recording...", end="", flush=True)
        time.sleep(0.15)  # let the key click die out
        audio = capture(args.seconds, args.device)
        level = peak_dbfs(audio)
        warn = ""
        if it["kind"] != "distractor" or it["command"] != "silence":
            if level < -35:
                warn = "  (very quiet: move closer or raise the mic level, then press r)"
            elif level > -0.5:
                warn = "  (clipped: lower the mic level, then press r)"
        print(f" done, peak {level} dBFS{warn}")
        rel = Path(args.speaker) / f"{it['command']}__w{it['wording']}__t{it['take']}.wav"
        write_wav(args.out / rel, audio)
        row = dict(speaker=args.speaker, command=it["command"], kind=it["kind"],
                   wording=it["wording"], take=it["take"], file=rel.as_posix(),
                   seconds=args.seconds, peak_dbfs=level, source="mic",
                   device=device_name, recorded_at=datetime.now().isoformat(timespec="seconds"),
                   text=it["ar"])
        if key == "r":
            # replace the earlier row for this clip
            rows = [r for r in load_manifest(manifest) if r["file"] != row["file"]]
            manifest.unlink(missing_ok=True)
            for r in rows:
                append_manifest(manifest, r)
        append_manifest(manifest, row)
        last = it
        i += 1
    print(f"\nmanifest: {manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
