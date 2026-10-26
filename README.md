# Sphinx-UE5: offline voice commands for Unreal Engine 5

A speech recognition plugin for Unreal Engine 5 built on
[PocketSphinx](https://github.com/cmusphinx/pocketsphinx). It listens to the
microphone on a worker thread and raises Blueprint events when the player says
one of your phrases. It runs offline, on the CPU, in about 26 MB of memory for
English, and decodes more than a hundred times faster than real time on one
thread.

It is a recogniser for **commands**: a short list of phrases, a grammar, at a
push a sentence. It is not a transcription engine, and a model from the 2010s
will not behave like one from the 2020s. In return it costs almost nothing to
run next to a game.

**A second engine recognises any phrase you can write.** Tunisian Darija,
Modern Standard Arabic, a spell called "amorix valor": type the command, in
Arabic script or Latin letters, and the game hears it. There is no dictionary
to fill and nothing to train. A modern speech model (Meta's Omnilingual ASR,
328 MB, Apache-2.0) runs offline inside Unreal on the engine's own Neural
Network Engine, and each phrase is scored against what it heard, the way
PocketSphinx spots key phrases. Use the language code `any`, or an Arabic code
(`ar`, `aeb` Tunisian, `ary` Moroccan, `arz` Egyptian...): they all share one
model. Both engines sit behind the same Voice Command component. See Status
for what has been measured.

## What it is for

Voice as an input, next to the keyboard and the gamepad: a known set of things
the player can say, each mapped to an action.

- **Accessibility.** Menus, camera, and the actions that need a button, for
  players who cannot press one easily. A Voice Command component per screen
  changes what can be said without reloading anything.
- **Hands that are busy.** VR and XR (on a Windows PC for now), simulators,
  cockpits: "lower the gear", "next page", "call the tower".
- **Orders to a squad or a companion.** "Flank left", "hold position",
  "follow me". Commands are reported with a gameplay tag, so they can feed the
  same routing as the input system.
- **Voice as the mechanic.** Spells said aloud, passwords at a door, a horror
  game that listens for the player. This plugin started in such a game: the
  author's university capstone, a voice-driven prototype built with Lanterns
  Studios.
- **Training and serious games.** A procedure said step by step and checked as
  it is said, offline, with nothing recorded or sent anywhere.
- **Kiosks and installations.** Museums, exhibitions, trade-show stands: no
  network, no account, no per-minute bill.

What it is not for: dictation, free conversation with a character, or open
questions. Those need a transcription model; see the next section.

## Choosing a recogniser

| | This plugin (PocketSphinx) | A Whisper-based plugin (whisper.cpp) | A cloud service (Gemini, Azure, Google Cloud) |
|---|---|---|---|
| Best at | a list of commands or a grammar | transcribing free speech | transcribing free speech, dialects, noisy audio |
| Runs | offline, on the CPU, alongside the game | offline, on the CPU or the GPU | online, on someone else's servers |
| Size and cost | 26 MB of memory for English, about 7 ms of one core to decode a second of speech | from about 75 MB (tiny) to several GB (large) | an API key, a quota, a bill or a free tier |
| When the game hears it | as the phrase is spotted, before the speaker stops if asked | after a chunk of audio is complete | after the audio has gone and come back |
| Languages | any CMUSphinx model: English, French, German, Spanish, Russian... and, through the second engine, Arabic with its dialects and any phrase written as it sounds | about a hundred, of very uneven quality | the most, including Arabic dialects |
| Accuracy on free speech | well behind current models | close to current models | current models |

Pick this one when the player says one of a few dozen known things and the game
must react at once, on any Windows PC, without a network. Pick another when the player
may say anything.

## Where this comes from

This is a continuation of two people's work, and most of the plugin's shape is
theirs:

- **Shane Colbert** wrote the plugin for Unreal Engine 4, with its last update
  in 2016 ([shanecolb/sphinx-ue4](https://github.com/shanecolb/sphinx-ue4)).
- **Urszula Kustra** ported it to Unreal Engine 5 and has kept it building
  through UE 5.8 ([ukustra/sphinx-ue4](https://github.com/ukustra/sphinx-ue4)).

This repository starts from that port (commit `08e3957`, June 2026) and adds
what is listed below. The first nine commits after the import are bug fixes to
the port, one per commit, kept apart so that they can go back to it as a pull
request.

## What this repository adds

**Fixes.** A worker object leaked on every level change and a thread deleted
twice; game-thread writes to state the worker thread was reading; a volume
meter that tested the first sample of each buffer instead of the current one;
an editor crash when the DLLs were missing; a decoder and a recording device
leaked on every mode switch; a multi-word phrase check that only looked at the
first word; failures that were only ever written to the log; a microphone loop
that kept a core busy. The full list is in [CHANGELOG.md](CHANGELOG.md).

**Measured on this machine** (how, and on what audio: [docs/benchmark.md](docs/benchmark.md)):

| | Before | Now |
|---|---|---|
| CPU used by the process while listening, nobody speaking | about 1.8 cores | about 0.9 core, the same as when not listening |
| First phrase after start-up recognised, English | 33 of 49 | 45 of 49 |
| First phrase after start-up recognised, French | 7 of 48 | 27 of 48 |
| First phrase after start-up recognised, German | 3 of 15 | 10 of 15 |
| Phrases reported that were not said, English | 6 | 0 |
| Switching key phrases or grammar | 394 ms, the model was loaded again | 0.2 ms |

The CPU row compares the July build (the nine fixes and nothing else) with the
current one. The other rows compare this code with a feature switched off,
which is how the plugin behaved before, and with it on. The recognition rows
come from synthetic English and French voices and from recorded German: they
show the change, not what a player will get.

With early reports switched on, a command said on its own reaches the game
about 250 ms sooner (105 ms before the end of the speech instead of 152 ms
after it), and one said inside a sentence about 430 ms sooner.

**Features.**

- Any language without recompiling: a language is a folder of model files, and
  `Init With Language Code` takes its name. English, French and German models
  are one script away.
- A **Voice Command** component: commands with several wordings, an optional
  gameplay tag, a cooldown, one event. Components can be switched on and off to
  change what the player may say; the recogniser is not reloaded.
- State, error codes and an `On Recognition Failed` event with a message that
  says what to do.
- Start time, end time and score of every phrase.
- Optional early reports: the event fires when the phrase is spotted, not when
  the speaker pauses.
- A C++ decoder class without thread or microphone, for tools and tests.
- 14 automated tests, a benchmark, and a sample project.

## Status

| What | State |
|---|---|
| Builds as a standalone plugin (`RunUAT BuildPlugin`, Win64) | UE 5.5.4, 5.6.1 and 5.8.1: builds, no compiler warning |
| Automated tests (`Tools/Run-Tests.ps1`), UE 5.8 | 21 of 21 pass, 7 of them for the second engine. One plays a clip of each language through the live path while switching any, English, Tunisian, MSA, any, as the sample's L key does: each is recognised |
| Arabic (`ar`, `aeb`, `ary`...), Omnilingual ASR through NNE with phrase scoring | built. On 89 synthetic Darija command clips and 18 distractors, inside Unreal: phrase scoring 78 of 89 commands, 0 false matches, 0 false accepts, against 50 of 89 for matching the model's text (thresholds chosen on those clips). Model loaded in 1.6 to 3.2 s; about 0.4 s per command with 8 ONNX Runtime threads, 0.7 s with 4, 2.4 s with 1; phrase scoring adds about 8 ms. The C++ matcher and phrase spellings agree with the Python ones on 1,343 and 143 cases |
| ... with a person speaking | the author, Tunisian Darija, two live sessions in the sample: about 75% of commands worked with text matching, by the author's count, and "the Tunisian works" with phrase scoring. Not yet measured on recordings with known commands; MSA not yet spoken |
| Invented words (`any`) | five made-up spell phrases, written once in Latin letters, spoken by two synthetic voices (English, French): 10 of 10 recognised, 0 wrong, 0 of 8 sentences of chat fired a spell. Nobody has spoken them yet |
| Pre-built plugin (the release zip) in a Blueprint-only project, UE 5.8 | 2.1.1: loads without compiling; all 21 tests pass there, the second engine included (Tunisian, English, MSA and `any`). 5.5 and 5.6 zips built the same way, not opened |
| ... of which on the live microphone | start, switch mode, shut down, four times over: passes. The device is opened and read; nobody spoke into it |
| Benchmark on audio files | run; 116 synthetic utterances (English, French), 31 recorded (1 English, 30 German) |
| Sample project | builds and runs in the editor on UE 5.8; driven by the author's voice |
| A person speaking into a microphone | used by the author with their own voice, in their capstone game (UE 5.6) and in the sample (UE 5.8). Not measured: every accuracy figure comes from audio files |
| Packaged game build | the sample, packaged with `BuildCookRun` (UE 5.8, Win64, Development): the models, the DLLs and ONNX Runtime are staged; the game loads the English model and starts listening, and with `-SpeechLanguage=any` or `aeb` loads the second engine's model (3.5 s) and scores phrases. Shipping (2.0.0): packages and runs without a crash; it writes no log, so listening was not confirmed there; the second engine was not tried in Shipping |
| Mac, Linux, consoles | not supported: the bundled libraries are 64-bit Windows builds |
| Unreal Engine 5.4 and earlier | not tried |

## Quick start

Requirements: Windows, Unreal Engine 5.5 or later with Visual Studio 2022, a
microphone.

```
git clone https://github.com/KhalilBenGaied66/Sphinx-UE5.git C:\Dev\Sphinx-UE5
cd C:\Dev\Sphinx-UE5
powershell -ExecutionPolicy Bypass -File Tools\Get-Models.ps1
```

Clone into a short path such as `C:\Dev\Sphinx-UE5`: Unreal's build files go
several folders deep, and a long parent path breaks the build on Windows'
260-character path limit.

Open `SphinxSample.uproject` (it is set to UE 5.8; right-click it to switch
engine version), let it compile, press Play, and say "go forward", "turn left",
"jump", "stop". The screen shows the recogniser's state, the microphone level,
and the last command heard with its timing.

In Tunisian Darija; or in Modern Standard Arabic with `-SpeechLanguage=ar`; or
with invented spell words with `-SpeechLanguage=any` ("velaris", "zelthar
morun", "amorix valor corva aeterna", "silentium"):

```
powershell -ExecutionPolicy Bypass -File Tools\Get-Models.ps1 -Language en,omnilingual
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%CD%\SphinxSample.uproject" -game -windowed -ResX=1280 -ResY=720 -SpeechLanguage=aeb
```

Say "9oddem" (قدّام), "ltali" (لتالي) or "wakhar" (وخّر), "3al lisar" or "a
gauche", "3al limin" or "a droite", "onguez" (اُنڨز), "o9ef" (أُقف), "barka" or
"stop". In MSA: تقدّم، ارجع، يسارا، يمينا، اقفز، توقف. The screen shows what
the model wrote, the phrase that won, its score and the time it took.
In the running sample, **L** switches language: English, Tunisian, MSA, spells.
The first switch to one of the last three loads the 328 MB model, a pause of a
few seconds. When a phrase does not fire, the screen shows the best phrase,
its score and why it was not accepted; add the wording you said to
`CommandsFor()` in `Source/SphinxSample/VoiceControlledCube.cpp`.

To use the plugin in your own project:

1. Copy `Plugins/SpeechRecognition` into your project's `Plugins` folder. For
   a Blueprint-only project, take the pre-built zip for your engine version
   from the repository's Releases page instead: it needs no compiler.
2. Put models in your project's `Content/model`
   (`Tools\Get-Models.ps1 -Destination <YourProject>\Content\model`).
3. Add a **Voice Command** component to an actor, fill in its commands, bind
   **On Voice Command**.
4. Before packaging, stage the models: see [docs/models.md](docs/models.md).

More: [docs/usage.md](docs/usage.md) (modes, events, tolerance, limits) and
[docs/design.md](docs/design.md) (threads, start-up handling, why).

## Languages

| | Model | Checked here with |
|---|---|---|
| English | CMUSphinx `en-us-ptm-5.2` | synthetic speech, one recording |
| French | CMUSphinx `fr-ptm-5.2` | synthetic speech |
| German | VoxForge / Zamia `de-voxforge-5.2` | 30 recorded sentences from the corpus the model was trained on |
| Others | any CMUSphinx model in the same layout | not tried |
| Arabic: MSA (`ar`), Tunisian (`aeb`), Moroccan (`ary`), Egyptian (`arz`), Levantine (`apc`)... | none exists for CMUSphinx: Omnilingual ASR CTC 300M (Meta, Apache-2.0) through NNE, one model for every Arabic code; phrases written in Arabic script or Arabizi are scored against its output | synthetic Tunisian voices; two live sessions in Darija by the author. See [docs/models.md](docs/models.md#arabic-and-tunisian-darija) |
| Any phrase (`any`): invented words, names, mixed languages | the same model and scoring, with no language | five invented spells on two synthetic voices |

Accented and non-Latin words work: key phrases are lower-cased with Unicode
rules and looked up in the dictionary as UTF-8. French recognition is clearly
weaker than English on the test audio (35 of 48 commands against 48 of 49);
the numbers are in the benchmark.

## Reproducing the numbers

```
powershell -ExecutionPolicy Bypass -File Tools\Get-Models.ps1 -Language en,fr,de -LanguageModel
powershell -ExecutionPolicy Bypass -File Tools\New-TestAudio.ps1      # needs English and French Windows voices
powershell -ExecutionPolicy Bypass -File Tools\Get-TestSpeech.ps1     # German recordings; needs ffmpeg
powershell -ExecutionPolicy Bypass -File Tools\Run-Tests.ps1
powershell -ExecutionPolicy Bypass -File Tools\Run-Tests.ps1 -Filter SpeechBenchmark -NoBuild
powershell -ExecutionPolicy Bypass -File Tools\Measure-ListeningCost.ps1
powershell -ExecutionPolicy Bypass -File Tools\Build-Plugin.ps1 -Version 5.5,5.6,5.8
```

The benchmark writes `Saved/SpeechRecognition/benchmark.md`;
[docs/benchmark.md](docs/benchmark.md) is that file from the run described
there, with the measurements the other scripts print. Synthetic voices differ
between Windows versions, so your figures will not match to the unit.

## Limits

- Windows 64-bit only, default recording device only, 16 kHz models.
- The bundled PocketSphinx is a 2016 build. It reads files with the narrow C
  runtime (handled for model paths) and ends the process on some malformed
  model files (not something the plugin can catch).
- A single short word is an unreliable command. Use phrases of two words or
  more; see the tolerance tables in the benchmark.
- The accuracy figures come from synthetic and read speech in a quiet setting.
  A player in a room with a headset microphone will do worse; how much worse
  has not been measured.

## Roadmap

1. **PocketSphinx 5.1 and Unreal's own audio capture.** The current library is
   ten years old and brings its own Windows-only microphone code. Moving to the
   maintained release and to Unreal's audio capture gives a choice of input
   device, removes the process-ending failure modes, and opens the way to other
   platforms.
2. **A recorded test set.** A few speakers, a headset, a room: the accuracy
   figures that matter.
3. **Arabic, measured.** The engine is in: Omnilingual ASR through NNE with
   phrase scoring. Next: Darija and MSA commands
   recorded by a few speakers with `Tools/DarijaEval/record.py`, and every
   method scored on them with `score.py`, including the Tunisian Whisper
   fine-tunes (3 to 6 GB). If that is not good enough for a game, the next step
   is fine-tuning the model on those recordings, run the same way on NNE. See
   [docs/models.md](docs/models.md#arabic-and-tunisian-darija).
4. **A packaged-build check** in the test script (done by hand so far).

## Licence and credits

The plugin's source is in the public domain ([The Unlicense](License.md)), as
it has been since the first version. PocketSphinx and SphinxBase are
distributed under a BSD-style licence, and each speech model has its own:
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Original plugin: Shane Colbert. Unreal Engine 5 port: Urszula Kustra. Fixes,
tests and the additions above: Khalil Ben Gaied.
