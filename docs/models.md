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

CMUSphinx publishes no Arabic model, so Arabic is not available today.
Training one with SphinxTrain needs tens of hours of transcribed speech, and
Tunisian Darija, which mixes Arabic with French and has no standard spelling,
would need its own. The plan is a second engine instead, behind the same
Voice Command component, so that a game changes language and nothing else.

What was looked at (October 2026, nothing of it run yet):

| Candidate | Where it runs | For Darija commands |
|---|---|---|
| whisper.cpp with a Whisper model fine-tuned on Tunisian speech (for example TuniSpeech-AI/whisper-tunisian-dialect, from Whisper large-v2) | offline, CPU or GPU, C/C++ under the MIT licence: embeddable in the plugin | the most likely route. The published Tunisian fine-tunes are large models; a game wants a small one, which may mean fine-tuning Whisper small on open Tunisian data |
| Gemini (Google), through its API | online | handles Derja when the prompt names the dialect (the author's Derja voice-agent prototype uses it), but needs a network, an API key that cannot ship inside a game, and a quota. Useful as a reference to score the others, not as the engine |
| Gemma audio models (Google, open weights, E2B and E4B) | offline | built for speech recognition in many languages; Darija quality not published; too heavy for a command recogniser running next to a game |
| A small keyword-spotting network trained on recorded Darija commands, run with Unreal's Neural Network Engine | offline, tiny | the best fit for a fixed command list, but it needs recordings: each command, many speakers, several takes |
| The French acoustic model with Darija words added to its dictionary in French phones | offline, possible today | a cheap experiment: French and Darija share many sounds and loanwords, but ع, ح and ق have no close French equivalent. May work for a handful of distinct commands, unmeasured |

The first step is a measurement, not an engine: twenty Darija commands
recorded by a few speakers, scored against each candidate, with Gemini as the
reference.

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
