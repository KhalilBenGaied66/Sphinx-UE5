# Speech models

The plugin recognises speech with a CMUSphinx model: an acoustic model (what
the sounds of a language look like) and a pronunciation dictionary (which sounds
make up each word). No model is in this repository; they are downloaded.

## Getting them

```
powershell -ExecutionPolicy Bypass -File Tools\Get-Models.ps1 -Language en,fr,de
```

| Code | Language | Download | Licence |
|---|---|---|---|
| `en` | US English | 14 MB | BSD-style |
| `fr` | French | 9 MB | BSD-style |
| `de` | German | 44 MB | LGPL 3 |

Add `-LanguageModel` to also fetch the n-gram model that language model mode
needs (27 MB, 17 MB; the German one comes in the same archive). Keyword and
grammar modes do not need it.

The script writes to `Content/model` of the sample project. Pass
`-Destination <folder>` to write somewhere else, for instance the
`Content/model` of your own project.

## Layout

One folder per language, named by a code of your choosing:

```
Content/model/
  de/
    de/                        acoustic model: mdef, means, variances, feat.params...
    de.dict                    pronunciation dictionary, UTF-8, lower case
    grammars/commands.gram     JSGF grammars            -> Enable Grammar Mode "commands"
    language_models/de.lm.bin  n-gram models            -> Enable Language Model Mode "de"
```

`Init With Language Code "de"` reads `de/de` and `de/de.dict`. A grammar or a
language model is looked up by name when its mode is enabled; `.lm` is tried
first, then `.lm.bin`.

The folder itself is a project setting: *Project Settings > Plugins > Speech
Recognition > Model Directory*, relative to `Content` or absolute.
`Get Installed Languages` returns the codes that have both an acoustic model
and a dictionary there.

## Another language

Any CMUSphinx acoustic model that the 2016 build of PocketSphinx can read will
do. The project publishes models for Dutch, Greek, Hindi, Italian, Kazakh,
Mandarin, Portuguese, Russian, Spanish and others at
<https://sourceforge.net/projects/cmusphinx/files/Acoustic%20and%20Language%20Models/>.
Download one, arrange it as above under a folder named by the code you want to
use, and pass that code to `Init With Language Code`. Nothing has to be
recompiled.

Two things to check in a dictionary:

- **Encoding and case.** The dictionary must be UTF-8 and its words lower case.
  The plugin lower-cases the phrases you give it (with Unicode rules, so
  `ÖFFNE` finds `öffne`) and looks each word up as it is spelt.
- **Coverage.** A key phrase with a word that is not in the dictionary is left
  out, with a warning in the log; `Get Rejected Phrases` lists them. To use a
  word the dictionary lacks, add a line for it: the word, then its phones in
  the model's phone set.

### Arabic and Tunisian Darija

CMUSphinx publishes no Arabic model, and training one needs tens of hours of
transcribed speech. Tunisian Darija mixes Arabic with French and has no
standard spelling, so it would need its own. The plugin handles Arabic with a
modern speech model instead, behind the same Voice Command component. A game
sets the language code (`ar` for Modern Standard Arabic, `aeb` for Tunisian,
`ary` Moroccan, `arz` Egyptian, `apc` Levantine...) and writes each command's
phrases, in Arabic script or in the Latin letters people type (`حلّ الباب`,
`7ell el beb`). There is no dictionary: any phrase can be written, as with
PocketSphinx's English model but without the words having to be in a
pronunciation dictionary. One spelling per phrase is enough; more help.

How it works, for any language that has no PocketSphinx model:

1. The microphone is read as for every language. An energy detector finds each
   utterance and keeps 0.3 s from before it, so the first syllable is not cut.
   0.45 s of silence ends the utterance.
2. The utterance goes to a transcription engine on a background task, so the
   microphone keeps being read meanwhile.
3. **Phrase scoring** (the default). The model writes one character, or a
   blank, every 20 ms. Each phrase being listened for is aligned with those
   per-frame scores: the best path that spells exactly that phrase (CTC
   Viterbi), compared with the model's own best path, in nats per character
   (`FPhraseScorer`). 0 means the model wrote that very phrase. The phrase must
   explain the whole utterance, so a sentence that merely contains a command
   word scores low. An Arabizi phrase is also scored in Arabic script, through a
   rough transliteration, and word breaks are optional. This is what
   PocketSphinx does with key phrases, on a model that knows Arabic letters.
   It does not depend on how the model spells: on one synthetic clip the model
   wrote `беркә`, in Cyrillic letters, for بركا, and the phrase still won.
4. The Voice Command component fires the command whose best phrase scores -2.5
   or better and leads the next command by 0.5 (`Phrase Score Threshold`,
   `Phrase Score Margin`).
5. With `Transcript Matching` set to Text, the component compares the text with its phrases instead
   (`FTranscriptMatcher`). Spellings are normalised (harakat, hamza forms and
   ڨ/ق unified, Arabizi digits to letters), and across scripts both sides are
   reduced to a consonant skeleton (9 = ق = q = g, 7 = ح, ch = ش). The best
   command fires at 80 out of 100 or more and 5 ahead of the next one.

The engine:

| Engine | Runs | Size | Licence | State |
|---|---|---|---|---|
| Omnilingual ASR CTC 300M (Meta), int8 ONNX export by OpenVoiceOS: the default | offline, on the CPU, through Unreal's Neural Network Engine (the NNERuntimeORT plugin, part of the engine). Ships with the game. One folder, `Content/model/omnilingual`, serves the code `any` and every Arabic code; a code's own folder (`model/aeb`) takes precedence | 328 MB (`Tools/Get-Models.ps1 -Language omnilingual`) | Apache-2.0 | built; tested on synthetic Darija, invented words, and two live sessions (below) |

Why Omnilingual first: of the candidates below, it is the only one that runs
inside Unreal with nothing added, because ONNX runs on the engine's own NNE.
It is also under 1 GB, licensed for commercial use, and its training corpus
includes Tunisian Arabic (`aeb_Arab`, among about 1600 languages). Whether it
understands Darija commands well enough has to be measured on recorded speech.

Candidates looked at (October 2026; sizes from the Hugging Face API, results as
their authors publish them):

| Model | Size | Licence | Published result | Here |
|---|---|---|---|---|
| Omnilingual ASR CTC 300M, ONNX | 328 MB (int8) | Apache-2.0 | none for Tunisian | the default engine |
| oddadmix/Whisperv3-tunisian-codeswitch: Whisper large-v3 for Tunisian Arabic mixed with French, 3rd in the NADI 2026 Tunisian code-switched ASR task | 6.2 GB | not stated | blind-test WER 15.2% | not downloaded: over 1 GB, licence unknown |
| TuniSpeech-AI/whisper-tunisian-dialect: Whisper large-v2 + LoRA, 21 h of Tunisian speech | 3.1 GB | CC-BY-NC-4.0, no commercial use | none | not downloaded |
| linagora/linto-asr-ar-tn-0.1: Kaldi TDNN for Tunisian | 0.8 GB | Apache-2.0 | WER 20.5% to 37.5% on its three test sets | not tried: needs a Kaldi runtime |
| medfadiabaidi/whisper-small-tunisian-asr: Whisper small on TEDxTN (22 h) | 1 GB | not stated | WER 38.0% on TEDxTN | not tried |
| Gemma 4 E2B / E4B (Google, open weights, audio input) | 10 / 16 GB (E2B 4.3 GB as 4-bit GGUF) | Apache-2.0 | no figure for Darija | not downloaded: too heavy to run next to a game |
| Whisper base, small, large-v3-turbo (OpenAI, through faster-whisper) | 148 MB, 486 MB, 1.6 GB | MIT | none for Tunisian | measured on synthetic audio in `Tools/DarijaEval`; would need whisper.cpp inside the plugin |
| The French PocketSphinx model with Darija words in French phones | already here | the model's | none | measured on synthetic audio: fires on every distractor |

Measured inside Unreal (UE 5.8, i7-14700F, `Tools/Run-Tests.ps1 -Filter
SpeechRecognition.Transcriber -OrtThreads <n>`), on 115 **synthetic** clips:
Microsoft's two Tunisian neural voices reading Arabic script, and the French
Windows voice reading Arabizi. They say how fast the engine is, not how well
it understands a player:

| ONNX Runtime threads | 1 (NNE's default in a packaged game) | 4 (the sample's game setting) | 8 (the sample's editor setting) |
|---|---|---|---|
| Median time per clip, model warm | 2388 ms | 716 ms | 431 ms |

The model loads in 1.6 to 3.1 s, on the game thread, the first time a Darija
mode starts. The Unreal transcripts match onnxruntime's in Python on 84 of 107
clips. The others differ by a letter or two, plausibly int8 kernels and thread
scheduling. Set the thread count for your game in Project Settings > Plugins >
NNERuntimeORT, or as the sample does in `Config/DefaultEngine.ini`:

```ini
[/Script/NNERuntimeORT.NNERuntimeORTSettings]
GameThreadingOptions=(bUseGlobalThreadPool=False,IntraOpNumThreads=4,InterOpNumThreads=1,ExecutionMode=SEQUENTIAL)
```

Scoring 143 phrases adds about 8 ms per utterance to the model's own time
(same run, same clips). Absolute times on this machine varied by a factor of
two between sessions, in Python and in Unreal alike, with the CPU otherwise
idle: compare times from one run only.

How each step changed recognition, on the same 89 synthetic command clips and
18 distractors, inside Unreal (`Tools/DarijaEval`, engines `ue-nne` and
`ue-nne-kws`):

| Matching | Hit | False match | False accept |
|---|---|---|---|
| text, same script only (first version) | 42/89 | 1 | 0/18 |
| text, with cross-script matching | 50/89 | 1 | 0/18 |
| phrase scoring (the default) | **78/89** | **0** | **0/18** |

The phrase scoring thresholds (-2.5, 0.5) were chosen on these clips, which
flatters its row. With one Arabizi spelling per command instead of every
spelling, phrase scoring in Python recognised 30 of the 38 clips that said
that wording, against 32 of 38 with every spelling; with one Arabic spelling,
24 of 38.

The first live test (the author, 2026-10-20, Tunisian Darija into the sample):
about 75% of commands worked by the author's count, with the first version's
text matching. The plugin logged 54 transcripts. The first version found a
command in 18 of them, cross-script matching in 30. The audio was not kept, so
phrase scoring cannot be replayed on it. They also showed what the model does
with short words (أُقف came out as `يقف`, `eqf`, `iقف`) and that it sometimes
writes Hindi, Korean or Ethiopic letters. All of these are synthetic audio or
unlabelled transcripts: only recordings of people with known commands can
rank the methods. `Tools/DarijaEval/record.py` makes them.

**Letters of other scripts.** Omnilingual's CTC model is not told which
language to expect, and on a short or unclear clip it sometimes writes Hindi,
Korean or Ethiopic letters (`aलiसर`, `이민`). Nothing in the plugin pins Arabic
codes to Arabic output, and phrase scoring does not need it to: only the
letters of your phrases are scored, so stray scripts in the model's own text do
not decide anything. They do show up in the log and on screen.

**Any phrase, no language (`any`).** Phrase scoring does not use the language
code: it checks whether the letters of a phrase fit the sounds the model
heard, with the letters of every script it was trained on. So the same engine
recognises words of no language at all. Five invented spell phrases ("amorix
valor", "corva aeterna", "sanarel vitae", "zelthar morun", "kraveth ulnar"),
written once each in Latin letters and spoken by the English and the French
Windows voices: 10 of 10 recognised, 0 wrong, and none of 8 sentences of chat
fired a spell. The model's own text was never right ("ameriqs veller" for
"amorix valor", "ça na rel vité" for "sanarel vitae"); the phrase still won by
three nats per character or more. Spell a word the way it sounds, and give two
spellings when unsure. Synthetic voices only: nobody has spoken them yet
(`Tools/DarijaEval/spells_check.py` reproduces it).

Modern Standard Arabic and the other Arabic codes use the same model and the
same scoring. The sample has an MSA command set (`-SpeechLanguage=ar`, phrases
written once each in Arabic script) and a spells set (`-SpeechLanguage=any`),
and the live test switches between `aeb`, `en`, `ar` and `any` on the
microphone. Nobody has spoken MSA into it yet.

`Tools/DarijaEval` holds the command list (22 commands, Arabic and Arabizi
spellings, French loanwords marked), a recording tool, and the scoring script
that compares every engine on the same clips. Its README says what to record.

## Shipping a game

The models are plain files, not Unreal assets. A packaged build only contains
them if the project stages their folder. In `Config/DefaultGame.ini`:

```ini
[/Script/UnrealEd.ProjectPackagingSettings]
+DirectoriesToAlwaysStageAsNonUFS=(Path="model")
```

The plugin writes this line to the editor log when it finds the model folder
inside `Content` and not staged. Ship only the languages you use, and only the
files a mode needs: keyword and grammar modes need the acoustic model and the
dictionary, a few megabytes to a few tens.

Each model keeps the licence of its authors
([THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)). The German model is
LGPL 3: read what that asks of you before you ship it.
