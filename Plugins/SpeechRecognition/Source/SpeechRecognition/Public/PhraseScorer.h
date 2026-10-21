#pragma once

#include "CoreMinimal.h"

/**
 * Scores written phrases against a CTC speech model's output, the way PocketSphinx
 * spots key phrases, but on a modern model that writes characters.
 *
 * The model gives, for every 20 ms frame, a score for each character it knows
 * (and a blank). A phrase's score is the best path through those frames that
 * spells exactly the phrase (CTC Viterbi), compared with the model's own best
 * path over the whole utterance, in nats per character: 0 means the model wrote
 * that very phrase, -1 to -2 a letter or two off, below -3 something else. The
 * phrase must explain the whole utterance (silence before and after is free), so
 * a sentence that only contains the command scores low.
 *
 * Arabizi phrases are also scored in Arabic script through a rough
 * transliteration, so a command written once, in either script, is enough. Word
 * breaks are optional. Same rules as Tools/DarijaEval/phrase_scoring.py.
 */
struct SPEECHRECOGNITION_API FPhraseScorer
{
	/** The spellings of one phrase worth scoring, best guess first. */
	static TArray<FString> Variants(const FString& Phrase);

	/** "7ell el beb" -> "حل ال بب" (bWriteVowels false) or "حال ال باب"-like spellings (true). */
	static FString ArabiziToArabic(const FString& Phrase, bool bWriteVowels);

	/**
	 * Best path spelling Ids, in nats per character. Relative is Frames x Vocab, each
	 * value the frame's logit minus the frame's highest logit (so all <= 0). Blank is token 0.
	 */
	static float AlignScore(const float* Relative, int32 Frames, int32 Vocab, TConstArrayView<int32> Ids);
};
