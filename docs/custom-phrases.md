# Writing your own phrases

You want the player to say "Remala konto biris" and something to happen. Is it
spelled "remala" or "remeyla"? Do you write it the way it should sound?

**Write it once, in plain letters, the way you would spell it for a friend:
`remala konto biris`.** Do not respell it to describe the pronunciation. The
measurements below show the plain spelling firing whichever way the phrase was
said, and an English-style respelling ("rehmahlah kontoh beeriss") scoring
worse, not better.

This page is about the second engine: language code `any` (invented words,
names, mixed languages) and the Arabic codes (`ar`, `aeb`...). English, French
and German with PocketSphinx work differently: see the last section.

## Adding a phrase

On a **Voice Command** component (Blueprint or C++):

1. **Language Code**: `any`.
2. **Commands**: add one. **Command** is the name your logic switches on
   (`Barrier`), **Phrases** what the player says (`remala konto biris`).
3. **On Voice Command** → **Switch on Name** on **Command** → your logic.

In the sample project, the same lists are in *Project Settings > Game > Sphinx
Sample*: add the command under **Spells (any)**, press Play, press L until the
screen says `any`. A name the cube has no action for makes it spin once.

## How the plugin hears a phrase

The speech model does not look your phrase up anywhere: there is no
dictionary. It hears letters. For each utterance it gives, for every moment of
the audio, how likely each letter is. The plugin then lines your phrase up,
letter by letter, against those likelihoods and gives it a score: the average
log-likelihood per letter, 0 when the model would have written exactly your
phrase, lower the further it is. A command fires when its best phrase scores
**-2.5 or better** and leads every other command by **0.5**.

So a phrase does not need to match what the model writes. It only needs its
letters to be the likely ones. The model wrote "remala contoberus",
"remer la konto beris" and "raimolicontobiris" for three ways of saying the
phrase, and `remala konto biris` scored between -0.18 and -0.29 on all three.

## What was measured

"remala konto biris", said six ways by the two Windows voices, and three lines
of chat. Each clip scored against six spellings. Cut-off -2.5; a score near 0
is a clear hit.

| How it was said | Model wrote | `remala konto biris` | `remeyla konto biris` | `remaila konto biris` | `re ma la kon to bi ris` | `rehmahlah kontoh beeriss` | Best other spell |
|---|---|---|---|---|---|---|---|
| plain (English voice) | remala contoberus | **-0.18** | -0.73 | -0.59 | -0.18 | -1.90 | -4.95 |
| "reh-MAH-lah, kon-toh, bee-riss" | remer la konto beris | **-0.21** | -0.23 | -0.31 | -0.38 | -1.14 | -4.45 |
| "re-MAY-la" | remaila contoberus | **-0.31** | -0.64 | -0.27 | -0.31 | -2.18 | -4.87 |
| French accent | remala kontobiri | **-0.30** | -0.90 | -0.63 | -0.39 | -1.79 | -5.00 |
| French accent, "re-MEI-la" | remela contobiri | **-0.50** | -0.55 | -0.69 | -0.68 | -2.06 | -5.21 |
| run together, fast | raimolicontobiris | **-0.29** | -0.63 | -0.60 | -0.29 | -1.96 | -4.86 |
| chat: "remember the castle" | remember the castle | -9.20 | -8.77 | -9.23 | -8.52 | -9.01 | -8.96 |
| chat: "what time is it" | what time is it | -8.79 | -9.04 | -9.09 | -8.81 | -9.48 | -9.90 |
| chat: "bonjour à tous" | bonjour à tous | -8.74 | -8.79 | -8.82 | -8.11 | -9.30 | -8.48 |

What it shows:

- **The plain spelling fires every time**, even for "re-MAY-la", which it does
  not describe (-0.31). A spelling that describes the sound better gains
  little: `remaila` -0.27 against -0.31.
- **English-style respelling is worse on every clip** (-1.14 to -2.18). "h",
  "ee", "ss" are letters the model does not write for those sounds, and every
  letter that is not heard costs.
- **Capitals and spaces do not matter.** `REMALA KONTO BIRIS` scored the same
  as `remala konto biris`; the phrase said run together scored -0.29 against
  the spaced spelling.
- **Chat stays far below** (-8 to -10), "remember", which starts the same way,
  included.
- The phrase leads the next spell by more than 4 points, far above the 0.5
  needed.

Synthetic voices are cleaner than players: expect real voices to score a point
or two lower. Reproduce with `Tools/DarijaEval/spelling_check.py`.

## The rules

1. **Spell it plainly, once.** As you would write the word in a book or a
   message. Lower case or capitals, with or without spaces between words:
   both are the same to the plugin.
2. **Do not respell for pronunciation.** No "h" after vowels, no doubled
   vowels or consonants to show stress or length, no hyphens.
3. **Read vowels the way most languages do** if you are unsure: a as in
   "father", e as in "bed", i as in "machine", o as in "go", u as in "rule".
   The model writes letters the way Spanish, Italian or Arabizi would, not the
   way English spelling does. Only when the word really sounds different from
   that, write the vowel you hear ("ei" for "ay").
4. **Leave out punctuation and symbols.** Letters the model does not know are
   ignored. Accents are dropped (`é` counts as `e`). Digits are read as Arabizi
   (`3`, `7`, `9`...), so avoid them in spells.
5. **Add a second spelling only when people say it differently**, not just
   with another accent: a different consonant, a syllable more or less, a
   short form players will use ("remala" alone). A command can have as many
   phrases as you like; the best one counts.
6. **The player says the phrase on its own.** The whole utterance is scored,
   so words around the phrase lower its score. One synthetic voice: the
   phrase "ارجع" ("go back") scored -1.22 when the clip was that word alone,
   and -5.25 when the clip was "ارجع لتالي" ("go back behind"), which the
   phrase "ارجع لتالي" scored -0.06. If players will say it inside a sentence
   ("remala konto biris, now"), write that sentence as a phrase of its own.
7. **Arabic and Darija**: Arabic script, Latin letters (Arabizi) or both. See
   [models.md](models.md#arabic-and-tunisian-darija).

## Choosing phrases that work

- **Two or three words are safer than one short word.** A phrase of a few
  syllables is told apart from chat easily; a one-syllable command ("ka") is
  close to many things people say.
- **Make commands sound different from each other.** Two spells one letter
  apart ("velaris", "valeris") will score close to each other; the 0.5 lead is
  there so that neither fires rather than the wrong one.
- **Avoid everyday words of the players' language** unless they are meant to
  fire whenever said alone: a player who says "stop" to a friend stops the
  game's "stop" too.

## When a phrase does not fire

Every utterance writes two lines to the log (and the sample shows them on
screen):

```
SpeechRecognitionPlugin: Heard "remaila contoberus" (2.0 s of audio, transcribed in 780 ms)
SpeechRecognitionPlugin: Phrase scores: best Barrier (-0.31, "remala konto biris"), next Jump (-4.87)
```

| What the log shows | What it means | What to do |
|---|---|---|
| Best is your command, score between -2.5 and about -4 | Close, but this player says it differently | Add what the "Heard" line wrote as a second phrase, or set **Phrase Score Threshold** to -3 |
| Best is your command, above -2.5, not fired | Another command scores within 0.5 | Make the two commands sound more different |
| Best is your command, below -6 | It heard something else | Read the "Heard" line: a different phrase, or noise |
| No "Heard" line | The speech detector heard nothing | The microphone level is too low, or nobody spoke |
| It fires on chat | The phrase is short or common | A longer phrase, or **Phrase Score Threshold** -2 |

To try a phrase without speaking every time, record it once and play the file
through the live path: `Set Input File` on the subsystem, or the console
variable `SpeechRecognition.InputFile` ([usage.md](usage.md)).

## English, French and German (PocketSphinx)

With `en`, `fr` or `de` the plugin uses PocketSphinx, which works from a
pronunciation dictionary: every word of a phrase must be in it, spelled
normally. A word that is not ("remala") is skipped, and the log and the sample
list it as "not in the dictionary". For invented words, use `any`; for real
words, write them as the dictionary does, and use **Tolerance** (not the
phrase score) to make a phrase easier or harder to trigger
([usage.md](usage.md#tolerance)).
