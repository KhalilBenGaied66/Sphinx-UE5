"""Generate PLACEHOLDER audio so the pipeline can run before anyone records.

    uv run --extra tunisian python placeholder.py

This machine has no Arabic text-to-speech voice, so the French Windows voice
(Hortense) reads each command's first Arabizi spelling, digits replaced by the
nearest French letters. That is a French voice mispronouncing Darija: the
numbers scored on it say whether the tools run, nothing about any recogniser.
Files go to recordings/placeholder/ with their own manifest, source
"placeholder-tts", so they never mix with real recordings.
"""

from __future__ import annotations

import csv
import re
import subprocess
import tempfile
from datetime import datetime
from pathlib import Path

import numpy as np

import commands as cl
from record import FIELDS, RATE, peak_dbfs, read_wav, write_wav

HERE = Path(__file__).parent
OUT = HERE / "recordings" / "placeholder"
VOICE = "Microsoft Hortense Desktop"

PS = r"""
param([string]$List, [string]$Voice)
Add-Type -AssemblyName System.Speech
$s = New-Object System.Speech.Synthesis.SpeechSynthesizer
$s.SelectVoice($Voice)
$fmt = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000,
  [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,
  [System.Speech.AudioFormat.AudioChannel]::Mono)
foreach ($line in [System.IO.File]::ReadAllLines($List, [System.Text.Encoding]::UTF8)) {
  $text, $path = $line -split "`t"
  $s.SetOutputToWaveFile($path, $fmt)
  $s.Speak($text)
}
$s.SetOutputToNull()
"""


def french_reading(latin: str) -> str:
    for a, b in (("3", "a"), ("7", "h"), ("9", "k"), ("5", "kh"), ("2", "")):
        latin = latin.replace(a, b)
    return re.sub(r"\s+", " ", latin).strip()


def main() -> int:
    clist = cl.load()
    jobs: list[tuple[str, str, int, Path]] = []  # (command, kind, wording, path)
    lines = []
    said: dict[Path, str] = {}  # the Arabic wording each clip stands for
    for cmd in clist.commands:
        for widx, w in enumerate(cmd.wordings):
            path = OUT / "tts" / f"{cmd.id}__w{widx}__t1.wav"
            jobs.append((cmd.id, "command", widx, path))
            said[path] = w.ar
            lines.append(f"{french_reading(w.arabizi[0])}\t{path}")
    for d in clist.distractors:
        path = OUT / "tts" / f"{d.id}__w0__t1.wav"
        jobs.append((d.id, "distractor", 0, path))
        said[path] = d.ar
        if d.id != "silence":
            lines.append(f"{french_reading(d.arabizi)}\t{path}")
    path_of_silence = OUT / "tts" / "silence__w0__t1.wav"

    OUT.joinpath("tts").mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        lst = Path(tmp) / "list.tsv"
        lst.write_text("\n".join(lines), encoding="utf-8")
        ps1 = Path(tmp) / "speak.ps1"
        ps1.write_text(PS, encoding="utf-8-sig")
        subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                        str(ps1), "-List", str(lst), "-Voice", VOICE], check=True)
    # room-noise stand-in for the silent clip
    rng = np.random.default_rng(0)
    write_wav(path_of_silence, rng.normal(0, 0.002, int(2.5 * RATE)).astype(np.float32))

    manifest = OUT / "manifest.csv"
    with manifest.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDS)
        writer.writeheader()
        for command, kind, widx, path in jobs:
            audio = read_wav(path)
            writer.writerow(dict(
                speaker="placeholder-tts", command=command, kind=kind, wording=widx, take=1,
                file=path.relative_to(OUT).as_posix(), seconds=round(len(audio) / RATE, 2),
                peak_dbfs=peak_dbfs(audio), source="placeholder-tts", device=VOICE,
                recorded_at=datetime.now().isoformat(timespec="seconds"), text=said[path]))
    print(f"{len(jobs)} placeholder clips, manifest {manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
