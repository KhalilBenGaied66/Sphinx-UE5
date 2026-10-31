# Using the plugin

Everything goes through one object, the **Speech Recognition Subsystem** of the
world, or through the **Voice Command** component that drives it for you.

## The short way: a Voice Command component

1. Add a **Voice Command** component to an actor.
2. Fill **Commands**. Each has a name, one or more phrases that trigger it, a
   tolerance, and optionally a gameplay tag.
3. Bind **On Voice Command**. It hands you the command's name, its tag, and the
   phrase that was heard with its timing and score.

The component starts the recogniser if nothing has (with its **Language Code**),
adds its phrases to those being listened for, and removes them when it is
deactivated or destroyed. Several components can be active at once; each one
only reports its own commands. Activate and deactivate them to change what the
player can say in a given context: the recogniser is not reloaded, so the switch
is immediate.

In C++:

```cpp
VoiceCommands = CreateDefaultSubobject<UVoiceCommandComponent>(TEXT("VoiceCommands"));

FVoiceCommand OpenDoor;
OpenDoor.Command = TEXT("OpenDoor");
OpenDoor.Phrases = { TEXT("open the door"), TEXT("open door") };
OpenDoor.Tolerance = EPhraseRecognitionTolerance::VE_5;
VoiceCommands->Commands.Add(OpenDoor);

VoiceCommands->OnVoiceCommand.AddDynamic(this, &AMyActor::HandleVoiceCommand);
```

`Source/SphinxSample/VoiceControlledCube.cpp` is a complete example. Its
commands are in Project Settings (*Game > Sphinx Sample*,
`Source/SphinxSample/SphinxSampleSettings.h`), where they can be changed
without rebuilding.

### Any phrase: Arabic, Tunisian Darija, invented words

Set **Language Code** to `any`, or to an Arabic code: `ar` (Modern Standard
Arabic), `aeb` (Tunisian), `ary` (Moroccan), `arz` (Egyptian), `apc`
(Levantine)... Then write each command's phrases, plainly, in Arabic
script, Latin letters or both. There is no dictionary, so any word can be a
command, an invented one included ([custom-phrases.md](custom-phrases.md):
how to spell one, and what to do when it does not fire):

```cpp
VoiceCommands->LanguageCode = TEXT("aeb");
OpenDoor.Phrases = { TEXT("7ell el beb") };            // once, in Arabizi
OpenDoor.Phrases = { TEXT("حلّ الباب"), TEXT("7ell el beb") }; // or in both scripts

VoiceCommands->LanguageCode = TEXT("ar");
OpenDoor.Phrases = { TEXT("افتح الباب") };            // Modern Standard Arabic

VoiceCommands->LanguageCode = TEXT("any");
Shield.Phrases = { TEXT("amorix valor") };            // a spell: no language at all
```

In Blueprint: on the Voice Command component, set **Language Code** to `any`,
add a command to **Commands** (Command `Patronus`, Phrases `expecto patronum`),
and in **On Voice Command**, *Switch on Name* on the command to run its logic.
Nothing else changes between languages.

`any` and all Arabic codes share one model (`Tools\Get-Models.ps1 -Language
omnilingual`).
Each phrase is scored against what the model heard (**Transcript Matching** =
Phrase Scoring, the default). The command whose best phrase scores at least
**Phrase Score Threshold** (-2.5 nats per character; 0 means the model wrote
exactly the phrase) and leads the next command by **Phrase Score Margin** (0.5)
fires. **On Voice Command** then hands over the model's text as the phrase,
**Matched Phrase** (the phrase that won), **Phrase Score**, every phrase's
score in **Phrase Scores**, and **Processing Seconds**. With Transcript
Matching set to Text, the text is compared with the phrases
instead (**Transcript Match Threshold** 80, **Transcript Match Margin** 5).
**Tolerance** is not used. Only keyword mode works for these languages: there
is no grammar to search. The model, the engines and their speed are in
[models.md](models.md#arabic-and-tunisian-darija).

**When a phrase does not fire.** Every utterance writes two lines to the log:

```
SpeechRecognitionPlugin: Heard "imchi limen" (1.2 s of audio, transcribed in 450 ms)
SpeechRecognitionPlugin: Phrase scores: best Right (-1.10, "imchi limin"), next Forward (-3.80)
```

and every change of language one more (`Listening in 'aeb' (...; model already
loaded), 6 phrase(s), from the microphone`). A best phrase above -2.5 that did
not fire lost on the margin; one well below it means the player said a wording
you have not written: add it to the command's phrases, as it sounds. The sample
shows the same on screen. No "Heard" line at all means the speech detector
heard nothing: the microphone level is too low, or nobody spoke.

**Replaying audio through the live path.** `Set Input File` on the subsystem
(or the console variable `SpeechRecognition.InputFile`) plays a 16 kHz 16-bit
mono WAV in place of the microphone, in real time, then silence. Everything
else is the same: speech detector, engine, language switches, components. It is
how `SpeechRecognition.Subsystem.LanguageSwitchWithAudio` tests the sample's
language switch, and a way to replay a player's recording.

### Coming from On Words Spoken

Projects built on the original plugin usually wire it like this: the words in
the game state's **Optional World List**, then in a character, **Bind Event to
On Words Spoken**, a **For Each Loop** over **Text Phrases**, and a **Switch on
String** with one pin per word. It works for English, but:

- the words are written twice, in the list and on the Switch pins, and a typo
  in either fails silently;
- one way of saying each thing, unless the list and the Switch both grow;
- one list for the whole game;
- with the second engine (`any`, Arabic codes) it never fires: On Words Spoken
  hands over the model's text ("expecto patronam"), which no Switch pin equals.
  Deciding whether that text is close enough to a phrase is done by the Voice
  Command component.

The same graph with the component:

1. On the character, add a **Voice Command** component. Set its **Language
   Code** (`en`; `any` for invented words).
2. In **Commands**, one entry per Switch pin: **Command** is the pin's name,
   **Phrases** every way of saying it. Two pins that mean the same thing
   ("Sorry", "Apologize") become one command with two phrases.
3. Bind **On Voice Command**, **Switch on Name** on **Command**, and connect the
   pins to what the old Switch drove (Play Anim Montage...).
4. Empty the game state's Optional World List, or set the game mode's game state
   back to the default. The component starts the recogniser itself, with its
   Language Code, only if nothing has started it yet: a game state that still
   starts it may set the language first.

Bind Event, For Each Loop and Switch on String go. Single short words ("lucy",
"better") fire on ordinary speech: a phrase of two or three words, or a
stricter **Tolerance**, keeps them for when they are meant.

## The subsystem

Get it with **Get World Subsystem > Speech Recognition Subsystem**, then:

1. **Init** (a language from the list) or **Init With Language Code** (the name
   of any model folder). Returns false if the DLLs could not be loaded.
2. Enable one mode:
   - **Enable Keyword Mode** with a list of phrases and their tolerances;
   - **Enable Grammar Mode** with the name of a JSGF file in the language's
     `grammars` folder;
   - **Enable Language Model Mode** with the name of an n-gram model in its
     `language_models` folder.

   Each returns false at once when a file is missing. Calling another one later
   switches mode. On the same language the model is not loaded again: a new
   list of key phrases or a grammar takes well under a millisecond.
3. Bind the events.

| Event | When |
|---|---|
| On Listening Started | The model is loaded and the microphone is open. |
| On Recognition Failed | A mode could not start, or the microphone failed. Carries an error code and a message. |
| On Started Speaking | The voice detector heard the start of speech. |
| On Words Spoken | The utterance ended and something was recognised: the list of phrases. |
| On Phrases Recognised | Same moment, with each phrase's start time, end time and score. |
| On Unknown Phrase | The utterance ended and nothing was recognised. |
| On Stopped Speaking | Always follows one of the two above. |

All of them are raised on the game thread.

**Get Speech Recognition State** tells where things are: Stopped, Initialised
(no mode yet), Starting, Listening, Failed. **Get Last Error Message** says why
it failed, in words a user can act on. **Get Rejected Phrases** lists the key
phrases that were left out because a word is not in the dictionary.

**Shutdown** stops everything; it also happens when the world ends.

## Which mode

| | Keyword | Grammar | Language model |
|---|---|---|---|
| You give it | a list of phrases | a JSGF grammar | an n-gram model |
| It reports | the phrases it spots, ignoring the rest | the sentence, word by word | whatever was said, word by word |
| Other speech | ignored | forced into a sentence of the grammar | transcribed |
| Good for | commands shouted in the middle of anything | structured orders: "go back five meters" | free text, roughly |

Keyword mode is the one to start with for a game.

## Tolerance

Each key phrase has a tolerance from V1 to V10. V1 triggers most easily and
V10 is the strictest. Too loose and a phrase fires on speech that only
resembles it; too strict and it is missed.

- Longer phrases are easier to tell apart: three or four syllables at least,
  and V5 to V7.
- A single short word ("stop", "jump") needs V8 or stricter, and will still be
  missed more often. Prefer "stop now" to "stop".
- Phrases that start alike ("lights on", "lights off") get spotted together.
  The plugin keeps the better-scored of the two, which is usually the right
  one; phrases that differ early are safer still.

`docs/benchmark.md` shows what each setting does on the test audio. Use
`Tools/Run-Tests.ps1 -Filter SpeechBenchmark` with your own phrases and
recordings to choose yours.

## Options

- **Set Report Key Phrases Early** (keyword mode, off by default). A phrase is
  reported the moment it is spotted instead of when the speaker pauses: a few
  hundred milliseconds sooner, more so when the command is followed by other
  words. The price: the recogniser cannot yet know what comes next, so phrases
  that start alike can both be reported.
- **Set Keep Best Of Overlapping Key Phrases** (on by default). See above.
- **Set Config Param** passes a PocketSphinx parameter through, for instance
  `-vad_postspeech` (hundredths of a second of silence that end an utterance;
  `Init` sets 10) or `-vad_threshold`. It applies the next time a mode is
  enabled, and changing one reloads the model.
- Console variable `SpeechRecognition.StartUpReplays` (default 2): how many
  times an utterance may be replayed through a decoder that has just started.
  0 turns the start-up handling off.
- Console command `Log SpeechRecognitionPlugin Verbose` shows each recognised
  phrase and PocketSphinx's own log.

## Grammars

A JSGF file, for instance `Content/model/en/grammars/move.gram`:

```
#JSGF V1.0;
grammar move;
public <command> = <move> | <turn> | <door>;
<move> = go ( forward | back ) [ <number> meters ];
<turn> = turn ( left | right );
<door> = ( open | close ) the door;
<number> = one | two | three | four | five | six | seven | eight | nine | ten;
```

`Enable Grammar Mode "move"` then reports "go", "back", "five", "meters" as four
entries of one utterance. Every word must be in the dictionary.

## Limits to know about

- **Windows, 64-bit only.** The bundled libraries are Windows builds.
- **The default recording device only.** The bundled audio layer has no way to
  choose another one.
- **16 kHz models.** The microphone is opened at the rate the model asks for.
- **One recogniser per world**, shared by everything that uses the subsystem.
- **No dedicated servers**: the subsystem is not created there.
- **A model that is damaged can end the process.** The 2016 library calls
  `exit` on some malformed files. The plugin checks that the files exist before
  handing them over; it cannot check that they are sound.
- **Accuracy is that of a 2010s recogniser.** It is quick and small, and good at
  a short list of well-chosen phrases. It is not a transcription engine.
