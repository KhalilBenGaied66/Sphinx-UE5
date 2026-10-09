# Measurements

What was measured, on what, and the command that measures it again.

Machine: Intel Core i7-14700F, 32 GB, Windows 11, Unreal Engine 5.8.1, a USB
microphone. Date: 9 October 2026. Models: the ones `Tools/Get-Models.ps1`
downloads.

## Read this first

- **Recognition figures come from audio files**, fed to the decoder the way
  the microphone thread feeds it. Nobody spoke into a microphone to produce any
  number here.
- **Most of the audio is synthetic.** The English and French utterances are
  spoken by Windows text-to-speech voices (Microsoft Zira, Microsoft Hortense).
  Synthetic speech is clean, evenly paced and free of room noise. The figures
  are a baseline for this code against regressions, and a fair way to compare
  two versions of it. They are not a forecast of how the recogniser does with
  players and their microphones.
- **The German audio is recorded, but not independent.** It is one speaker from
  the corpus the German model was trained on.
- Counts are small (49, 48 and 15 commands). A difference of one or two is
  noise.
- "As up to July 2026" means this code with a feature switched off, which
  reproduces what the plugin did before it. The CPU measurement is the
  exception: it runs the July build itself.

## What listening costs

`Tools/Measure-ListeningCost.ps1` starts a headless editor, leaves it idle for
15 s, then starts keyword recognition on the microphone and leaves it listening
for 30 s with nobody speaking. It reads the process's CPU time in both phases.
The script only uses the Blueprint API, so the same script measured the July
2026 build (the nine fixes, before anything else) and the current one. A
headless editor keeps most of a core busy by itself; the plugin's share is the
difference.

| Run | July build: idle | July build: listening | Current: idle | Current: listening |
|---|---|---|---|---|
| 1 | 1.17 cores | 1.81 cores | 0.90 cores | 0.93 cores |
| 2 | 0.84 cores | 1.78 cores | 0.85 cores | 0.89 cores |
| 3 | 0.96 cores | 1.85 cores | 0.62 cores | 0.90 cores |

Listening used 1.8 cores with the July build on every run, and 0.9 with the
current one, which is what the editor uses when idle. The idle figure itself
wanders from run to run (run 3 of the current build caught the editor in a
quiet moment), so the difference per run is not worth quoting to the percent.
The cause was a loop that read the recording device again at once whenever it
had nothing new, at the highest thread priority. It now sleeps 5 ms.

To measure the July build again: check out the tag `fixes-2026-07`, package
`SpeechRecognition/SpeechRecognition.uplugin` with `RunUAT BuildPlugin`, copy
the `ThirdParty` folder into the packaged plugin (at that commit BuildPlugin
leaves it out), put the result in the `Plugins` folder of an empty project
together with `Content/model/en`, and run
`Tools\Measure-ListeningCost.ps1 -Project <that project>.uproject`.

## Switching mode

From the `SpeechRecognition.Decoder.Reinitialise` test, English model, 40
switches between a list of key phrases and a grammar:

| | Per switch | Process memory after 40 |
|---|---|---|
| Decoder kept (same model and parameters) | 0.2 ms | +0.0 MB |
| Model loaded again (what every switch used to do) | 394 ms | -0.2 MB |

## Tests

`Tools/Run-Tests.ps1`, Unreal Engine 5.8.1: 14 of 14 pass.

| Test | What it checks |
|---|---|
| Unit.PhraseText | pronunciation suffixes, filler words, Unicode lower-casing, the phrase constructor |
| Unit.Volume | the loudness figure, including a buffer that starts below zero |
| Unit.VoiceCommandMapping | a phrase finds its command; one event per phrase that is ours |
| Decoder.MissingFiles | each missing file gives its own error, and nothing is left initialised |
| Decoder.KeywordValidation | phrases with an unknown word are rejected, wherever the word is |
| Decoder.RecordedSpeech | a person saying "go forward ten meters": keyword, grammar and language model modes, on one decoder |
| Decoder.Reinitialise | 40 mode switches on a kept decoder and 40 with a reload, under a memory watch |
| Decoder.StartUp | the first phrase after start-up is heard; no false start is reported; silence reports nothing |
| Decoder.NonAsciiPath | a model under a folder with accents in its name |
| Decoder.French | accented phrases typed in capitals; three French utterances |
| Decoder.German | umlauts and eszett; a phrase spotted in a recorded German sentence; German dictation (4 of 7 words) |
| Subsystem.ConfigParams | parameter validation, including a float equal to zero |
| Subsystem.FailsCleanly | a mode before Init, a language with no model: false at once, an error code, an event |
| Subsystem.Live | start, switch mode, shut down on the live microphone, four times |

`Subsystem.Live` opens the recording device and reads it. It does not check
that anything is recognised from it: there was nobody to speak.

Two things were checked by hand, once, on the same day:

- **A missing DLL.** With `pocketsphinx.dll` renamed, the editor starts, the
  log names the folder it looked in, the unit tests pass and `Init` returns
  false with a message. The original plugin stopped the editor at start-up.
- **The sample as a game.** Started without a window (`-game -nullrhi`), the
  sample loads its game mode, spawns the cube, and the recogniser reports that
  it is listening within a few seconds. Nobody spoke to it.

## Builds

`Tools/Build-Plugin.ps1 -Version 5.5,5.6,5.8` packages the plugin on its own
with `RunUAT BuildPlugin` (editor, game and shipping configurations, Win64,
Visual Studio 2022):

| Engine | Result |
|---|---|
| Unreal Engine 5.5.4 | built, no compiler warning |
| Unreal Engine 5.6.1 | built, no compiler warning |
| Unreal Engine 5.8.1 | built, no compiler warning |

Each packaged copy holds the plugin's binaries and the two PocketSphinx DLLs.
The tests and the benchmark were run on 5.8.1 only.

## Recognition

The rest of this page is the report written by
`Tools/Run-Tests.ps1 -Filter SpeechBenchmark`, as written apart from the level
of its headings.

Written by `Automation RunTests SpeechBenchmark` on 2026-10-09.

- Engine: 5.8.1
- CPU: Intel(R) Core(TM) i7-14700F (20 cores, 28 threads); decoding uses one thread
- Decoder parameters: the ones `Init` sets by default
- Audio is fed 1024 samples (64 ms) at a time, as the microphone thread does

**What the audio is.** 31 utterance(s) recorded by a person and 116 synthesised by Windows text-to-speech voices. Synthetic speech is clean, evenly paced and free of room noise, so the figures below are a regression baseline for this code, not a forecast of how the recogniser does with players and their microphones.

Times are audio time: how much sound had gone into the decoder when it answered. Decoding itself runs far faster than real time and adds next to nothing.

### Keyword spotting, `en`

12 key phrases are listened for at once (`Resources/TestAudio/commands.tsv`): single words at tolerance V8, longer phrases at V5.

Each phrase is said alone at three speaking rates and once inside a sentence, by a Windows text-to-speech voice; six sentences contain no phrase.

| | Reported when the speaker pauses (default) | Reported as soon as spotted |
|---|---|---|
| Commands spotted | 48 of 49 | 48 of 49 |
| Phrases spotted that were not said | 0 | 4 |
| Sentences without a command left alone | 6 of 6 | 6 of 6 |
| Command said on its own: end of speech to report (median) | 152 ms | -105 ms |
| Command inside a sentence: end of the sentence to report (median) | 109 ms | -323 ms |

A negative time means the phrase was reported while the speaker was still talking.

When two different phrases are spotted over the same stretch of speech, only the better-scored one is kept. Without that:

| | Commands spotted | Spotted but not said |
|---|---|---|
| Best of overlapping phrases (default) | 48 of 49 | 0 |
| Every phrase spotted | 48 of 49 | 6 |

The first thing a new decoder hears (a new decoder for every utterance; the figures above are from a decoder left running):

| | Speech 0.5 s after start-up | Speech 3 s after start-up |
|---|---|---|
| Decoded once, as up to July 2026 | 33 of 49 spotted, 0 not said | 30 of 49 spotted, 5 not said |
| With start-up handling (default) | 45 of 49 spotted, 1 not said | 47 of 49 spotted, 2 not said |

| Measure | Value |
|---|---|
| Model load (acoustic model + dictionary + key phrases) | 400 ms |
| Memory taken by a loaded decoder | about 26 MB |
| Decoding speed | 156x faster than real time (203.4 s of audio in 1.30 s) |

Missed (default reporting):

- en_stop_slow: said "stop", heard ""

The search beam (`-beam`): `Init` sets 1e-40 by default, PocketSphinx's own default is 1e-48, which keeps more hypotheses alive.

| Beam | Commands spotted | Spotted but not said | Decoding speed |
|---|---|---|---|
| 1e-40 | 48 of 49 | 0 | 148x real time |
| 1e-48 | 48 of 49 | 0 | 155x real time |
| 1e-60 | 48 of 49 | 0 | 152x real time |
| 1e-80 | 48 of 49 | 0 | 155x real time |

The same audio with every phrase at one tolerance (default reporting):

| Tolerance | Commands spotted | Spotted but not said | Sentences without a command left alone |
|---|---|---|---|
| V1 | 49 of 49 | 4 | 3 of 6 |
| V2 | 49 of 49 | 4 | 3 of 6 |
| V3 | 49 of 49 | 4 | 3 of 6 |
| V4 | 49 of 49 | 3 | 4 of 6 |
| V5 | 49 of 49 | 2 | 5 of 6 |
| V6 | 49 of 49 | 0 | 6 of 6 |
| V7 | 48 of 49 | 0 | 6 of 6 |
| V8 | 48 of 49 | 0 | 6 of 6 |
| V9 | 47 of 49 | 0 | 6 of 6 |
| V10 | 46 of 49 | 0 | 6 of 6 |

### Keyword spotting, `fr`

12 key phrases are listened for at once (`Resources/TestAudio/commands.tsv`): single words at tolerance V8, longer phrases at V5.

Each phrase is said alone at three speaking rates and once inside a sentence, by a Windows text-to-speech voice; six sentences contain no phrase.

| | Reported when the speaker pauses (default) | Reported as soon as spotted |
|---|---|---|
| Commands spotted | 35 of 48 | 35 of 48 |
| Phrases spotted that were not said | 4 | 7 |
| Sentences without a command left alone | 5 of 6 | 5 of 6 |
| Command said on its own: end of speech to report (median) | 170 ms | -87 ms |
| Command inside a sentence: end of the sentence to report (median) | 110 ms | -605 ms |

A negative time means the phrase was reported while the speaker was still talking.

When two different phrases are spotted over the same stretch of speech, only the better-scored one is kept. Without that:

| | Commands spotted | Spotted but not said |
|---|---|---|
| Best of overlapping phrases (default) | 35 of 48 | 4 |
| Every phrase spotted | 35 of 48 | 8 |

The first thing a new decoder hears (a new decoder for every utterance; the figures above are from a decoder left running):

| | Speech 0.5 s after start-up | Speech 3 s after start-up |
|---|---|---|
| Decoded once, as up to July 2026 | 7 of 48 spotted, 11 not said | 8 of 48 spotted, 20 not said |
| With start-up handling (default) | 27 of 48 spotted, 15 not said | 27 of 48 spotted, 18 not said |

| Measure | Value |
|---|---|
| Model load (acoustic model + dictionary + key phrases) | 316 ms |
| Memory taken by a loaded decoder | about 11 MB |
| Decoding speed | 139x faster than real time (212.0 s of audio in 1.53 s) |

Missed (default reporting):

- fr_avance_slow: said "avance", heard ""
- fr_avance_normal: said "avance", heard ""
- fr_avance_fast: said "avance", heard ""
- fr_saute_slow: said "saute", heard ""
- fr_saute_normal: said "saute", heard ""
- fr_saute_fast: said "saute", heard ""
- fr_saute_carrier: said "bon saute maintenant", heard "recule"
- fr_ouvre_la_porte_slow: said "ouvre la porte", heard ""
- fr_ferme_la_porte_slow: said "ferme la porte", heard ""
- fr_suis_moi_slow: said "suis moi", heard "à droite"
- fr_suis_moi_normal: said "suis moi", heard "à droite"
- fr_suis_moi_fast: said "suis moi", heard ""
- fr_suis_moi_carrier: said "bon suis moi maintenant", heard ""

Spotted but not said (default reporting):

- fr_saute_carrier: said "bon saute maintenant", heard "recule" at 82.41-82.65 s, score -104
- fr_suis_moi_slow: said "suis moi", heard "à droite" at 168.65-168.97 s, score -468
- fr_suis_moi_normal: said "suis moi", heard "à droite" at 172.50-172.76 s, score -623
- fr_negative_1: said "il fait beau aujourd'hui", heard "à gauche" at 199.85-200.02 s, score -525

The search beam (`-beam`): `Init` sets 1e-40 by default, PocketSphinx's own default is 1e-48, which keeps more hypotheses alive.

| Beam | Commands spotted | Spotted but not said | Decoding speed |
|---|---|---|---|
| 1e-40 | 35 of 48 | 4 | 141x real time |
| 1e-48 | 35 of 48 | 4 | 139x real time |
| 1e-60 | 35 of 48 | 4 | 141x real time |
| 1e-80 | 35 of 48 | 4 | 139x real time |

The same audio with every phrase at one tolerance (default reporting):

| Tolerance | Commands spotted | Spotted but not said | Sentences without a command left alone |
|---|---|---|---|
| V1 | 40 of 48 | 73 | 0 of 6 |
| V2 | 40 of 48 | 73 | 0 of 6 |
| V3 | 40 of 48 | 73 | 0 of 6 |
| V4 | 40 of 48 | 69 | 0 of 6 |
| V5 | 39 of 48 | 38 | 1 of 6 |
| V6 | 37 of 48 | 9 | 4 of 6 |
| V7 | 27 of 48 | 2 | 6 of 6 |
| V8 | 24 of 48 | 1 | 6 of 6 |
| V9 | 24 of 48 | 0 | 6 of 6 |
| V10 | 23 of 48 | 0 | 6 of 6 |

### Keyword spotting, `de`

15 key phrases are listened for at once (`Resources/TestAudio/commands.tsv`): single words at tolerance V8, longer phrases at V5.

The German audio is 30 sentences read by one speaker, from VoxForge. They belong to the corpus the German model was trained on, so this section shows that German works from end to end; it says little about how well it does with another voice. The key phrases are fragments of 15 of the sentences; the other 15 contain none.

| | Reported when the speaker pauses (default) | Reported as soon as spotted |
|---|---|---|
| Commands spotted | 9 of 15 | 9 of 15 |
| Phrases spotted that were not said | 0 | 0 |
| Sentences without a command left alone | 15 of 15 | 15 of 15 |
| Command said on its own: end of speech to report (median) | - | - |
| Command inside a sentence: end of the sentence to report (median) | 84 ms | -89 ms |

A negative time means the phrase was reported while the speaker was still talking.

When two different phrases are spotted over the same stretch of speech, only the better-scored one is kept. Without that:

| | Commands spotted | Spotted but not said |
|---|---|---|
| Best of overlapping phrases (default) | 9 of 15 | 0 |
| Every phrase spotted | 9 of 15 | 0 |

The first thing a new decoder hears (a new decoder for every utterance; the figures above are from a decoder left running):

| | Speech 0.5 s after start-up | Speech 3 s after start-up |
|---|---|---|
| Decoded once, as up to July 2026 | 3 of 15 spotted, 0 not said | 3 of 15 spotted, 0 not said |
| With start-up handling (default) | 10 of 15 spotted, 0 not said | 10 of 15 spotted, 1 not said |

| Measure | Value |
|---|---|
| Model load (acoustic model + dictionary + key phrases) | 445 ms |
| Memory taken by a loaded decoder | about 28 MB |
| Decoding speed | 140x faster than real time (220.2 s of audio in 1.57 s) |

Missed (default reporting):

- de_voxforge_34: said "mittlerweile ist das doppelt so teuer", heard ""
- de_voxforge_35: said "die geldmenge ist jährlich um neun prozent gewachsen", heard ""
- de_voxforge_48: said "es handelt sich um ein fass ohne boden", heard ""
- de_voxforge_76: said "über das thema wird im radio berichtet", heard ""
- de_voxforge_84: said "der geist ist inzwischen aus der flasche entwichen", heard ""
- de_voxforge_99: said "es ist auch unter anderen namen bekannt", heard ""

The search beam (`-beam`): `Init` sets 1e-40 by default, PocketSphinx's own default is 1e-48, which keeps more hypotheses alive.

| Beam | Commands spotted | Spotted but not said | Decoding speed |
|---|---|---|---|
| 1e-40 | 9 of 15 | 0 | 140x real time |
| 1e-48 | 9 of 15 | 1 | 137x real time |
| 1e-60 | 11 of 15 | 1 | 131x real time |
| 1e-80 | 11 of 15 | 3 | 120x real time |

The same audio with every phrase at one tolerance (default reporting):

| Tolerance | Commands spotted | Spotted but not said | Sentences without a command left alone |
|---|---|---|---|
| V1 | 9 of 15 | 2 | 13 of 15 |
| V2 | 9 of 15 | 2 | 13 of 15 |
| V3 | 9 of 15 | 2 | 13 of 15 |
| V4 | 9 of 15 | 1 | 14 of 15 |
| V5 | 9 of 15 | 0 | 15 of 15 |
| V6 | 9 of 15 | 0 | 15 of 15 |
| V7 | 9 of 15 | 0 | 15 of 15 |
| V8 | 9 of 15 | 0 | 15 of 15 |
| V9 | 9 of 15 | 0 | 15 of 15 |
| V10 | 9 of 15 | 0 | 15 of 15 |

### Grammar mode, `en`

Sentences of `Resources/TestAudio/grammars/en_move.gram`; a sentence counts when every word is right.

| When the result is read | Sentences right |
|---|---|
| After the utterance is closed (now) | 9 of 9 |
| Before the utterance is closed (as up to July 2026) | 9 of 9 |
