# How it works

## Three layers

```
game thread                                   worker thread
-----------                                   -------------
UVoiceCommandComponent(s)
        |  phrases, per component
        v
USpeechRecognitionSubsystem   -- requests -->  FSpeechRecognitionWorker
  language, mode, phrases,        (queue)        the recording device
  parameters, state                              FSphinxDecoder
        ^                                          PocketSphinx
        |          <-- events --
        |             (queue)
  Tick: raises the Blueprint events
```

**`FSphinxDecoder`** wraps PocketSphinx: give it a model, a mode and what the
mode searches for, feed it 16-bit PCM, get utterances back. It has no thread
and no microphone. Tests and the benchmark drive it from audio files; the
worker drives it from the recording device. Nothing else in the plugin
includes a PocketSphinx header.

**`FSpeechRecognitionWorker`** is the thread. It owns the recording device and
one decoder, both on its own stack. It receives requests and sends events
through two queues, and that is all it shares with the game thread apart from
one atomic integer (the microphone level). It never touches a `UObject`.

**`USpeechRecognitionSubsystem`** is the Blueprint API and holds every piece
of state the user sets: language, mode, phrases, parameters. When a mode is
enabled it builds a complete decoder configuration and hands a copy to the
worker. In its `Tick` it empties the event queue and raises the Blueprint
events, so they arrive on the game thread at a known point of the frame, also
while the game is paused.

The original plugin had the game thread write the worker's fields directly,
and the worker copy the subsystem's delegates before posting them back. The
July fixes put a command queue between the two; this version removes the
shared fields altogether.

A request carries a generation number and every event carries the number of
the request that produced it. After a mode switch, events still in the queue
from the previous mode are dropped by number.

## What happens to the audio

The worker reads the recording device 1024 samples at a time (64 ms at
16 kHz). When the device has nothing new the thread sleeps 5 ms; the device
buffers several seconds, so nothing is lost. The original loop asked again at
once, at the highest thread priority, which kept a core busy whenever nobody
spoke.

PocketSphinx's voice detector cuts the stream into utterances. For each one
the decoder raises: speech started; then, when the speaker pauses, recognised
(with the phrases) or unrecognised; then speech ended.

## Start-up

A new decoder has things to learn from the audio before it hears well: how
loud the room is (its noise tracker), what silence looks like (its voice
detector), and the average spectrum of this voice on this microphone (a mean
it subtracts from every frame). Until it has learnt them, two things went
wrong in the original plugin:

1. The voice detector took the first half second of any stream for speech.
   The game got "started speaking", "unknown phrase", "stopped speaking" right
   after start-up, with nobody talking.
2. The first real utterance was decoded against statistics that were a guess,
   or, after that false start, the statistics of background noise. It was
   often missed. With a model that ships no starting mean (the French and
   German ones), it was nearly always missed.

The benchmark measures the second point by giving every utterance a brand new
decoder: 33 of 49 English commands were recognised, 7 of 48 French ones, 3 of
15 German ones.

What the decoder does now, while it is "settling":

- An utterance that begins in the stream's first 0.3 s is not announced. If
  nothing is recognised in it, no event is raised for it at all.
- Each utterance is kept in memory. When the speaker pauses, PocketSphinx
  updates its statistics with what it just heard; the utterance is then played
  through the decoder again, against those statistics, and the second answer
  is the one reported. If it is still empty the utterance is played once more.
- If nothing comes of it, the statistics are put back to their starting values
  so that noise leaves no trace.
- Settling ends with the first utterance in which something is recognised, or
  after six utterances.

The same measurement then gives 45 of 49, 27 of 48 and 10 of 15. A replay
costs a few milliseconds of CPU and no delay worth the name: decoding runs
more than a hundred times faster than real time.

What is known and what is inferred: the library logs the mean it subtracts,
and those logs show it jumping to the statistics of background noise after the
false start, and starting from a placeholder for the French and German models.
That the noise tracker also needs time is an inference from how the same
recording is processed differently on its first and later passes; it was not
traced inside the library. The replay is justified by the measurements, not by
a complete account of the library's internals.

It is a workaround for behaviour inside a ten-year-old library, not a cure.
The first phrase is still a little less reliable than later ones.

## Changing what is listened for

Loading a model takes 0.3 to 0.45 s and, as above, a new decoder hears badly
at first. So a decoder is only rebuilt when the model, the dictionary or a
PocketSphinx parameter changes. Another mode, another grammar or another list
of key phrases is a new *search* on the running decoder: 0.2 ms, the recording
device stays open, and what the decoder has learnt is kept.

This is what makes it reasonable to switch Voice Command components on and off
as the game goes from one situation to another.

## Overlapping phrases

Keyword spotting scores each phrase on its own, so one stretch of speech can
set off two phrases that start alike: say "lights off" and "lights on" is
often spotted too, ending a little earlier, with a lower score. Two phrases
cannot be said at once: when two detections share more than half of the
shorter one's time span, only the better-scored is reported. On the English
test audio this removes all six false reports and no true one; on the French
audio four of eight.

Early reports cannot do this. A phrase reported the moment it is spotted has
gone out before its rival is heard. That is the trade the option makes.

## Text

Dictionaries are UTF-8 and lower case. Key phrases are lower-cased with
Unicode rules before lookup (`FText::ToLower`; `FString::ToLower` only knows A
to Z), so capitals with accents or umlauts and non-Latin capitals match.

Dictionaries list alternative pronunciations as `word(2)`, `word(3)`. A phrase
may name one (`read(2) the book`); results have the suffix removed.

Paths are a separate matter: the 2016 library opens files with the narrow C
runtime. For a path with non-ASCII characters the plugin tries the system code
page, then UTF-8, then the short (8.3) name, and takes the first spelling the
runtime can actually open.

## Failures

`Init` and the mode functions return false for what can be known at once: the
DLLs did not load, a file is missing. What only the decoder or the device can
tell (a model it refuses, no usable key phrase, no microphone) arrives as
`On Recognition Failed`, with a code and a message. The worker thread stays
alive after a failure; enabling a mode again is a retry.

PocketSphinx writes its own diagnostics to stderr, where nobody sees them in
an Unreal process. They are routed to the plugin's log category, and the last
error line is quoted in the failure message.
