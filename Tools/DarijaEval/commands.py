"""The command list, and matching a free transcript back to one command.

A recogniser that writes text (Whisper) does not know the command list.
Its transcript is normalised and compared with every spelling of every wording:
Arabic script against Arabic script, Latin against the Arabizi and French
spellings, and, across scripts, both reduced to a consonant skeleton (9 = ق = q
= g, 7 = ح, ch = ش...), so that one spelling in either script is enough. The
best command wins if its score clears a threshold and is not tied with another
command. PocketSphinx listens for the phrases themselves, so its output maps to
a command directly.
"""

from __future__ import annotations

import re
import tomllib
import unicodedata
from dataclasses import dataclass, field
from pathlib import Path

from rapidfuzz import fuzz

HERE = Path(__file__).parent
DEFAULT_COMMANDS = HERE / "commands.toml"

# Default acceptance score (0-100) for a fuzzy match, and the margin the winner
# must hold over the best other command. Both are reported with every result.
THRESHOLD = 80.0
MARGIN = 5.0


@dataclass
class Wording:
    ar: str
    arabizi: list[str]
    loan: str = ""
    sphinx: list[str] = field(default_factory=list)

    @property
    def sphinx_phrase(self) -> str:
        return " ".join(line.split()[0] for line in self.sphinx)


@dataclass
class Command:
    id: str
    en: str
    wordings: list[Wording]

    @property
    def has_loan(self) -> bool:
        return any(w.loan for w in self.wordings)


@dataclass
class Distractor:
    id: str
    ar: str
    arabizi: str


@dataclass
class CommandList:
    commands: list[Command]
    distractors: list[Distractor]

    def by_id(self, cid: str) -> Command | None:
        return next((c for c in self.commands if c.id == cid), None)

    def is_distractor(self, cid: str) -> bool:
        return any(d.id == cid for d in self.distractors)


def load(path: Path = DEFAULT_COMMANDS) -> CommandList:
    data = tomllib.loads(Path(path).read_text(encoding="utf-8"))
    commands = [
        Command(
            id=c["id"],
            en=c["en"],
            wordings=[
                Wording(
                    ar=w["ar"],
                    arabizi=list(w["arabizi"]),
                    loan=w.get("loan", ""),
                    sphinx=list(w.get("sphinx", [])),
                )
                for w in c["wording"]
            ],
        )
        for c in data["command"]
    ]
    distractors = [Distractor(d["id"], d["ar"], d["arabizi"]) for d in data.get("distractor", [])]
    ids = [c.id for c in commands] + [d.id for d in distractors]
    if len(ids) != len(set(ids)):
        raise ValueError(f"duplicate ids in {path}")
    return CommandList(commands, distractors)


# --- normalisation ---------------------------------------------------------

_HARAKAT = re.compile("[ً-ٰٟـ]")  # short vowels, shadda, tatweel
_AR_MAP = str.maketrans(
    {
        "أ": "ا", "إ": "ا", "آ": "ا", "ٱ": "ا",
        "ى": "ي", "ئ": "ي", "ؤ": "و", "ة": "ه", "ء": "",
        # Tunisian /g/ is written ڨ, گ or ق depending on the writer
        "ڨ": "ق", "گ": "ق", "ڤ": "ف", "پ": "ب",
    }
)
_ARABIC = re.compile("[؀-ۿ]")
_NOT_WORD = re.compile(r"[^\w\s]", re.UNICODE)


def is_arabic(text: str) -> bool:
    return bool(_ARABIC.search(text))


def norm_ar(text: str) -> str:
    text = _HARAKAT.sub("", text).translate(_AR_MAP)
    text = _NOT_WORD.sub(" ", text)
    text = re.sub(r"[^؀-ۿ\s]", " ", text)
    text = re.sub(r"(.)\1+", r"\1", text)  # doubled letters stand for a shadda
    return " ".join(text.split())


def norm_latin(text: str) -> str:
    """Lowercase Latin skeleton shared by Arabizi and French spellings."""
    text = unicodedata.normalize("NFKD", text.lower())
    text = "".join(ch for ch in text if not unicodedata.combining(ch))
    text = re.sub(r"[^a-z0-9\s]", " ", text)
    for a, b in (("5", "kh"), ("7", "h"), ("9", "k"), ("q", "k"), ("3", "a"), ("2", ""),
                 ("dh", "d"), ("th", "t"), ("gu", "g"), ("ou", "u"), ("o", "u"),
                 ("e", "a"), ("i", "y"), ("y", "i"), ("ch", "sh"), ("c", "k"), ("w", "u")):
        text = text.replace(a, b)
    text = re.sub(r"[^a-z\s]", " ", text)
    text = re.sub(r"(.)\1+", r"\1", text)
    return " ".join(text.split())


# --- across scripts: a consonant skeleton ----------------------------------
#
# Arabic script leaves short vowels out and Arabizi writes them in, so the two
# meet on consonants. Letters that writers swap share one symbol (ق ڨ ك q g k c
# -> k; ح ه 7 h -> h; خ غ 5 kh gh -> x; ش ch sh -> c; ث ت ط t th -> t;
# د ذ ض ظ d dh -> d; س ص s -> s). Vowels, ا و ي ى ة, ع and hamza are dropped:
# they are the letters Arabizi and Arabic script disagree on most. Letters of
# other scripts (Omnilingual sometimes slips into Devanagari or Hangul) are
# dropped too.

_SKELETON_AR = {
    "ب": "b", "پ": "b", "ت": "t", "ث": "t", "ط": "t", "ج": "j", "ح": "h", "ه": "h",
    "خ": "x", "غ": "x", "د": "d", "ذ": "d", "ض": "d", "ظ": "d", "ر": "r", "ز": "z",
    "س": "s", "ص": "s", "ش": "c", "ف": "f", "ڤ": "f", "ق": "k", "ڨ": "k", "گ": "k",
    "ك": "k", "ل": "l", "م": "m", "ن": "n",
}
_SKELETON_LATIN_PAIRS = (("kh", "x"), ("gh", "x"), ("ch", "C"), ("sh", "C"), ("th", "t"),
                         ("dh", "d"), ("5", "x"), ("8", "x"), ("7", "h"), ("9", "k"))
_SKELETON_LATIN = {"b": "b", "p": "b", "t": "t", "j": "j", "h": "h", "x": "x", "d": "d",
                   "r": "r", "z": "z", "s": "s", "C": "c", "f": "f", "v": "f", "k": "k",
                   "q": "k", "g": "k", "c": "k", "l": "l", "m": "m", "n": "n"}


def skeleton_word(word: str) -> str:
    """One word, in any script, as consonant classes."""
    out = []
    latin = unicodedata.normalize("NFKD", word.lower())
    latin = "".join(ch for ch in latin if not unicodedata.combining(ch))
    for a, b in _SKELETON_LATIN_PAIRS:
        latin = latin.replace(a, b)
    for ch in latin:
        if ch in _SKELETON_AR:
            out.append(_SKELETON_AR[ch])
        elif ch in _SKELETON_LATIN:
            out.append(_SKELETON_LATIN[ch])
    return re.sub(r"(.)\1+", r"\1", "".join(out))


def skeleton_words(text: str) -> list[str]:
    words = re.split(r"[\s\u060c\u061f.,!?;:]+", text)
    return [w for w in (skeleton_word(x) for x in words) if w]


def skeleton(text: str) -> str:
    """The whole text as one skeleton: word breaks do not count ("3al lisar" = "عليسار")."""
    return re.sub(r"(.)\1+", r"\1", "".join(skeleton_words(text)))


# --- matching --------------------------------------------------------------


@dataclass
class _Hit:
    score: float
    script: str  # "ar", "lat" or "sk": which normalised transcript the span refers to
    start: int  # matched token span [start, end)
    end: int
    length: int  # letters in the candidate, to prefer the longer of two equal matches


def _window_score(candidate: str, transcript: str, script: str) -> _Hit:
    """Best similarity of `candidate` to any run of about the same number of words."""
    cand_tokens = candidate.split()
    tokens = transcript.split()
    best = _Hit(0.0, script, 0, 0, 0)
    if not cand_tokens or not tokens:
        return best
    n = len(cand_tokens)
    letters = len(candidate.replace(" ", ""))
    for size in sorted({max(1, n - 1), n, n + 1}):
        for i in range(0, max(1, len(tokens) - size + 1)):
            window = " ".join(tokens[i : i + size])
            # very short phrases ("la", "ih") must match exactly: a fuzzy score on
            # two letters fires on almost anything
            if letters <= 3:
                score = 100.0 if window == candidate else 0.0
            else:
                score = fuzz.ratio(candidate, window)
            if (score, letters) > (best.score, best.length):
                best = _Hit(score, script, i, min(i + size, len(tokens)), letters)
    return best


def _skeleton_score(candidate: str, words: list[str]) -> _Hit:
    """Best similarity of a phrase's skeleton to a run of transcript words, joined."""
    best = _Hit(0.0, "sk", 0, 0, 0)
    letters = len(candidate)
    # three consonants or fewer say too little to be matched loosely across scripts
    if letters < 3 or not words:
        return best
    for i in range(len(words)):
        joined = ""
        for j in range(i, min(len(words), i + 4)):
            joined = re.sub(r"(.)\1+", r"\1", joined + words[j])
            if letters == 3:
                score = 100.0 if joined == candidate else 0.0
            elif letters == 4:
                # one consonant off in four is too loose: allow only exact or one extra
                score = fuzz.ratio(candidate, joined) if abs(len(joined) - letters) <= 1 else 0.0
                score = score if score >= 88.0 else 0.0
            else:
                score = fuzz.ratio(candidate, joined)
            if (score, letters) > (best.score, best.length):
                best = _Hit(score, "sk", i, j + 1, letters)
            if len(joined) > letters + 2:
                break
    return best


def _candidates(cmd: Command) -> tuple[list[str], list[str]]:
    ar, lat = [], []
    for w in cmd.wordings:
        ar.append(norm_ar(w.ar))
        lat.extend(norm_latin(s) for s in w.arabizi)
    return [c for c in ar if c], [c for c in lat if c]


@dataclass
class Match:
    command: str | None  # None: no command accepted
    score: float
    runner_up: str | None
    runner_up_score: float


# Off reproduces the matcher before cross-script matching, for comparisons.
CROSS_SCRIPT = True


def match(transcript: str, commands: CommandList,
          threshold: float = THRESHOLD, margin: float = MARGIN,
          cross_script: bool | None = None) -> Match:
    cross = CROSS_SCRIPT if cross_script is None else cross_script
    t_ar = norm_ar(transcript)
    t_lat = norm_latin(re.sub("[\u0600-\u06ff]", " ", transcript))
    t_sk = skeleton_words(transcript) if cross else []
    hits: list[tuple[_Hit, str]] = []
    for cmd in commands.commands:
        ar, lat = _candidates(cmd)
        found = [_window_score(c, t_ar, "ar") for c in ar] if t_ar else []
        found += [_window_score(c, t_lat, "lat") for c in lat] if t_lat else []
        if cross:
            phrases = [w.ar for w in cmd.wordings] + [a for w in cmd.wordings for a in w.arabizi]
            found += [_skeleton_score(skeleton(ph), t_sk) for ph in phrases]
        best = max(found, key=lambda h: (h.score, h.length), default=_Hit(0.0, "", 0, 0, 0))
        hits.append((best, cmd.id))
    hits.sort(key=lambda x: (x[0].score, x[0].length), reverse=True)
    top, top_id = hits[0]

    # A shorter command heard inside the winner's words is not a rival:
    # "3la lisar" contains "la" (no), but the speaker said "left".
    def nested(h: _Hit) -> bool:
        return (h.script == top.script and top.start <= h.start and h.end <= top.end
                and h.length < top.length)

    rival, rival_id = next(((h, c) for h, c in hits[1:] if not nested(h)), hits[1])
    accepted = top.score >= threshold and top.score - rival.score >= margin
    return Match(top_id if accepted else None, top.score, rival_id, rival.score)
