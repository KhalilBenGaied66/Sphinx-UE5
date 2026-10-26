# Changelog

Starting point: [ukustra/sphinx-ue4](https://github.com/ukustra/sphinx-ue4) at
commit `08e3957` ("5.8 update"). The Unreal Engine 5 port itself is that
repository's work and is not listed here.

## 2.1.1 (October 2026)

### Removed

- **The Gemini engine.** A game needs a recogniser that works offline and
  without a daily quota, and the free tier allowed 20 requests a day. Both
  engines left run on the CPU, inside the game. `-SpeechTranscriber` and the
  `Transcription Engine` and `Gemini Model` settings are gone; a project that
  still sets them in its config gets the local model, as before by default.
- The plugin no longer depends on the HTTP and Json modules.
- `Tools/DarijaEval` no longer has a `gemini` engine or the `google-genai`
  package.

## 2.1.0 (October 2026)

### Added

- **A second engine: any phrase you can write.** For what PocketSphinx has no
  model for, Meta's Omnilingual ASR (CTC 300M, int8 ONNX, Apache-2.0) runs
  offline inside Unreal on the Neural Network Engine (NNERuntimeORT, CPU), on a
  background task, with its own speech detector. Language code `any` (invented
  words, names, mixed languages) or an Arabic code: `ar` (Modern Standard
  Arabic), `aeb` (Tunisian), `ary`, `arz`, `apc`... All share one model folder,
  `Content/model/omnilingual`, which `Tools/Get-Models.ps1 -Language
  omnilingual` downloads (328 MB).
- **Phrase scoring** (`FPhraseScorer`). Each phrase is aligned with the model's
  per-frame character scores (CTC Viterbi over the whole utterance), so a
  command is recognised whatever the model would have written for it. Arabizi
  phrases are also scored in Arabic script; word breaks are optional. On 89
  synthetic Darija command clips inside Unreal: 78 recognised, 0 wrong, 0 of 18
  distractors fired, against 50 for matching the model's text.
- **Text matching across scripts** (`FTranscriptMatcher`), for engines that only
  write text: Arabic and Latin spellings normalised and compared, and reduced
  to a consonant skeleton across scripts.
- **Voice Command component:** `Transcript Matching` (phrase scoring or text),
  `Phrase Score Threshold` and `Margin`, `Transcript Match Threshold` and
  `Margin`. `FRecognisedPhraseDetail` gains `bIsTranscript`, `MatchedPhrase`,
  `PhraseScore`, `PhraseScores` and `ProcessingSeconds`.
- **`Set Input File`** (and `SpeechRecognition.InputFile`): a WAV file in place
  of the microphone, played in real time, for tests and for replaying
  recordings through the live path.
- **A log line per language change and per utterance**: which language, which
  engine, whether the model was loaded or reused, what was heard, and the best
  and next phrase with their scores. The sample shows a phrase that came close
  but did not fire, and why.
- **A Gemini engine for development** (`-SpeechTranscriber=gemini`, key from the
  `GEMINI_API_KEY` environment variable, never shipped).
- **Sample:** `-SpeechLanguage=aeb` (Tunisian Darija), `ar` (MSA), `any`
  (spells). The screen shows the model's text, the winning phrase and its score.
- **`Tools/DarijaEval`:** the Darija command list, a recording tool, and a
  script that scores every engine on the same clips.
- **Tests:** seven more (phrase scorer, matcher parity with the Python version
  on 1,454 cases, speech detector events, the model on 115 clips, the live
  subsystem switching Tunisian, English, MSA and `any`, and the same switch with
  a clip of each language played through the worker).

### Changed

- The plugin depends on the NNERuntimeORT plugin (part of the engine), and on
  the NNE, HTTP and Json modules.

## 2.0.0 (October 2026)

### Fixed

- **A microphone thread that kept a core busy.** The loop read the recording
  device without pausing; when the device had nothing new it asked again at
  once, at the highest thread priority. It now sleeps 5 ms on an empty read and
  runs at normal priority. Measured in `docs/benchmark.md`.
- **Recognition failures that nobody heard about.** A wrong model path or a
  missing microphone only wrote a log line, `Init` still returned true, and the
  dead thread was "reused" by the next `Init`. There is now a state
  (`Get Speech Recognition State`), an error code and message, and an
  `On Recognition Failed` event. Missing files are reported at the call, with
  `false`; what only the decoder can tell arrives as the event. The thread
  survives a failure and a new mode request retries.
- **Events raised from a copy of the delegate taken on the worker thread.** The
  worker copied `On Words Spoken` while the game thread could be binding to it.
  The worker now only fills a queue; the subsystem empties it in its `Tick` and
  raises every event on the game thread.
- **`FRecognitionPhrase(keyword)` threw the keyword away** and built an empty
  phrase.
- **`Set Config Param` refused any float equal to zero**, taking the parsed
  value 0 for a parse failure.
- **Parameters were lost after the first mode switch.** They were consumed by
  the first initialisation; the second one ran on defaults. They now stay set
  until `Shutdown` or `Clear Config Params`.
- **A parameter written with the wrong type corrupted the configuration.** The
  library's setters do not check: an integer given as a float was read back as
  garbage. Values are now written according to the type PocketSphinx declares,
  and unknown parameter names are refused with a warning.
- **Key phrases with capital accented letters never matched.** Phrases were
  lower-cased byte by byte. They are now lower-cased with Unicode rules, so
  "ARRÊTE" and "ÖFFNE" match the dictionary's "arrête" and "öffne".
- **Models under a folder with accents in its name could not be loaded.** The
  bundled library opens files with the narrow C runtime; the path is now handed
  over in an encoding that runtime can open, or refused with a clear message.
- **The DLL lookup walked every plugin folder** and loaded the first file with
  the right name, with the 32-bit DLLs sitting next to the 64-bit ones. The DLLs
  are now loaded from this plugin's own folder.
- **`using namespace std;` in a public header**, along with the PocketSphinx
  headers, an unused Chaos include and `Engine.h`. Public headers now include
  nothing from the third-party libraries.
- **`RunUAT BuildPlugin` produced a plugin without PocketSphinx.** The
  ThirdParty folder was not packaged. `Config/FilterPlugin.ini` now lists it.
- The Mac entry in the plugin descriptor and the Mac branch of the build rules
  referred to libraries that are not in the repository. Both are gone, with the
  unused 32-bit Windows libraries: the plugin is Win64 only, and says so.

### Added

- **Any language, without recompiling.** `Init With Language Code` takes the
  name of a model folder; `Get Installed Languages` lists what is there. The
  model folder is a project setting. German is added to the language list, and
  `Tools/Get-Models.ps1` fetches English, French and German models.
- **Start-up handling.** A new decoder used to report a false start of speech
  in the stream's first moments and then miss the first real phrase. Each
  utterance is now replayed through the decoder until one is recognised, and
  the false start is not reported. Figures in `docs/benchmark.md`.
- **Mode switches that keep the decoder.** Changing mode or key phrases on the
  same model no longer reloads it: a fraction of a millisecond instead of some
  400 ms, the microphone stays open, and what the decoder has learnt about the
  room and the voice is kept.
- **Best of overlapping key phrases.** When two phrases are spotted over the
  same stretch of speech ("lights on" inside "lights off"), only the
  better-scored one is reported. Can be turned off.
- **Early key phrase reports** (`Set Report Key Phrases Early`): a phrase is
  reported when it is spotted instead of when the speaker pauses.
- **`On Phrases Recognised`**: each phrase with its start time, end time and
  score. `On Listening Started` when the microphone is open.
- **`UVoiceCommandComponent`**: commands with several wordings, an optional
  gameplay tag, a cooldown, one event. Several components share the recogniser
  and each hears its own commands.
- **`FSphinxDecoder`**: the decoder as a plain C++ class, without thread or
  microphone, for tools and tests.
- **Tests and a benchmark** that decode audio files, including live start,
  mode switch and shutdown cycles on the microphone
  (`Tools/Run-Tests.ps1`).
- **A sample project** at the root of the repository: a cube that obeys spoken
  commands, set up in C++ so that it needs no asset.
- PocketSphinx's own error messages now reach the Unreal log, and the failure
  message quotes the last one.
- A hint in the editor log when the model folder is not staged for packaged
  builds, with the line to add.

### Changed

- The repository is now a sample project with the plugin under
  `Plugins/SpeechRecognition`. Copy that folder into your own project.
- Keyword results no longer delete every digit from a phrase: only
  pronunciation suffixes such as `(2)` are removed.
- An utterance in which nothing usable was recognised raises
  `On Unknown Phrase`; it used to raise `On Words Spoken` with an empty list
  in grammar mode.
- Recognised phrases and "several pronunciations" notices are logged at
  Verbose instead of Log.
- `SpeechRecognitionWorker.h` moved from Public to Private.
- The hypothesis is read after the utterance is closed, as the PocketSphinx
  API intends. `SpeechRecognition.LegacyHypothesisOrder 1` restores the old
  order. On the test audio both give the same results.

## July 2026 fixes

Nine fixes on top of the UE5 port, one commit each, described with a manual
check for each in [FIXES.md](Plugins/SpeechRecognition/FIXES.md): the worker
that was never freed and the thread deleted twice; shared state written by the
game thread while the worker read it; the volume loop that tested the first
sample of the buffer instead of the current one; the editor crash when the DLLs
are missing; leaks in keyword setup and configuration parameters; the decoder
and the recording device leaked on every mode switch; the multi-word phrase
check that only looked at the first word; a pointer kept into a temporary
conversion buffer; a bitwise OR and ANSI file names.
