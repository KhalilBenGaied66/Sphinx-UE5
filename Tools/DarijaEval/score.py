"""Run each candidate recogniser on the recordings and score it against the command list.

    uv run --extra tunisian python score.py                                   # default engines, real recordings
    uv run --extra tunisian python score.py --manifest recordings/placeholder/manifest.csv
    uv run --extra tunisian python score.py --engines whisper-small,gemini,sphinx-fr-kws
    uv run --extra tunisian python score.py --engines all --allow-download

For every clip: the engine's output, the command it maps to (fuzzy match across
all spellings for text engines, the spotted phrase for PocketSphinx), and the
time the engine took on that clip, model already loaded and warmed up on one
clip first. Writes results/<run>/report.md, results.csv and raw.jsonl.

Definitions used in the report:
  hit          command clip mapped to its own command
  false match  command clip mapped to a different command
  miss         command clip mapped to no command
  false accept distractor clip (chat, silence) mapped to any command
Gemini transcripts are cached in results/cache/ (the free tier has a daily quota);
the local engines are always re-run.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import platform
import statistics
import subprocess
import sys
import time
from collections import Counter, defaultdict
from datetime import datetime
from importlib import metadata
from pathlib import Path

import commands as cl
import engines as en
from record import read_wav

HERE = Path(__file__).parent
DEFAULT_ENGINES = "whisper-base,whisper-small,sphinx-fr-kws,sphinx-fr-grammar"


def sha1(path: Path) -> str:
    return hashlib.sha1(path.read_bytes()).hexdigest()


def load_clips(manifest: Path) -> list[dict]:
    with manifest.open(encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        r["path"] = manifest.parent / r["file"]
    return rows


def machine() -> str:
    cpu = platform.processor()
    try:
        out = subprocess.run(["powershell", "-NoProfile", "-Command",
                              "(Get-CimInstance Win32_Processor).Name"],
                             capture_output=True, text=True, timeout=20)
        cpu = out.stdout.strip() or cpu
    except Exception:
        pass
    gpu = ""
    try:
        out = subprocess.run(["nvidia-smi", "--query-gpu=name,memory.total", "--format=csv,noheader"],
                             capture_output=True, text=True, timeout=20)
        gpu = out.stdout.strip()
    except Exception:
        pass
    pkgs = []
    for p in ("faster-whisper", "ctranslate2", "pocketsphinx", "google-genai", "transformers", "torch"):
        try:
            pkgs.append(f"{p} {metadata.version(p)}")
        except metadata.PackageNotFoundError:
            pass
    return f"{cpu}; GPU {gpu or 'none'}; Python {platform.python_version()}; " + ", ".join(pkgs)


class Cache:
    def __init__(self, path: Path):
        self.path = path
        self.data: dict[str, dict] = {}
        if path.exists():
            for line in path.read_text(encoding="utf-8").splitlines():
                rec = json.loads(line)
                self.data[rec["key"]] = rec

    def get(self, key: str) -> dict | None:
        return self.data.get(key)

    def put(self, key: str, rec: dict) -> None:
        rec = dict(rec, key=key)
        self.data[key] = rec
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.path.open("a", encoding="utf-8") as f:
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")


def run_engine(engine: en.Engine, clips: list[dict], clist: cl.CommandList, args, cache_dir: Path):
    t0 = time.perf_counter()
    engine.load(args.allow_download)
    load_s = time.perf_counter() - t0
    description = engine.describe()
    cache = Cache(cache_dir / f"{engine.name}.jsonl") if engine.name == "gemini" else None

    audio = {str(c["path"]): read_wav(c["path"]) for c in clips}
    if cache is None and clips:  # warm-up, not timed
        engine.run(audio[str(clips[0]["path"])])

    out = []
    day_quota_gone = False
    for i, c in enumerate(clips, 1):
        key = f"{sha1(c['path'])}|{description}"
        rec = cache.get(key) if cache else None
        cached = rec is not None
        if rec is None and day_quota_gone:
            rec = dict(text="", direct=False, command=None, latency_ms=0.0,
                       error="not sent: the daily free-tier quota ran out earlier in this run")
        if rec is None:
            for attempt in range(5):
                if isinstance(engine, en.Gemini):
                    engine.wait_turn()
                t = time.perf_counter()
                try:
                    res = engine.run_clip(c, audio[str(c["path"])])
                    err = res.error
                except Exception as exc:
                    res, err = en.Result(""), f"{type(exc).__name__}: {exc}"[:1500]
                ms = (time.perf_counter() - t) * 1000  # the last attempt only
                rate_limited = "429" in err or "RESOURCE_EXHAUSTED" in err
                if rate_limited and "PerDay" in err:
                    # waiting does not help: the free tier resets once a day
                    day_quota_gone = True
                    print(f"  {engine.name}: daily quota reached; no more requests this run",
                          flush=True)
                    break
                if not rate_limited or attempt == 4:
                    break
                wait = 20 * (attempt + 1)
                print(f"  {engine.name}: rate limited, waiting {wait} s", flush=True)
                time.sleep(wait)
            if res.latency_ms is not None:
                ms = res.latency_ms
            rec = dict(text=res.text, direct=res.direct, command=res.command,
                       latency_ms=round(ms, 1), error=err)
            if cache and not err:
                cache.put(key, rec)
        if rec["direct"]:
            pred, score, runner, runner_score = rec["command"], None, None, None
        else:
            m = cl.match(rec["text"], clist, args.threshold, args.margin)
            pred, score, runner, runner_score = m.command, m.score, m.runner_up, m.runner_up_score
        out.append(dict(engine=engine.name, file=c["file"], speaker=c["speaker"],
                        source=c["source"], truth=c["command"], kind=c["kind"],
                        wording=int(c["wording"]), take=int(c["take"]), said=c.get("text", ""),
                        text=rec["text"], pred=pred, score=score, runner_up=runner,
                        runner_up_score=runner_score, latency_ms=rec["latency_ms"],
                        cached=cached, error=rec["error"]))
        mark = "ok " if pred == c["command"] else ("-- " if pred is None else "XX ")
        print(f"  {engine.name} [{i}/{len(clips)}] {mark}{c['command']:<16} -> "
              f"{str(pred):<16} {rec['latency_ms']:>8.0f} ms  {rec['text'][:60]}"
              + (f"  ERROR {rec['error'][:120]}" if rec["error"] else ""), flush=True)
    return dict(name=engine.name, description=description, load_s=round(load_s, 1)), out


def pct(a: int, b: int) -> str:
    return f"{a}/{b} ({100 * a / b:.0f}%)" if b else "-"


def summarise(rows: list[dict]) -> dict:
    # clips the engine failed on (quota, network) are counted as errors, not misses
    ok = [r for r in rows if not r["error"]]
    cmd = [r for r in ok if r["kind"] == "command"]
    dis = [r for r in ok if r["kind"] == "distractor"]
    hits = sum(r["pred"] == r["truth"] for r in cmd)
    wrong = sum(r["pred"] is not None and r["pred"] != r["truth"] for r in cmd)
    miss = sum(r["pred"] is None for r in cmd)
    fa = sum(r["pred"] is not None for r in dis)
    lat = sorted(r["latency_ms"] for r in rows if not r["error"])
    errors = sum(bool(r["error"]) for r in rows)
    p90 = lat[min(len(lat) - 1, int(0.9 * len(lat)))] if lat else None
    return dict(n_cmd=len(cmd), hits=hits, wrong=wrong, miss=miss, n_dis=len(dis), fa=fa,
                lat_med=statistics.median(lat) if lat else None, lat_p90=p90, errors=errors,
                cached=sum(r["cached"] for r in rows))


def report(path: Path, args, clips, clist, infos, rows) -> None:
    sources = Counter(c["source"] for c in clips)
    speakers = sorted({c["speaker"] for c in clips})
    by_engine = defaultdict(list)
    for r in rows:
        by_engine[r["engine"]].append(r)
    L = []
    L.append(f"# Darija command recognition: run {path.parent.name}\n")
    if any(s.startswith("placeholder") for s in sources):
        L.append("> **PLACEHOLDER AUDIO.** These clips are synthetic speech "
                 "(French voice reading Arabizi, or edge-tts Tunisian neural voices reading Arabic). "
                 "They show that the tools run end to end; "
                 "the numbers below say nothing about any recogniser on Darija.\n")
    L.append(f"- Command: `{' '.join(sys.argv)}`")
    L.append(f"- Audio: {', '.join(f'`{m}`' for m in args.manifest)}: {len(clips)} clips "
             f"({sum(c['kind'] == 'command' for c in clips)} command, "
             f"{sum(c['kind'] == 'distractor' for c in clips)} distractor), "
             f"sources {dict(sources)}, speakers {', '.join(speakers)}")
    L.append(f"- Command list: {len(clist.commands)} commands, {len(clist.distractors)} distractors (`commands.toml`)")
    if args.outside:
        L.append(f"- Skipped: {len(args.outside)} clips of commands not in the list "
                 f"({', '.join(sorted({c['command'] for c in args.outside}))})")
    L.append(f"- Fuzzy match: threshold {args.threshold:g}, margin {args.margin:g}, cross-script {cl.CROSS_SCRIPT}; command list: {args.commands_mode}")
    L.append(f"- Machine: {machine()}")
    L.append(f"- Date: {datetime.now().isoformat(timespec='minutes')}\n")

    L.append("## Summary\n")
    L.append("Clips an engine failed on (Errors column) are left out of its other columns.\n")
    L.append("| Engine | Hit | False match | Miss | False accept on distractors | Latency median / p90 (ms) | Load (s) | Errors |")
    L.append("|---|---|---|---|---|---|---|---|")
    for info in infos:
        s = summarise(by_engine[info["name"]])
        lat = (f"{s['lat_med']:.0f} / {s['lat_p90']:.0f}" if s["lat_med"] is not None else "-")
        if s["cached"]:
            lat += f" ({s['cached']} cached)"
        L.append(f"| {info['name']} | {pct(s['hits'], s['n_cmd'])} | {pct(s['wrong'], s['n_cmd'])} | "
                 f"{pct(s['miss'], s['n_cmd'])} | {pct(s['fa'], s['n_dis'])} | {lat} | "
                 f"{info['load_s']} | {s['errors']} |")
    L.append("")
    for info in infos:
        L.append(f"- **{info['name']}**: {info['description']}")
    L.append("")

    L.append("## Per command: hits / takes (false matches)\n")
    names = [i["name"] for i in infos]
    L.append("| Command | Loan wording | " + " | ".join(names) + " |")
    L.append("|---|---|" + "---|" * len(names))
    for cmd in clist.commands:
        cells = []
        for n in names:
            rs = [r for r in by_engine[n] if r["truth"] == cmd.id and not r["error"]]
            h = sum(r["pred"] == cmd.id for r in rs)
            w = sum(r["pred"] not in (None, cmd.id) for r in rs)
            cells.append(f"{h}/{len(rs)}" + (f" ({w})" if w else "") if rs else "-")
        loan = "yes" if cmd.has_loan else ""
        L.append(f"| {cmd.id} | {loan} | " + " | ".join(cells) + " |")
    L.append("")

    L.append("## Loanword wordings against native ones (hit rate)\n")
    L.append("| Engine | French loan wordings | Darija wordings |")
    L.append("|---|---|---|")
    for n in names:
        loan, native = [], []
        for r in by_engine[n]:
            if r["kind"] != "command" or r["error"]:
                continue
            ws = clist.by_id(r["truth"]).wordings
            if r.get("said"):  # the manifest says what was said: find that wording
                hit = next((w for w in ws if cl.norm_ar(w.ar) == cl.norm_ar(r["said"])), None)
                if hit is None:
                    continue
                w = hit
            else:
                w = ws[r["wording"]]
            (loan if w.loan else native).append(r["pred"] == r["truth"])
        L.append(f"| {n} | {pct(sum(loan), len(loan))} | {pct(sum(native), len(native))} |")
    L.append("")

    L.append("## Confusions\n")
    for n in names:
        conf = Counter((r["truth"], r["pred"]) for r in by_engine[n]
                       if r["pred"] is not None and r["pred"] != r["truth"])
        if conf:
            items = ", ".join(f"{t} -> {p} ({k})" for (t, p), k in conf.most_common())
            L.append(f"- {n}: {items}")
    L.append("")
    L.append("Every transcript is in `results.csv` and `raw.jsonl` next to this file.\n")
    path.write_text("\n".join(L), encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manifest", type=Path, nargs="+", default=[HERE / "recordings" / "manifest.csv"],
                    help="one or more manifest.csv files (default recordings/manifest.csv)")
    ap.add_argument("--engines", default=DEFAULT_ENGINES,
                    help=f"comma list; 'all' = every local engine ({','.join(en.ALL)}); add gemini by name")
    ap.add_argument("--commands", type=Path, default=cl.DEFAULT_COMMANDS)
    ap.add_argument("--allow-download", action="store_true",
                    help="let engines download missing models (sizes are in engines.SIZES)")
    ap.add_argument("--device", default="cpu", help="cpu or cuda for Whisper models (default cpu)")
    ap.add_argument("--gemini-rpm", type=float, default=8.0, help="Gemini requests per minute")
    ap.add_argument("--threshold", type=float, default=cl.THRESHOLD)
    ap.add_argument("--margin", type=float, default=cl.MARGIN)
    ap.add_argument("--kws-threshold", type=float, default=1e-30,
                    help="key phrase threshold (default 1e-30, the plugin's V5 tolerance, "
                         "VoiceCommandComponent's default)")
    ap.add_argument("--kws-score", type=float, default=-1.5,
                    help="phrase scoring: accept at this many nats per character or better (default -1.5)")
    ap.add_argument("--kws-margin", type=float, default=0.3,
                    help="phrase scoring: lead over the next command (default 0.3)")
    ap.add_argument("--commands-mode", choices=["full", "arabizi", "arabic"], default="full",
                    help="score with every spelling, or one Arabizi / one Arabic spelling per command")
    ap.add_argument("--sphinx-model", type=Path,
                    default=Path(os.environ.get("SPHINX_FR_MODEL", en.SPHINX_FR_DEFAULT)))
    ap.add_argument("--limit", type=int, default=0, help="score only the first N clips")
    ap.add_argument("--out", type=Path, default=HERE / "results")
    args = ap.parse_args()
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass

    clist = cl.load(args.commands)
    if args.commands_mode != "full":
        from compare_matchers import reduced
        clist = reduced(clist, args.commands_mode)
    clips = [c for m in args.manifest for c in load_clips(m)]
    if args.limit:
        clips = clips[: args.limit]
    known = {c.id for c in clist.commands}
    outside = [c for c in clips if c["kind"] == "command" and c["command"] not in known]
    if outside:
        print(f"skipping {len(outside)} clips of commands not in {args.commands.name}: "
              f"{sorted({c['command'] for c in outside})}")
        clips = [c for c in clips if c not in outside]
    args.outside = outside

    run_dir = args.out / datetime.now().strftime("%Y%m%d-%H%M%S")
    args.work = run_dir / "work"
    specs = []
    for spec in (s.strip() for s in args.engines.split(",") if s.strip()):
        specs += en.ALL if spec == "all" else [spec]
    infos, rows, skipped = [], [], []
    for spec in specs:
        engine = en.build(spec, clist, args)
        print(f"== {engine.name}", flush=True)
        try:
            info, out = run_engine(engine, clips, clist, args, args.out / "cache")
        except (en.NeedsDownload, RuntimeError, ImportError) as exc:
            print(f"   skipped: {exc}", flush=True)
            skipped.append((spec, str(exc)))
            continue
        infos.append(info)
        rows.extend(out)
        del engine

    run_dir.mkdir(parents=True, exist_ok=True)
    with (run_dir / "raw.jsonl").open("w", encoding="utf-8") as f:
        for r in rows:
            f.write(json.dumps(r, ensure_ascii=False) + "\n")
    if rows:
        with (run_dir / "results.csv").open("w", encoding="utf-8-sig", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0]))
            w.writeheader()
            w.writerows(rows)
    report(run_dir / "report.md", args, clips, clist, infos, rows)
    if skipped:
        with (run_dir / "report.md").open("a", encoding="utf-8") as f:
            f.write("## Not run\n\n" + "\n".join(f"- {s}: {why}" for s, why in skipped) + "\n")
    print(f"\nreport: {run_dir / 'report.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
