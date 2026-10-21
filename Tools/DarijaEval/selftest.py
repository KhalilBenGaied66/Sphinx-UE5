"""Check commands.toml and the matcher before scoring anything.

    uv run --extra tunisian python selftest.py

Every spelling of every wording must map back to its own command, every
distractor and a few known Whisper hallucinations must map to none, and the
PocketSphinx tokens must have one pronunciation each. Run it after editing the
command list: a failure here means two commands are too close for the fuzzy
matcher, or a spelling is missing.
"""

from __future__ import annotations

import sys

import commands as cl

# Text Whisper is known to produce on silence or noise in Arabic.
HALLUCINATIONS = ["اشتركوا في القناة", "شكرا على المشاهدة", "ترجمة نانسي قنقر",
                  "موسيقى", "Thank you for watching"]


def main() -> int:
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    clist = cl.load()
    failures = 0

    def check(text: str, want: str | None, what: str) -> None:
        nonlocal failures
        m = cl.match(text, clist)
        if m.command != want:
            failures += 1
            print(f"FAIL {what}: {text!r} -> {m.command} ({m.score:.0f}, runner-up "
                  f"{m.runner_up} {m.runner_up_score:.0f}), expected {want}")

    n = 0
    for cmd in clist.commands:
        for w in cmd.wordings:
            for text in [w.ar, *w.arabizi]:
                check(text, cmd.id, cmd.id)
                n += 1
    for d in clist.distractors:
        if d.id != "silence":
            check(d.ar, None, d.id)
            check(d.arabizi, None, d.id)
            n += 2
    for text in HALLUCINATIONS:
        check(text, None, "hallucination")
        n += 1

    tokens: dict[str, str] = {}
    for cmd in clist.commands:
        for w in cmd.wordings:
            if not w.sphinx:
                failures += 1
                print(f"FAIL {cmd.id}: wording {w.ar} has no sphinx entry")
            for line in w.sphinx:
                token, phones = line.split(maxsplit=1)
                if tokens.setdefault(token, phones) != phones:
                    failures += 1
                    print(f"FAIL sphinx token {token!r}: two pronunciations")

    print(f"{n} spellings checked, {len(tokens)} sphinx tokens, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
