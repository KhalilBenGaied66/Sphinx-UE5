#pragma once

#include "CoreMinimal.h"

/**
 * Finds which command a free transcript stands for.
 *
 * A transcribing engine does not know the command list: it writes what it heard,
 * in Arabic script, in Latin letters, or both, with whatever spelling it likes.
 * The transcript and every phrase are normalised (Arabic harakat, hamza forms and
 * ڨ/ق unified; Arabizi digits mapped to letters; doubled letters collapsed), then
 * each phrase is compared with every run of about as many words in the transcript.
 * Arabic phrases are compared with the Arabic part of the transcript, Latin ones
 * with the Latin part, and, across scripts, both are reduced to a consonant
 * skeleton (9 = ق = q = g, 7 = ح, ch = ش...): a phrase written once, in either
 * script, matches a transcript in the other.
 *
 * The best command wins if it scores at least Threshold (0-100) and leads every
 * other command by Margin. A shorter command heard inside the winner's words is
 * not a rival ("3la lisar" contains "la"). Phrases of three letters or fewer must
 * match exactly. Same rules as Tools/DarijaEval/commands.py, which the
 * measurements use.
 */
struct SPEECHRECOGNITION_API FTranscriptMatcher
{
	static constexpr float DefaultThreshold = 80.f;
	static constexpr float DefaultMargin = 5.f;

	struct FResult
	{
		/** Index into the groups given to Match, or INDEX_NONE when no command is accepted. */
		int32 Group = INDEX_NONE;
		/** The phrase of that group that matched best. */
		FString Phrase;
		float Score = 0.f;
		int32 RunnerUp = INDEX_NONE;
		float RunnerUpScore = 0.f;
	};

	/** Each group is one command: all the ways of saying it, in any script. */
	static FResult Match(const FString& Transcript, const TArray<TArray<FString>>& Groups,
		float Threshold = DefaultThreshold, float Margin = DefaultMargin, bool bCrossScript = true);

	/** Consonant classes of a text in any script, word breaks ignored: "3al lisar" and "عليسار" both give "lsr". */
	static FString Skeleton(const FString& Text);
	static TArray<FString> SkeletonWords(const FString& Text);

	static FString NormaliseArabic(const FString& Text);
	static FString NormaliseLatin(const FString& Text);
	static bool ContainsArabic(const FString& Text);

	/** rapidfuzz's ratio: 100 * 2 * LCS / (|A| + |B|). */
	static float Similarity(const FString& A, const FString& B);
};
