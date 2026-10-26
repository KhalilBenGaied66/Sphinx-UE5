# Tools/DarijaEval: Darija voice commands, measured

Which speech recogniser can understand a short game command spoken in Tunisian
Darija? CMUSphinx has no Arabic model, so the plugin transcribes Darija with a
second engine (Omnilingual ASR through Unreal's NNE, see
[docs/models.md](../../docs/models.md#arabic-and-tunisian-darija)) and matches
the text against the command list. This folder measures that engine, inside
Unreal and out, against the alternatives, on the same clips.

It has three parts: a list of 22 Darija commands with their spellings, a
recording tool, and a scoring script that runs each candidate on the
recordings. The plugin's C++ matcher is a port of `commands.py`, kept equal by
a test (`export_cpp_cases.py`).

## Status

| Item | State | How to check |
|---|---|---|
| Command list, 22 commands with 1-3 wordings and 6 distractors (`commands.toml`) | written by Claude from general knowledge of Tunisian Arabic, with a few wordings from a second Claude draft; **not yet checked by a Tunisian speaker** | `uv run --extra tunisian python selftest.py` (every spelling maps back to its command: 150 checked, 0 failures) |
| Recording tool (`record.py`) | written; device listing run; **not yet run with a microphone** | `uv run --extra tunisian python record.py --list-devices` |
| Real recordings | **none yet** | `recordings/manifest.csv` does not exist |
| Placeholder audio | two synthetic sets: 49 clips of the French Windows voice reading the Arabizi spellings (`placeholder.py`), and 66 clips of Microsoft's two Tunisian neural voices (ar-TN Hedi and Reem, through the free online edge-tts service) reading Arabic script, made by another Claude session with its own wordings; 8 of those are commands not in this list and are skipped | `uv run --extra tunisian python placeholder.py`; the edge-tts set is in `recordings/placeholder-edge/` |
| Scoring (`score.py`) | ran end to end on the placeholder audio with the engines below | see Results |
| Whisper base, small, large-v3-turbo (faster-whisper) | run on placeholder audio, on the GPU (each with and without the command prompt) and on the CPU | |
| Tunisian fine-tune (TuniSpeech-AI/whisper-tunisian-dialect) | adapter written, **not run**: the model is a 3.09 GB download, waiting for the user's OK | `uv run --extra tunisian python score.py --engines whisper-tunisian` stops and prints the size |
| French PocketSphinx + Darija words in French phones | run on placeholder audio: key phrase spotting at the plugin default threshold (other thresholds: `--engines sphinx-fr-kws@1e-10`) and grammar mode | |
| Omnilingual ASR CTC 300M (the plugin's engine), through onnxruntime in Python | run on placeholder audio | `--engines omni-ctc-300m` |
| The plugin itself, in Unreal | run on placeholder audio: the Unreal test writes its transcripts, `score.py` scores them | `Tools/Run-Tests.ps1 -Filter SpeechRecognition.Transcriber`, then `--engines ue-nne` |

**No number in this folder says anything about Darija yet.** The only audio so
far is synthetic: a French voice reading Latin spellings, and two Tunisian
neural voices that lean toward Modern Standard Arabic. It shows that each tool
loads, runs and gets scored. It does not measure recognition.

## What to record

1. **Correct the list first.** Read `commands.toml`. Change any wording people
   don't say, add the one they do say, and remove what is wrong. Then run
   `uv run --extra tunisian python selftest.py`. A failure means two commands are now too close
   for the matcher, or a spelling is missing.
2. **Pick the microphone a player would use** (headset or laptop mic) and a
   normal room. `uv run --extra tunisian python record.py --list-devices` shows the inputs.
3. **Record:** `uv run --extra tunisian python record.py --speaker <id>`, where the id is letters
   and digits, e.g. `khalil` or `s02`. The tool asks for each command four
   times, shuffled, rotating through its wordings, then the six distractors
   twice: chat that is not a command, and one silent clip. That is 100 clips of
   2.5 s, about thirteen minutes. Say each line the way you would say it to a game.
   At the prompt: Enter = record, r = redo the last clip, s = skip, q = quit.
   Running the same command again resumes where you stopped.
4. **More speakers.** One speaker measures one voice. Aim for at least three,
   better five or more: women and men, and more than one region (in Tunis ق is
   /q/, elsewhere it is often /g/). Use one `--speaker` id per person.
5. **Score:** `uv run --extra tunisian python score.py --engines all --device cuda`
   (see Candidates for what needs a download first). `all` is every engine;
   nothing leaves the PC. For the plugin itself,
   run `Tools\Run-Tests.ps1 -Filter SpeechRecognition.Transcriber` first: it
   transcribes every clip under `recordings/` inside Unreal, and
   `--engines ue-nne` scores what it heard.
6. **Or try it live:** run the sample with `-SpeechLanguage=aeb` (the repository
   README says how) and speak to the cube. That is a demonstration; only the
   recorded clips give numbers.

Consent: recordings are personal data (Tunisian law 2004-63). They stay in
`recordings/`, which is git-ignored. Every engine runs on this PC, so no clip
leaves it. Ask each speaker before recording all the same.

Arabic letters may show up disconnected or left-to-right in a Windows console.
The Arabizi line under each prompt is there for that reason.

## Candidates

| Engine name | What | Download | Licence | Runs |
|---|---|---|---|---|
| `whisper-base`, `whisper-small` | Whisper through faster-whisper (CTranslate2), language `ar`, beam 5 | 148 MB, 486 MB (downloaded) | MIT | offline; CPU int8, or GPU float16 with `--device cuda` (name ends in `@gpu`) |
| `whisper-turbo` | Whisper large-v3-turbo, same settings | none: it was already in this machine's Hugging Face cache (1.6 GB on disk) | MIT | offline, CPU int8 |
| `…+prompt` | the same model with the command list given as `initial_prompt`, which biases it toward the commands | | | |
| `whisper-tunisian` | TuniSpeech-AI/whisper-tunisian-dialect: Whisper large-v2 with a LoRA trained on about 21 h of Tunisian speech, merged into the weights; through transformers, fp16 on the GPU | **3.09 GB**; needs `uv sync --extra tunisian` too (torch 2.11 cu128 from the local uv cache, transformers) | **CC-BY-NC-4.0: no commercial use**, so it can be measured but not shipped in a commercial game | offline, GPU. About 3.1 GB of fp16 weights, which fits easily in the 16 GB of the RTX 5060 Ti |
| `sphinx-fr-kws[@threshold]`, `sphinx-fr-grammar` | pocketsphinx 5.1 with the French acoustic model from `Content/model/fr` and each Darija wording spelled in French phones (`sphinx` lines in `commands.toml`) | none (30 MB, already local) | BSD (pocketsphinx); the French model's own licence | offline, CPU |
| `omni-ctc-300m` | Omnilingual ASR CTC 300M (Meta), int8 ONNX, through onnxruntime, greedy CTC: the model the plugin runs | 328 MB | Apache-2.0 | offline, CPU (8 threads) |
| `ue-nne` | the same model run by the plugin inside Unreal (NNE, ONNX Runtime CPU): transcripts and times read from `Saved/SpeechRecognition/darija-nne.tsv` | none | | Unreal, CPU |
| `omni-kws` | the same model, with each phrase scored against its output (`phrase_scoring.py`, CTC Viterbi over the whole utterance) instead of matching its text. `--kws-score` (-2.5) and `--kws-margin` (0.5) set the acceptance; `omni-kws-anywhere` lets the phrase sit anywhere in the utterance | none more | Apache-2.0 | offline, CPU |
| `ue-nne-kws` | phrase scoring inside Unreal: the command the Voice Command component chose for each clip (`darija-nne-kws.tsv`) | none | | Unreal, CPU |

The Whisper engines write text, which `commands.py` matches to the
command list. The text is normalised first: Arabic harakat, hamza forms and ڨ/ق
are unified, and Arabizi digits map to letters. Then it is compared, a window of
words at a time, with every Arabic and Latin spelling of every wording
(rapidfuzz ratio). The best command is accepted at a score of 80 or more and at
least 5 points ahead of any other command. Phrases of three letters or fewer
(`la`, `ih`) must match exactly. PocketSphinx listens for the phrases
themselves, so its output maps to a command directly:

- **kws:** key phrase spotting. The default threshold is 1e-30, the plugin's V5
  tolerance, which is the Voice Command component's default. When several
  phrases fire, the longest one (in phones) wins.
- **grammar:** always picks one of the phrases, so it can never stay silent on a
  distractor.

No engine downloads a model unless `score.py` is given `--allow-download`.
Without that flag, a missing model stops that engine and prints its size.

## What is measured

For every clip and engine: the output, the command it maps to, and the time the
engine took on that clip. Each model is loaded and warmed up on one clip before
timing starts. Model load time is reported separately.

| Term | Meaning |
|---|---|
| hit | a command clip mapped to its own command |
| false match | a command clip mapped to a different command |
| miss | a command clip mapped to no command |
| false accept | a distractor (chat or silence) mapped to any command |

Each run writes `results/<date-time>/report.md` (summary, per-command table,
French loan wordings against Darija ones, confusions), `results.csv` and
`raw.jsonl` with every transcript. The report header names the audio, its source
and the machine.

## Results

**Placeholder audio only. The hit rates below say nothing about Darija.** They
come from synthetic voices: 89 command clips and 18 distractor clips from the
two sets in Status. They show that every engine loads, runs and gets scored.
Latency is the one column that roughly carries over to real speech: the
compute depends on the model and the clip length, hardly on what was said.

GPU run, all engines (`results/20261019-212942/report.md`):

```bash
uv run --extra tunisian python score.py --manifest recordings/placeholder/manifest.csv recordings/placeholder-edge/manifest.csv --engines all --device cuda
```

| Engine | Hit | False match | Miss | False accept | Latency median / p90 (ms) |
|---|---|---|---|---|---|
| whisper-base@gpu | 32/89 | 2/89 | 55/89 | 0/18 | 56 / 88 |
| whisper-base+prompt@gpu | 46/89 | 5/89 | 38/89 | 0/18 | 63 / 94 |
| whisper-small@gpu | 40/89 | 1/89 | 48/89 | 0/18 | 103 / 155 |
| whisper-small+prompt@gpu | 57/89 | 3/89 | 29/89 | 1/18 | 112 / 166 |
| whisper-turbo@gpu | 37/89 | 0/89 | 52/89 | 1/18 | 178 / 195 |
| whisper-turbo+prompt@gpu | 49/89 | 0/89 | 40/89 | 2/18 | 184 / 201 |
| sphinx-fr-kws@1e-30 | 26/89 | 63/89 | 0/89 | 18/18 | 86 / 105 |
| sphinx-fr-grammar | 54/89 | 32/89 | 3/89 | 17/18 | 37 / 49 |

`whisper-tunisian` was not run (not downloaded). That run also had a Gemini
column (online, 9 of 22 clips before the free tier's 20 requests a day ran
out); the Gemini engine was removed in 2.1.1, since a game needs a recogniser
that works offline.

The plugin's engine, in Python and in Unreal (`results/20261020-005104/report.md`;
the Unreal transcripts from `Tools/Run-Tests.ps1 -Filter SpeechRecognition.Transcriber -OrtThreads 8`):

```bash
uv run --extra tunisian python score.py --manifest recordings/placeholder-edge/manifest.csv recordings/placeholder/manifest.csv --engines omni-ctc-300m,ue-nne
```

| Engine | Hit | False match | Miss | False accept | Latency median / p90 (ms) |
|---|---|---|---|---|---|
| omni-ctc-300m (onnxruntime, 8 threads) | 40/89 | 0/89 | 49/89 | 0/18 | 349 / 433 |
| ue-nne (the plugin in Unreal, 8 threads) | 42/89 | 1/89 | 46/89 | 0/18 | 431 / 544 |

The two transcribe 84 of the 107 clips identically; the rest differ by a
letter or two.

### Cross-script matching and phrase scoring (2026-10-20)

Matching the model's text: same script only (the first version) against
cross-script matching, re-run on the transcripts already written
(`compare_matchers.py results/20261019-212942/raw.jsonl results/20261020-005104/raw.jsonl`).
"one Arabizi spelling" keeps only the first Arabizi spelling of each command's
first wording, as if a developer had typed the command once:

| Engine | Command list | Same script only | Cross-script |
|---|---|---|---|
| ue-nne (the plugin) | every spelling | 42/89 hits, 1 false | 50/89, 1 false |
| ue-nne | one Arabizi spelling | 5/89, 0 | 26/89, 0 |
| ue-nne | one Arabic spelling | 17/89, 0 | 29/89, 0 |
| whisper-small+prompt@gpu | every spelling | 57/89, 3 | 66/89, 2 |
| omni-ctc-300m | every spelling | 40/89, 0 | 49/89, 1 |

No row changed its false accepts on the 18 distractors (0 for these engines,
1 for whisper-small+prompt).

Phrase scoring (`results/20261020-023237`, `-022917`, `-023105`, `-025626`):

| Engine | Command list | Hit | False match | False accept |
|---|---|---|---|---|
| omni-kws (Python) | every spelling | 80/89 | 0 | 0/18 |
| ue-nne-kws (the plugin in Unreal) | every spelling | 78/89 | 0 | 0/18 |
| omni-kws | one Arabizi spelling | 38/89 | 3 | 0/18 |
| omni-kws | one Arabic spelling | 31/89 | 1 | 0/18 |

The one-spelling rows are capped by clips that say another wording than the
one kept. On the 38 clips that say it: 30 hits with one Arabizi spelling, 24
with one Arabic spelling, 32 with every spelling.

**The thresholds (-2.5 nats per character, 0.5 margin) were chosen on these same
clips**, from this sweep of `omni-kws` (every spelling):

| Score at least | Margin at least | Hit | False match | False accept |
|---|---|---|---|---|
| -1.5 | 0.5 | 70/89 | 0 | 0/18 |
| -2.0 | 0.5 | 75/89 | 0 | 0/18 |
| -2.5 | 0.5 | 80/89 | 0 | 0/18 |
| -3.0 | 0.5 | 82/89 | 0 | 0/18 |
| -3.0 | 0.0 | 87/89 | 0 | 3/18 |
| none | none | 88/89 | 1 | 18/18 |

Placing the phrase anywhere in the utterance (`omni-kws-anywhere`) found
commands inside sentences but fired on 5 to 16 of the 18 distractors at the
same settings, so the plugin requires the phrase to explain the whole
utterance.

The live session (`results/live-2026-10-20.tsv`, 54 transcripts from the
plugin's log, without labels): the first version found a command in 18 of
them, cross-script matching in 30 (`compare_matchers.py --live`). The audio was
not kept, so phrase scoring cannot be replayed on it.

CPU run, Whisper without prompt (`results/20261019-213154/report.md`):

```bash
uv run --extra tunisian python score.py --manifest recordings/placeholder/manifest.csv recordings/placeholder-edge/manifest.csv --engines whisper-base,whisper-small,whisper-turbo --device cpu
```

| Engine | Hit | False match | Miss | False accept | Latency median / p90 (ms) |
|---|---|---|---|---|---|
| whisper-base | 27/89 | 2/89 | 60/89 | 0/18 | 861 / 914 |
| whisper-small | 43/89 | 3/89 | 43/89 | 0/18 | 2591 / 2674 |
| whisper-turbo | 41/89 | 0/89 | 48/89 | 1/18 | 11500 / 15413 |

Machine: i7-14700F, RTX 5060 Ti 16 GB, faster-whisper 1.2.1, CTranslate2 4.8.3,
pocketsphinx 5.1.1, torch 2.11.0+cu128. Hit counts differ slightly between the
GPU (float16) and CPU (int8) runs of the same model.

What the plumbing already shows, independent of the audio:

- **Whisper on the CPU is too slow for a game command:** about 0.9 s for base,
  2.6 s for small and 11.5 s for turbo per clip on this i7. On the GPU the same
  models take 56–184 ms. Whisper always encodes a 30 s window, so streaming
  short commands to it costs the same as a long sentence.
- **Key phrase spotting with Darija words in French phones fires on
  everything** at the plugin's default tolerance: every distractor and every
  silent clip triggered a command. Short phrases (ih, la, hbes) are the cause.
  Grammar mode always picks a phrase, so it can never reject a distractor.
- **The command prompt raises Whisper's hits but leaks command words into
  chat.** With the prompt, turbo wrote the weather line as "هزّها، أُمزيان،
  ليوم" (pick it up) and small wrote it as "إيه، ..." (yes).
- **Fuzzy matching makes its own false accepts:** turbo heard "chnowa a7walek"
  as "شنوة أوليك", and "أوليك" is close enough to "أنولي" (annuli, cancel) to
  fire "no". Short wordings make the matcher fire easily; the threshold and
  margin need tuning on real recordings held out from the test.

## Limits

- The command list is a draft by a non-speaker. Until a Tunisian speaker has
  corrected it, a low score may just mean the expected wording is wrong.
- Latency is measured on 2.5 s clips, one at a time, with the model warm. A
  game would stream audio and detect the end of speech; that is not measured.
  Whisper pads every clip to a 30 s window, so its time barely depends on the
  clip length.
- CPU numbers use int8 and GPU numbers float16 for the faster-whisper models.
  A game renders on the same GPU; the GPU latencies here are measured on an
  idle card.
- The fuzzy matcher's threshold (80) and margin (5) are set by hand. They are
  printed in every report and can be changed with `--threshold` and `--margin`.
  A threshold tuned on the same recordings it is scored on would flatter the
  result.
- The French-phone spellings are a guess. ع, ح and ه have no French phone and
  are dropped, which shortens commands like حبس to two phones and makes them
  fire easily.
- The model card of the Tunisian fine-tune reports no word error rate. Its
  `config.json` sets English as the forced language token, so the adapter
  forces Arabic explicitly. Other Tunisian models, with their published
  results, are listed in `docs/models.md`; the strongest published one
  (oddadmix/Whisperv3-tunisian-codeswitch, 6.2 GB, licence not stated) has no
  adapter here yet.
- Omnilingual's CTC model is not told which language to expect: on a short or
  unclear clip it can answer in Latin or Cyrillic letters (`беркә` for بركا in
  the placeholder run). Latin output is still matched against the Arabizi
  spellings; Cyrillic matches nothing.

## Files

| File | Purpose |
|---|---|
| `commands.toml` | the commands, wordings, spellings, loanword marks, French phones, distractors |
| `commands.py` | loading the list, normalisation, fuzzy matching |
| `selftest.py` | checks the list and the matcher |
| `record.py` | the recording tool |
| `placeholder.py` | synthetic placeholder clips |
| `engines.py` | the candidate recognisers |
| `score.py` | runs the engines, writes the report |
| `export_cpp_cases.py` | writes the cases the plugin's C++ matcher and phrase scorer are tested against (`Plugins/SpeechRecognition/Resources/TestAudio/darija/`) |
| `phrase_scoring.py` | phrase scoring: CTC alignment of each phrase, Arabizi to Arabic transliteration |
| `compare_matchers.py` | old and new text matching on transcripts already written, and on live transcripts |
| `spells_check.py` | invented spell words, no language: five spells and four sentences of chat on two synthetic voices, scored with the plugin's default acceptance |

Set up with `uv sync --extra tunisian` (Python 3.11; the extra brings torch
2.11 cu128 and transformers). Then always use `uv run --extra tunisian ...`:
a plain `uv run` re-syncs to the base dependencies and uninstalls torch.
Without the extra, only the CPU engines run. The GPU route reuses the
cuBLAS and cuDNN DLLs inside the torch wheel for CTranslate2, so no CUDA
toolkit is needed. Free tools only; nothing here needs a paid service.
