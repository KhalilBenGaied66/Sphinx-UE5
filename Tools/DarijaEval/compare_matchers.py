"""Old matcher against new, on transcripts the engines already wrote.

    uv run --extra tunisian python compare_matchers.py results/<run>/raw.jsonl [...]
    uv run --extra tunisian python compare_matchers.py --live results/live-2026-10-20.tsv

Matching is the step after an engine has written its text, so it can be
compared without running any engine again. Each command list is tried with the
matcher as it was (same script only) and with cross-script matching:

  full      every spelling in commands.toml
  arabizi   one Arabizi spelling per command: a developer who typed it once in Latin letters
  arabic    one Arabic spelling per command: the same, in Arabic script

--live takes transcripts without labels (what the plugin heard in a live test)
and prints what each matcher makes of them, side by side.
"""

from __future__ import annotations

import argparse
import copy
import json
import sys
from collections import defaultdict
from pathlib import Path

import commands as cl


def reduced(clist: cl.CommandList, keep: str) -> cl.CommandList:
    out = copy.deepcopy(clist)
    for cmd in out.commands:
        first = cmd.wordings[0]
        if keep == "arabizi":
            # the Arabic field must say something: use the Arabizi text, which
            # the Arabic-script comparison then ignores
            cmd.wordings = [cl.Wording(ar=first.arabizi[0], arabizi=[first.arabizi[0]], loan=first.loan)]
        else:
            cmd.wordings = [cl.Wording(ar=first.ar, arabizi=[], loan=first.loan)]
    return out


def pct(a: int, b: int) -> str:
    return f"{a}/{b} ({100 * a / b:.0f}%)" if b else "-"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("raw", nargs="*", type=Path)
    ap.add_argument("--live", type=Path)
    args = ap.parse_args()
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    clist = cl.load()
    lists = {"full": clist, "arabizi": reduced(clist, "arabizi"), "arabic": reduced(clist, "arabic")}

    if args.live:
        print("| Heard | old matcher | cross-script | one Arabizi spelling, cross-script |")
        print("|---|---|---|---|")
        for line in args.live.read_text(encoding="utf-8").splitlines():
            text = line.strip()
            if not text:
                continue
            old = cl.match(text, clist, cross_script=False).command or "-"
            new = cl.match(text, clist, cross_script=True).command or "-"
            once = cl.match(text, lists["arabizi"], cross_script=True).command or "-"
            print(f"| {text} | {old} | {new} | {once} |")
        return 0

    rows = []
    for raw in args.raw:
        for line in raw.read_text(encoding="utf-8").splitlines():
            rec = json.loads(line)
            # PocketSphinx and phrase scoring pick a command themselves: nothing to match
            direct = rec.get("direct") or rec["engine"].startswith(("sphinx", "omni-kws"))
            if not direct and not rec.get("error"):
                rows.append(rec)
    by_engine = defaultdict(list)
    for r in rows:
        by_engine[r["engine"]].append(r)

    print("| Engine | Command list | Matcher | Hit | False match | False accept |")
    print("|---|---|---|---|---|---|")
    for engine, rs in by_engine.items():
        for name, commands in lists.items():
            for cross in (False, True):
                hit = wrong = fa = n_cmd = n_dis = 0
                for r in rs:
                    pred = cl.match(r["text"], commands, cross_script=cross).command
                    if r["kind"] == "command":
                        n_cmd += 1
                        hit += pred == r["truth"]
                        wrong += pred is not None and pred != r["truth"]
                    else:
                        n_dis += 1
                        fa += pred is not None
                print(f"| {engine} | {name} | {'cross-script' if cross else 'old'} | {pct(hit, n_cmd)} | "
                      f"{pct(wrong, n_cmd)} | {pct(fa, n_dis)} |")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
