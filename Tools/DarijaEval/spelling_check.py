"""How much does the spelling of a custom phrase matter?

    uv run python spelling_check.py

One invented phrase, "remala konto biris", said six ways by the two Windows
voices (plain, stressed differently, "re-MAY-la", with a French accent, run
together), plus three lines of chat. Each clip is scored against six spellings
of the phrase with phrase_scoring.py, which gives the plugin's scores
(SpeechRecognition.Unit.PhraseScorer: 0 differences). Synthetic voices: a check
of the method, not of players. Results are in docs/custom-phrases.md; the clips
go to recordings/spelling/.
"""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path

import engines as en
import phrase_scoring as ps
import record
from spells_check import PS

OUT = Path(__file__).parent / "recordings" / "spelling"
ZIRA, HORTENSE = "Microsoft Zira Desktop", "Microsoft Hortense Desktop"
CLIPS = [
    ("zira_plain", ZIRA, "remala konto biris"),
    ("zira_mah", ZIRA, "reh mah lah, kon toh, bee riss"),
    ("zira_may", ZIRA, "remayla konto biris"),
    ("hortense_plain", HORTENSE, "rémala konto biris"),
    ("hortense_mei", HORTENSE, "rémeila konto biris"),
    ("zira_fast", ZIRA, "remalakontobiris"),
    ("chat_zira_1", ZIRA, "remember the castle"),
    ("chat_zira_2", ZIRA, "what time is it"),
    ("chat_hortense", HORTENSE, "bonjour à tous"),
]
SPELLINGS = ["remala konto biris", "remeyla konto biris", "remaila konto biris",
             "re ma la kon to bi ris", "rehmahlah kontoh beeriss", "REMALA KONTO BIRIS"]
# the sample's other spells, for how far ahead the phrase is
OTHER = {"Forward": ["velaris"], "Back": ["zelthar morun"], "Jump": ["amorix valor corva aeterna"]}


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        lst = Path(tmp) / "list.tsv"
        lst.write_text("\n".join(f"{voice}\t{text}\t{OUT / (name + '.wav')}" for name, voice, text in CLIPS),
                       encoding="utf-8")
        ps1 = Path(tmp) / "speak.ps1"
        ps1.write_text(PS, encoding="utf-8-sig")
        subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(ps1),
                        "-List", str(lst)], check=True)

    model = en.OmnilingualCTC()
    model.load(False)
    ids = {tok: i for i, tok in model.vocab.items() if not tok.startswith("<")}

    print("| Clip | Model wrote | " + " | ".join(SPELLINGS) + " | Best other spell |")
    print("|---" * (len(SPELLINGS) + 3) + "|")
    for name, _, _ in CLIPS:
        logits = model.logits(record.read_wav(OUT / (name + ".wav")))
        scores = [ps.score_phrases(logits, {"x": [s]}, ids, anywhere=False)[0][0] for s in SPELLINGS]
        other = ps.score_phrases(logits, OTHER, ids, anywhere=False)[0]
        print(f"| {name} | {model.greedy(logits)} | " + " | ".join(f"{s:.2f}" for s in scores)
              + f" | {other[1]} {other[0]:.2f} |")


if __name__ == "__main__":
    main()
