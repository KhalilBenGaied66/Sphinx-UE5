"""Phrase scoring: how well each written phrase fits a CTC model's output.

Omnilingual ASR writes one character per 20 ms frame, or a blank. Instead of
reading its best guess and matching that text to the command list, each
phrase is aligned with the model's per-frame probabilities: the best path that
spells exactly that phrase (CTC Viterbi). Its score is how much less likely
that path is than the model's own best path over the same frames, in nats per
character: 0 means the model wrote that very phrase, -1 to -2 a letter or two
off, below -3 something else. This is PocketSphinx's keyword spotting on a
modern acoustic model: any phrase can be scored, in any script the model
writes, and nothing depends on how the model happens to spell.

Two ways to place the phrase:
  whole     the phrase explains the whole utterance (blanks before and after
            are free): extra words cost, so chat is rejected
  anywhere  the phrase may sit anywhere in the utterance, the rest is free:
            a command inside a sentence is found, and so are false alarms

Arabizi phrases are also scored in Arabic script, through a rough
transliteration (two variants: with and without the vowels written), so a
developer may write a command in either script only.
"""

from __future__ import annotations

import re
import unicodedata

import numpy as np

_HARAKAT = re.compile("[ً-ٰٟـ]")
_ARABIC = re.compile("[؀-ۿ]")

# Arabizi to Arabic script, longest spelling first
_DIGRAPHS = (("ch", "ش"), ("sh", "ش"), ("kh", "خ"), ("gh", "غ"), ("th", "ت"), ("dh", "ض"),
             ("ou", "و"), ("oo", "و"), ("ee", "ي"), ("aa", "ا"))
_LETTERS = {"3": "ع", "7": "ح", "9": "ق", "5": "خ", "2": "ء", "8": "غ",
            "b": "ب", "p": "ب", "t": "ت", "j": "ج", "d": "د", "r": "ر", "z": "ز", "s": "س",
            "f": "ف", "v": "ف", "k": "ك", "q": "ق", "g": "ق", "c": "ك", "l": "ل", "m": "م",
            "n": "ن", "h": "ه", "w": "و", "y": "ي", "x": "كس"}


def is_arabic(text: str) -> bool:
    return bool(_ARABIC.search(text))


def _fold_latin(text: str) -> str:
    text = unicodedata.normalize("NFKD", text.lower())
    return "".join(ch for ch in text if not unicodedata.combining(ch))


def arabizi_to_arabic(phrase: str, write_vowels: bool) -> str:
    words = []
    for word in _fold_latin(phrase).split():
        if word in ("el", "al", "l"):
            words.append("ال")
            continue
        out, i = [], 0
        while i < len(word):
            pair = word[i:i + 2]
            hit = next((ar for lat, ar in _DIGRAPHS if pair == lat), None)
            if hit:
                out.append(hit)
                i += 2
                continue
            ch = word[i]
            if ch in "aeiou":
                first, last = i == 0, i == len(word) - 1
                if first:
                    out.append("ا")
                elif ch == "i":
                    out.append("ي")
                elif ch in "ou":
                    out.append("و")
                elif ch == "a" and (write_vowels or last):
                    out.append("ا")
            elif ch in _LETTERS:
                out.append(_LETTERS[ch])
            i += 1
        words.append(re.sub(r"(.)\1+", r"\1", "".join(out)))
    return " ".join(w for w in words if w)


def variants(phrase: str) -> list[str]:
    """The spellings of one phrase worth scoring."""
    out = []
    if is_arabic(phrase):
        base = " ".join(_HARAKAT.sub("", phrase).split())
        out.append(base)
        out.append(base.translate(str.maketrans({"أ": "ا", "إ": "ا", "آ": "ا", "ى": "ي", "ڨ": "ق", "گ": "ق"})))
    else:
        latin = " ".join(_fold_latin(phrase).split())
        out.append(re.sub(r"[0-9]", "", latin))
        out.append(latin.replace("9", "q").replace("7", "h").replace("5", "kh").replace("3", "a").replace("2", ""))
        out.append(arabizi_to_arabic(phrase, write_vowels=True))
        out.append(arabizi_to_arabic(phrase, write_vowels=False))
    # word breaks are optional: "3al lisar" is often said, and written, as one word
    out += [v.replace(" ", "") for v in out if " " in v]
    seen, unique = set(), []
    for v in out:
        if v and v not in seen:
            seen.add(v)
            unique.append(v)
    return unique


def encode(text: str, vocab: dict[str, int]) -> list[int]:
    return [vocab[ch] for ch in text if ch in vocab]


def log_softmax(logits: np.ndarray) -> np.ndarray:
    m = logits.max(axis=1, keepdims=True)
    z = logits - m
    return z - np.log(np.exp(z).sum(axis=1, keepdims=True))


def align_score(relative: np.ndarray, ids: list[int], anywhere: bool) -> float:
    """Best CTC path spelling `ids`, in nats per character relative to the model's own best path.

    relative[t, k] = log p(k | frame t) - max_k log p(k | frame t), so every value is <= 0.
    """
    if not ids:
        return -np.inf
    blank = 0
    ext = [blank]
    for i in ids:
        ext += [i, blank]
    ext = np.array(ext)
    S = len(ext)
    T = relative.shape[0]
    # skipping the blank between two tokens is allowed unless they are the same token
    can_skip = np.zeros(S, dtype=bool)
    can_skip[2:] = (ext[2:] != blank) & (ext[2:] != ext[:-2])
    neg = -np.inf
    D = np.full(S, neg)
    best = neg
    for t in range(T):
        emit = relative[t, ext]
        prev = D
        stay = prev
        step = np.concatenate(([neg], prev[:-1]))
        skip = np.where(can_skip, np.concatenate(([neg, neg], prev[:-2])), neg)
        cand = np.maximum(np.maximum(stay, step), skip)
        if anywhere or t == 0:
            # start here: the frames before are not part of the phrase
            cand[0] = max(cand[0], 0.0)
            cand[1] = max(cand[1], 0.0)
        D = emit + cand
        if anywhere:
            best = max(best, D[-1], D[-2])
    if not anywhere:
        best = max(D[-1], D[-2])
    return float(best) / len(ids)


def score_phrases(logits: np.ndarray, phrases_by_command: dict[str, list[str]],
                  vocab: dict[str, int], anywhere: bool) -> list[tuple[float, str, str]]:
    """(score, command, the spelling that scored best), best first."""
    relative = log_softmax(logits.astype(np.float64))
    relative = relative - relative.max(axis=1, keepdims=True)
    out = []
    for command, phrases in phrases_by_command.items():
        best = (-np.inf, command, "")
        for phrase in phrases:
            for v in variants(phrase):
                s = align_score(relative, encode(v, vocab), anywhere)
                if s > best[0]:
                    best = (s, command, v)
        out.append(best)
    out.sort(reverse=True)
    return out
