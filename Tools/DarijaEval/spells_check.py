"""Does phrase scoring recognise invented words, in no language at all?

    uv run --extra tunisian python spells_check.py

Five made-up spell phrases, plus chat that is not a spell, spoken by the two
Windows voices (English Zira, French Hortense; synthetic, so a check of the
method, not of players). Each clip is scored against the five phrases, written
once each in Latin letters as a developer would, with the plugin's default
acceptance (-2.5 nats per character, 0.5 ahead of the next phrase). The clips
go to recordings/spells/.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

import engines as en
import phrase_scoring as ps
import record

OUT = Path(__file__).parent / "recordings" / "spells"
SPELLS = {
    "shield": "amorix valor",
    "fire": "corva aeterna",
    "heal": "sanarel vitae",
    "teleport": "zelthar morun",
    "summon": "kraveth ulnar",
}
CHAT = ["where is the door", "I need more potions", "bonjour comment ca va", "let's go to the castle"]
VOICES = ["Microsoft Zira Desktop", "Microsoft Hortense Desktop"]

PS = r"""
param([string]$List)
Add-Type -AssemblyName System.Speech
$s = New-Object System.Speech.Synthesis.SpeechSynthesizer
$fmt = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000,
  [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
foreach ($line in [System.IO.File]::ReadAllLines($List, [System.Text.Encoding]::UTF8)) {
  $voice, $text, $path = $line -split "`t"
  $s.SelectVoice($voice)
  $s.SetOutputToWaveFile($path, $fmt)
  $s.Speak($text)
}
$s.SetOutputToNull()
"""


def main() -> int:
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    OUT.mkdir(parents=True, exist_ok=True)
    jobs = []
    for v, voice in enumerate(VOICES):
        for cid, text in SPELLS.items():
            jobs.append((voice, text, OUT / f"{cid}_{v}.wav", cid))
        for i, text in enumerate(CHAT):
            jobs.append((voice, text, OUT / f"chat{i}_{v}.wav", None))
    with tempfile.TemporaryDirectory() as tmp:
        lst = Path(tmp) / "list.tsv"
        lst.write_text("\n".join(f"{a}\t{b}\t{c}" for a, b, c, _ in jobs), encoding="utf-8")
        ps1 = Path(tmp) / "speak.ps1"
        ps1.write_text(PS, encoding="utf-8-sig")
        subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(ps1),
                        "-List", str(lst)], check=True)

    model = en.OmnilingualCTC()
    model.load(False)
    ids = {tok: i for i, tok in model.vocab.items() if not tok.startswith("<")}
    phrases = {cid: [text] for cid, text in SPELLS.items()}

    hits = wrong = fired = 0
    print("| Voice | Said | Model wrote | Best phrase | Score | Next | Decision |")
    print("|---|---|---|---|---|---|---|")
    for voice, text, path, truth in jobs:
        logits = model.logits(record.read_wav(path))
        ranked = ps.score_phrases(logits, phrases, ids, anywhere=False)
        (best, cid, _), second = ranked[0], ranked[1][0]
        pred = cid if best >= -2.5 and best - second >= 0.5 else None
        if truth:
            hits += pred == truth
            wrong += pred is not None and pred != truth
        else:
            fired += pred is not None
        print(f"| {voice.split()[1]} | {text} | {model.greedy(logits)} | {SPELLS[cid]} | "
              f"{best:.2f} | {second:.2f} | {pred or '-'} |")
    n_spells = sum(1 for j in jobs if j[3])
    print(f"\nspells recognised {hits}/{n_spells}, wrong {wrong}, chat that fired a spell {fired}/{len(jobs) - n_spells}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
