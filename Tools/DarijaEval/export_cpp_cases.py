"""Write the cases the plugin's C++ matcher is tested against.

    uv run --extra tunisian python export_cpp_cases.py

The plugin matches transcripts with a C++ port of commands.py. To keep the two
the same, this script writes every spelling of commands.toml, the distractors,
known Whisper hallucinations and every transcript the engines produced in
results/, each with what the Python matcher makes of it (normalised Arabic,
normalised Latin, command). The automated test SpeechRecognition.Unit.
TranscriptMatcherParity reads them and fails on the first difference.

Output: Plugins/SpeechRecognition/Resources/TestAudio/darija/
  matcher_groups.tsv   command <tab> phrase, in commands.toml order
  matcher_cases.tsv    text <tab> Arabic form <tab> Latin form <tab> command or "-" <tab> skeleton
  phrase_variants.tsv  phrase <tab> the spellings phrase scoring tries, separated by " | "
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

import commands as cl
import phrase_scoring as ps
from selftest import HALLUCINATIONS

HERE = Path(__file__).parent
OUT = HERE.parents[1] / "Plugins" / "SpeechRecognition" / "Resources" / "TestAudio" / "darija"


def clean(text: str) -> str:
    return re.sub(r"[\t\r\n]+", " ", text).strip()


def main() -> int:
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    clist = cl.load()
    OUT.mkdir(parents=True, exist_ok=True)

    groups = []
    for cmd in clist.commands:
        for w in cmd.wordings:
            for phrase in [w.ar, *w.arabizi]:
                groups.append(f"{cmd.id}\t{phrase}")
    (OUT / "matcher_groups.tsv").write_text("\n".join(groups) + "\n", encoding="utf-8", newline="\n")

    texts: list[str] = []
    for cmd in clist.commands:
        for w in cmd.wordings:
            texts += [w.ar, *w.arabizi]
    for d in clist.distractors:
        texts += [d.ar, d.arabizi]
    texts += HALLUCINATIONS
    for raw in sorted((HERE / "results").glob("*/raw.jsonl")):
        for line in raw.read_text(encoding="utf-8").splitlines():
            rec = json.loads(line)
            if not rec.get("direct") and rec.get("text"):
                texts.append(rec["text"])

    seen, rows = set(), []
    for text in texts:
        text = clean(text)
        if not text or text in seen:
            continue
        seen.add(text)
        m = cl.match(text, clist)
        latin = cl.norm_latin(re.sub("[؀-ۿ]", " ", text))
        rows.append(f"{text}\t{cl.norm_ar(text)}\t{latin}\t{m.command or '-'}\t{cl.skeleton(text)}")
    (OUT / "matcher_cases.tsv").write_text("\n".join(rows) + "\n", encoding="utf-8", newline="\n")
    variants = []
    for line in groups:
        phrase = line.split("\t", 1)[1]
        variants.append(f"{phrase}\t{' | '.join(ps.variants(phrase))}")
    (OUT / "phrase_variants.tsv").write_text("\n".join(variants) + "\n", encoding="utf-8", newline="\n")
    print(f"{len(groups)} phrases, {len(rows)} cases -> {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
