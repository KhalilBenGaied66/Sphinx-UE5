"""The candidate recognisers, behind one interface.

Each engine takes 16 kHz mono float32 audio and returns what it heard. Text
engines (Whisper, the Tunisian fine-tune) return a transcript that
score.py matches to the command list; PocketSphinx returns a command directly.

No engine downloads a model unless score.py is run with --allow-download: a
model missing from the Hugging Face cache stops the run with its size instead.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path

import numpy as np

import commands as cl

HERE = Path(__file__).parent
SPHINX_FR_DEFAULT = HERE.parents[1] / "Content" / "model" / "fr"  # the repository's own model folder

# Download sizes read from the Hugging Face API on 2026-10-19 (sum of repo files).
SIZES = {
    "Systran/faster-whisper-base": 147_886_409,
    "Systran/faster-whisper-small": 486_215_847,
    "mobiuslabsgmbh/faster-whisper-large-v3-turbo": 1_620_000_000,  # already cached here
    "TuniSpeech-AI/whisper-tunisian-dialect": 3_090_700_000,
}


class NeedsDownload(RuntimeError):
    pass


def _mb(n: int) -> str:
    return f"{n / 1e6:,.0f} MB" if n < 1e9 else f"{n / 1e9:.2f} GB"


def _ensure_cached(repo: str, allow_download: bool, weights: str = "model.bin") -> None:
    """Look for the weights file itself: faster-whisper fetches only the model
    files, which huggingface_hub reports as an incomplete snapshot."""
    from huggingface_hub import try_to_load_from_cache

    if not isinstance(try_to_load_from_cache(repo, weights), str):
        if not allow_download:
            raise NeedsDownload(
                f"{repo} is not in the Hugging Face cache; downloading it takes "
                f"{_mb(SIZES.get(repo, 0))}. Re-run with --allow-download to fetch it."
            ) from None


@dataclass
class Result:
    text: str
    command: str | None = None  # set only by engines that output a command directly
    direct: bool = False
    error: str = ""
    latency_ms: float | None = None  # set by engines whose time was measured elsewhere


class Engine:
    name = "engine"
    kind = "text"  # or "direct"

    def describe(self) -> str:
        return self.name

    def load(self, allow_download: bool) -> None:
        raise NotImplementedError

    def run(self, audio: np.ndarray) -> Result:
        raise NotImplementedError

    def run_clip(self, clip: dict, audio: np.ndarray) -> Result:
        return self.run(audio)


def _cuda_dlls() -> None:
    """CTranslate2 on the GPU needs CUDA 12 cuBLAS and cuDNN 9 DLLs. The torch
    cu128 wheel of the `tunisian` extra ships both: reuse them instead of
    installing the CUDA toolkit."""
    try:
        import torch
    except ImportError:
        raise RuntimeError("--device cuda needs the CUDA DLLs: run with `uv run --extra tunisian`") from None
    lib = Path(torch.__file__).parent / "lib"
    os.add_dll_directory(str(lib))
    os.environ["PATH"] = str(lib) + os.pathsep + os.environ.get("PATH", "")


def _command_prompt(clist: cl.CommandList) -> str:
    words = "، ".join(c.wordings[0].ar for c in clist.commands)
    return f"أوامر لعبة بالدارجة التونسية: {words}."


# --- (a) Whisper through faster-whisper -------------------------------------

class FasterWhisper(Engine):
    REPOS = {
        "base": "Systran/faster-whisper-base",
        "small": "Systran/faster-whisper-small",
        "turbo": "mobiuslabsgmbh/faster-whisper-large-v3-turbo",
    }

    def __init__(self, size: str, clist: cl.CommandList, prompt: bool, device: str):
        self.size, self.prompt, self.device = size, prompt, device
        self.repo = self.REPOS[size]
        self.initial_prompt = _command_prompt(clist) if prompt else None
        self.name = f"whisper-{size}" + ("+prompt" if prompt else "") + ("@gpu" if device == "cuda" else "")

    def describe(self) -> str:
        how = "command list given as initial_prompt" if self.prompt else "no prompt"
        ct = "int8" if self.device == "cpu" else "float16"
        return (f"faster-whisper, {self.repo}, language ar, beam 5, {how}, "
                f"{self.device} {ct}")

    def load(self, allow_download: bool) -> None:
        if self.device == "cuda":
            _cuda_dlls()
        from faster_whisper import WhisperModel

        _ensure_cached(self.repo, allow_download)
        self.model = WhisperModel(self.repo, device=self.device,
                                  compute_type="int8" if self.device == "cpu" else "float16")

    def run(self, audio: np.ndarray) -> Result:
        segments, _ = self.model.transcribe(
            audio, language="ar", task="transcribe", beam_size=5, temperature=0.0,
            without_timestamps=True, condition_on_previous_text=False,
            vad_filter=False, initial_prompt=self.initial_prompt)
        return Result(" ".join(s.text.strip() for s in segments).strip())


# --- (b) Tunisian fine-tune through transformers ----------------------------

class TunisianWhisper(Engine):
    repo = "TuniSpeech-AI/whisper-tunisian-dialect"
    name = "whisper-tunisian"

    def __init__(self, device: str):
        self.device_pref = device

    def describe(self) -> str:
        return (f"transformers, {self.repo} (Whisper large-v2 + LoRA merged, "
                f"CC-BY-NC-4.0), language ar, beam 5, {self.device} {self.dtype_name}")

    def load(self, allow_download: bool) -> None:
        import torch
        from transformers import WhisperForConditionalGeneration, WhisperProcessor

        _ensure_cached(self.repo, allow_download, "model.safetensors")
        self.torch = torch
        self.device = "cuda" if torch.cuda.is_available() and self.device_pref != "cpu" else "cpu"
        dtype = torch.float16 if self.device == "cuda" else torch.float32
        self.dtype_name = str(dtype).replace("torch.", "")
        self.processor = WhisperProcessor.from_pretrained(self.repo)
        self.model = WhisperForConditionalGeneration.from_pretrained(
            self.repo, dtype=dtype).to(self.device).eval()
        self.dtype = dtype

    def run(self, audio: np.ndarray) -> Result:
        feats = self.processor(audio, sampling_rate=16000, return_tensors="pt").input_features
        feats = feats.to(self.device, dtype=self.dtype)
        with self.torch.inference_mode():
            ids = self.model.generate(feats, language="ar", task="transcribe",
                                      num_beams=5, max_new_tokens=64)
        text = self.processor.batch_decode(ids, skip_special_tokens=True)[0]
        if self.device == "cuda":
            self.torch.cuda.synchronize()
        return Result(text.strip())


# --- Omnilingual ASR (Meta), CTC 300M, ONNX ----------------------------------

class OmnilingualCTC(Engine):
    """Meta's Omnilingual ASR CTC model, whose training corpus includes Tunisian
    Arabic (aeb_Arab). Raw 16 kHz waveform in, logits out, greedy CTC decoding:
    the same graph the plugin runs through Unreal's NNE."""

    repo = "OpenVoiceOS/omnilingual-asr-ctc-300m-onnx"

    def __init__(self, quant: str = "int8"):
        self.file = "model.int8.onnx" if quant == "int8" else "model.onnx"
        self.name = "omni-ctc-300m" + ("" if quant == "int8" else "-fp32")

    def describe(self) -> str:
        return (f"onnxruntime CPU, {self.repo}/{self.file} (Apache-2.0), greedy CTC, "
                f"per-utterance mean/variance normalisation, no language conditioning")

    def load(self, allow_download: bool) -> None:
        import onnxruntime as ort
        from huggingface_hub import hf_hub_download, try_to_load_from_cache

        if not isinstance(try_to_load_from_cache(self.repo, self.file), str) and not allow_download:
            raise NeedsDownload(f"{self.repo}/{self.file} is not cached (328 MB int8, 1.3 GB fp32); "
                                f"re-run with --allow-download")
        model = hf_hub_download(self.repo, self.file)
        tokens = hf_hub_download(self.repo, "tokens.txt")
        self.vocab = {}
        for line in Path(tokens).read_text(encoding="utf-8").splitlines():
            # "<token> <id>"; the space token is written as a literal space
            tok, _, idx = line.rpartition(" ")
            if idx.isdigit():
                self.vocab[int(idx)] = tok
        opts = ort.SessionOptions()
        opts.intra_op_num_threads = 8
        self.session = ort.InferenceSession(model, opts, providers=["CPUExecutionProvider"])
        self.input = self.session.get_inputs()[0].name

    def logits(self, audio: np.ndarray) -> np.ndarray:
        x = audio.astype(np.float32)
        if len(x) < 8000:  # half a second at least, as the plugin does
            x = np.pad(x, (0, 8000 - len(x)))
        x = (x - x.mean()) / (x.std() + 1e-7)
        return self.session.run(None, {self.input: x[None, :]})[0][0]

    def greedy(self, logits: np.ndarray) -> str:
        ids = logits.argmax(axis=-1)
        out, prev = [], -1
        for i in ids:
            if i != prev and i != 0:
                tok = self.vocab.get(int(i), "")
                if not tok.startswith("<"):  # <pad>, </s>, <unk>: as the plugin does
                    out.append(tok)
            prev = i
        return " ".join("".join(out).split())

    def run(self, audio: np.ndarray) -> Result:
        return Result(self.greedy(self.logits(audio)))


class OmnilingualPhraseScoring(OmnilingualCTC):
    """The same model, but each command phrase is scored against its output
    (phrase_scoring.py) instead of matching the text it writes."""

    def __init__(self, clist: cl.CommandList, anywhere: bool, threshold: float, margin: float):
        super().__init__("int8")
        self.clist, self.anywhere = clist, anywhere
        self.threshold, self.margin = threshold, margin
        self.name = "omni-kws" + ("-anywhere" if anywhere else "")
        self.kind = "direct"
        self.phrases = {c.id: [w.ar for w in c.wordings] + [a for w in c.wordings for a in w.arabizi]
                        for c in clist.commands}

    def describe(self) -> str:
        where = "anywhere in the utterance" if self.anywhere else "over the whole utterance"
        return (f"{super().describe()}; each command phrase scored by CTC alignment {where}, "
                f"accepted at {self.threshold:g} nats per character or better, "
                f"{self.margin:g} ahead of the next command")

    def load(self, allow_download: bool) -> None:
        super().load(allow_download)
        self.token_ids = {tok: i for i, tok in self.vocab.items() if not tok.startswith("<")}

    def run(self, audio: np.ndarray) -> Result:
        import phrase_scoring as ps

        logits = self.logits(audio)
        ranked = ps.score_phrases(logits, self.phrases, self.token_ids, self.anywhere)
        (best, command, spelling), second = ranked[0], ranked[1][0] if len(ranked) > 1 else -np.inf
        accepted = best >= self.threshold and best - second >= self.margin
        text = f"{self.greedy(logits)}  [{command} {best:.2f} via {spelling}; next {second:.2f}]"
        return Result(text, command=command if accepted else None, direct=True)


# --- the plugin itself, inside Unreal ----------------------------------------

class UnrealNNE(Engine):
    """What the plugin heard: the transcripts and times the Unreal test
    SpeechRecognition.Transcriber.Omnilingual wrote (same model, run by NNE)."""

    name = "ue-nne"

    def __init__(self, path: Path):
        self.path = Path(path)

    def describe(self) -> str:
        return (f"the plugin in Unreal: Omnilingual CTC 300M int8 through NNE (ONNX Runtime CPU), "
                f"transcripts from {self.path.name} (Tools/Run-Tests.ps1)")

    def load(self, allow_download: bool) -> None:
        if not self.path.exists():
            raise RuntimeError(f"{self.path} does not exist: run Tools/Run-Tests.ps1 first")
        self.rows = {}
        for line in self.path.read_text(encoding="utf-8").splitlines()[1:]:
            parts = line.split("\t")
            if len(parts) == 3:
                self.rows[parts[0].replace("\\", "/")] = (parts[1], float(parts[2]))

    def run_clip(self, clip: dict, audio: np.ndarray) -> Result:
        key = Path(clip["path"]).resolve().relative_to(HERE.resolve()).as_posix()
        if key not in self.rows:
            return Result("", error=f"{key} is not in {self.path.name}")
        text, ms = self.rows[key]
        return Result(text, latency_ms=ms)

    def run(self, audio: np.ndarray) -> Result:
        return Result("")


class UnrealPhraseScoring(UnrealNNE):
    """The plugin's phrase scoring inside Unreal: the command each clip got from
    UVoiceCommandComponent::FindCommandForPhraseScores (darija-nne-kws.tsv)."""

    name = "ue-nne-kws"

    def __init__(self, path: Path):
        super().__init__(path)
        self.kind = "direct"

    def describe(self) -> str:
        return (f"the plugin in Unreal: phrase scoring on Omnilingual's output (NNE), decided by the "
                f"Voice Command component's defaults (-2.5 nats per character, 0.5 ahead), from {self.path.name}")

    def load(self, allow_download: bool) -> None:
        if not self.path.exists():
            raise RuntimeError(f"{self.path} does not exist: run Tools/Run-Tests.ps1 first")
        self.rows = {}
        for line in self.path.read_text(encoding="utf-8").splitlines()[1:]:
            parts = line.split("\t")
            if len(parts) == 5:
                command = None if parts[1] == "-" else parts[1]
                self.rows[parts[0].replace("\\", "/")] = (command, float(parts[2]), parts[3], float(parts[4]))

    def run_clip(self, clip: dict, audio: np.ndarray) -> Result:
        key = Path(clip["path"]).resolve().relative_to(HERE.resolve()).as_posix()
        if key not in self.rows:
            return Result("", error=f"{key} is not in {self.path.name}")
        command, score, phrase, ms = self.rows[key]
        return Result(f"[{command} {score:.2f} via {phrase}]", command=command, direct=True, latency_ms=ms)


# --- (c) French PocketSphinx with Darija words in French phones -------------

class SphinxFrench(Engine):
    def __init__(self, clist: cl.CommandList, mode: str, model_dir: Path,
                 kws_threshold: float, work: Path):
        self.clist, self.mode, self.model_dir = clist, mode, Path(model_dir)
        self.kws_threshold, self.work = kws_threshold, work
        self.name = f"sphinx-fr-{mode}" + (f"@{kws_threshold:g}" if mode == "kws" else "")
        self.kind = "direct"

    def describe(self) -> str:
        how = (f"keyword spotting, one key phrase per wording, threshold {self.kws_threshold:g}, "
               f"longest phrase spotted wins"
               if self.mode == "kws" else "JSGF grammar of all wordings (always picks one)")
        return (f"pocketsphinx (Python), French acoustic model from {self.model_dir}, "
                f"Darija words in French phones from commands.toml, {how}")

    def _write_dict(self) -> Path:
        entries: dict[str, str] = {}
        for cmd in self.clist.commands:
            for w in cmd.wordings:
                for line in w.sphinx:
                    token, phones = line.split(maxsplit=1)
                    if entries.get(token, phones) != phones:
                        raise ValueError(f"token {token!r} has two pronunciations")
                    entries[token] = phones
        self.work.mkdir(parents=True, exist_ok=True)
        path = self.work / "darija-fr.dict"
        path.write_text("".join(f"{t} {p}\n" for t, p in sorted(entries.items())),
                        encoding="ascii")
        return path

    def load(self, allow_download: bool) -> None:
        from pocketsphinx import Decoder

        hmm = self.model_dir / "fr"
        if not (hmm / "mdef").exists():
            raise RuntimeError(f"French acoustic model not found at {hmm}")
        self.phrases, self.phones = {}, {}
        for cmd in self.clist.commands:
            for w in cmd.wordings:
                self.phrases[w.sphinx_phrase] = cmd.id
                self.phones[w.sphinx_phrase] = sum(len(l.split()) - 1 for l in w.sphinx)
        dict_path = self._write_dict()
        self.decoder = Decoder(hmm=str(hmm), dict=str(dict_path), lm=None,
                               samprate=16000, loglevel="FATAL")
        if self.mode == "kws":
            kws = self.work / "darija.kws"
            kws.write_text("".join(f"{p} /{self.kws_threshold:g}/\n" for p in self.phrases),
                           encoding="ascii")
            self.decoder.add_kws("darija", str(kws))
            self.decoder.activate_search("darija")
        else:
            alts = " | ".join(f"( {p} )" for p in self.phrases)
            jsgf = f"#JSGF V1.0;\ngrammar darija;\npublic <cmd> = {alts};\n"
            self.decoder.add_jsgf_string("darija", jsgf)
            self.decoder.activate_search("darija")

    def run(self, audio: np.ndarray) -> Result:
        pcm = (np.clip(audio, -1, 1) * 32767).astype("<i2").tobytes()
        self.decoder.start_utt()
        self.decoder.process_raw(pcm, full_utt=True)
        self.decoder.end_utt()
        hyp = self.decoder.hyp()
        text = hyp.hypstr.strip() if hyp is not None else ""
        command = None
        if text:
            # Key phrase spotting reports every phrase that crossed its threshold,
            # short ones ("ih", "la") many times over. Take the longest phrase heard,
            # in phones: the closest this script gets to the plugin's best-of-overlap.
            heard = [p for p in self.phrases if f" {p} " in f" {text} "]
            if heard:
                command = self.phrases[max(heard, key=lambda p: self.phones[p])]
        return Result(text, command=command, direct=True)


def build(spec: str, clist: cl.CommandList, args) -> Engine:
    base, _, opt = spec.partition("+")
    if base.startswith("whisper-") and base[8:] in FasterWhisper.REPOS:
        return FasterWhisper(base[8:], clist, prompt=(opt == "prompt"), device=args.device)
    if base == "whisper-tunisian":
        return TunisianWhisper(args.device)
    if base == "ue-nne-kws":
        return UnrealPhraseScoring(HERE.parents[1] / "Saved" / "SpeechRecognition" / "darija-nne-kws.tsv")
    if base == "ue-nne":
        return UnrealNNE(HERE.parents[1] / "Saved" / "SpeechRecognition" / "darija-nne.tsv")
    if base in ("omni-kws", "omni-kws-anywhere"):
        return OmnilingualPhraseScoring(clist, base.endswith("anywhere"), args.kws_score, args.kws_margin)
    if base in ("omni-ctc-300m", "omni-ctc-300m-fp32"):
        return OmnilingualCTC("fp32" if base.endswith("fp32") else "int8")
    if base.startswith("sphinx-fr-kws") or base == "sphinx-fr-grammar":
        # sphinx-fr-kws@1e-10 overrides --kws-threshold for that run
        name, _, thr = base.partition("@")
        return SphinxFrench(clist, name.rsplit("-", 1)[1], args.sphinx_model,
                            float(thr) if thr else args.kws_threshold, args.work)
    raise SystemExit(f"unknown engine {spec!r}")


# "all" means every engine.
ALL = ["whisper-base", "whisper-base+prompt", "whisper-small", "whisper-small+prompt",
       "whisper-turbo", "whisper-turbo+prompt", "whisper-tunisian", "omni-ctc-300m", "omni-kws",
       "sphinx-fr-kws", "sphinx-fr-grammar"]
