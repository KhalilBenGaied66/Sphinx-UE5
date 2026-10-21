#include "TranscriptMatcher.h"

namespace
{
	bool IsArabicBlock(TCHAR C)
	{
		return C >= 0x0600 && C <= 0x06FF;
	}

	/** Letters and digits of the Arabic block: what Python's \w keeps there. */
	bool IsArabicWordChar(TCHAR C)
	{
		return (C >= 0x0620 && C <= 0x064A)
			|| (C >= 0x0660 && C <= 0x0669)
			|| (C >= 0x066E && C <= 0x06D3)
			|| C == 0x06D5
			|| (C >= 0x06EE && C <= 0x06FF);
	}

	bool IsSpace(TCHAR C)
	{
		return C == TEXT(' ') || C == TEXT('\t') || C == TEXT('\n') || C == TEXT('\r') || C == 0x00A0;
	}

	FString CollapseRepeats(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (int32 i = 0; i < In.Len(); ++i)
		{
			if (i == 0 || In[i] != In[i - 1])
			{
				Out.AppendChar(In[i]);
			}
		}
		return Out;
	}

	FString CollapseSpaces(const FString& In)
	{
		TArray<FString> Words;
		In.ParseIntoArrayWS(Words);
		return FString::Join(Words, TEXT(" "));
	}

	/** Lower case without accents, for the Latin letters French and Arabizi use. */
	TCHAR FoldLatin(TCHAR C)
	{
		if (C >= TEXT('A') && C <= TEXT('Z'))
		{
			return C + (TEXT('a') - TEXT('A'));
		}
		switch (C)
		{
		case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5:
		case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5:
			return TEXT('a');
		case 0x00E7: case 0x00C7:
			return TEXT('c');
		case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB:
		case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB:
			return TEXT('e');
		case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF:
		case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF:
			return TEXT('i');
		case 0x00F1: case 0x00D1:
			return TEXT('n');
		case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6:
		case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6:
			return TEXT('o');
		case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC:
		case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC:
			return TEXT('u');
		case 0x00FF: case 0x00FD: case 0x00DD:
			return TEXT('y');
		default:
			return C;
		}
	}

	struct FHit
	{
		float Score = 0.f;
		int32 Script = -1; // 0 Arabic, 1 Latin
		int32 Start = 0;
		int32 End = 0;
		int32 Length = 0;
		FString Phrase;
	};

	bool Better(float ScoreA, int32 LengthA, float ScoreB, int32 LengthB)
	{
		return ScoreA > ScoreB || (ScoreA == ScoreB && LengthA > LengthB);
	}

	// --- across scripts: consonant classes, as in commands.py ---------------

	TCHAR SkeletonArabic(TCHAR C)
	{
		switch (C)
		{
		case 0x0628: case 0x067E: return TEXT('b');                         // ب پ
		case 0x062A: case 0x062B: case 0x0637: return TEXT('t');            // ت ث ط
		case 0x062C: return TEXT('j');                                      // ج
		case 0x062D: case 0x0647: return TEXT('h');                         // ح ه
		case 0x062E: case 0x063A: return TEXT('x');                         // خ غ
		case 0x062F: case 0x0630: case 0x0636: case 0x0638: return TEXT('d'); // د ذ ض ظ
		case 0x0631: return TEXT('r');                                      // ر
		case 0x0632: return TEXT('z');                                      // ز
		case 0x0633: case 0x0635: return TEXT('s');                         // س ص
		case 0x0634: return TEXT('c');                                      // ش
		case 0x0641: case 0x06A4: return TEXT('f');                         // ف ڤ
		case 0x0642: case 0x06A8: case 0x06AF: case 0x0643: return TEXT('k'); // ق ڨ گ ك
		case 0x0644: return TEXT('l');                                      // ل
		case 0x0645: return TEXT('m');                                      // م
		case 0x0646: return TEXT('n');                                      // ن
		default: return 0;
		}
	}

	TCHAR SkeletonLatin(TCHAR C)
	{
		switch (C)
		{
		case TEXT('b'): case TEXT('p'): return TEXT('b');
		case TEXT('t'): return TEXT('t');
		case TEXT('j'): return TEXT('j');
		case TEXT('h'): return TEXT('h');
		case TEXT('x'): return TEXT('x');
		case TEXT('d'): return TEXT('d');
		case TEXT('r'): return TEXT('r');
		case TEXT('z'): return TEXT('z');
		case TEXT('s'): return TEXT('s');
		case TEXT('C'): return TEXT('c'); // ch, sh
		case TEXT('f'): case TEXT('v'): return TEXT('f');
		case TEXT('k'): case TEXT('q'): case TEXT('g'): case TEXT('c'): return TEXT('k');
		case TEXT('l'): return TEXT('l');
		case TEXT('m'): return TEXT('m');
		case TEXT('n'): return TEXT('n');
		default: return 0;
		}
	}

	FString SkeletonWord(const FString& Word)
	{
		FString Folded;
		Folded.Reserve(Word.Len());
		for (const TCHAR C : Word)
		{
			Folded.AppendChar(FoldLatin(C));
		}
		static const TCHAR* const Pairs[][2] = {
			{ TEXT("kh"), TEXT("x") }, { TEXT("gh"), TEXT("x") }, { TEXT("ch"), TEXT("C") }, { TEXT("sh"), TEXT("C") },
			{ TEXT("th"), TEXT("t") }, { TEXT("dh"), TEXT("d") }, { TEXT("5"), TEXT("x") }, { TEXT("8"), TEXT("x") },
			{ TEXT("7"), TEXT("h") }, { TEXT("9"), TEXT("k") },
		};
		for (const auto& Pair : Pairs)
		{
			Folded.ReplaceInline(Pair[0], Pair[1], ESearchCase::CaseSensitive);
		}
		FString Out;
		for (const TCHAR C : Folded)
		{
			const TCHAR A = SkeletonArabic(C);
			const TCHAR L = A != 0 ? A : SkeletonLatin(C);
			if (L != 0)
			{
				Out.AppendChar(L);
			}
		}
		return CollapseRepeats(Out);
	}

	bool IsSkeletonBreak(TCHAR C)
	{
		return IsSpace(C) || C == 0x060C || C == 0x061F || C == TEXT('.') || C == TEXT(',') || C == TEXT('!')
			|| C == TEXT('?') || C == TEXT(';') || C == TEXT(':');
	}

	FHit WindowScore(const FString& Candidate, const TArray<FString>& Tokens, int32 Script)
	{
		FHit Best;
		Best.Script = Script;
		TArray<FString> CandidateTokens;
		Candidate.ParseIntoArray(CandidateTokens, TEXT(" "));
		if (CandidateTokens.Num() == 0 || Tokens.Num() == 0)
		{
			return Best;
		}
		const int32 N = CandidateTokens.Num();
		const int32 Letters = Candidate.Replace(TEXT(" "), TEXT("")).Len();

		TArray<int32> Sizes = { FMath::Max(1, N - 1), N, N + 1 };
		Sizes.Sort();
		int32 Previous = -1;
		for (const int32 Size : Sizes)
		{
			if (Size == Previous)
			{
				continue;
			}
			Previous = Size;
			const int32 Windows = FMath::Max(1, Tokens.Num() - Size + 1);
			for (int32 i = 0; i < Windows; ++i)
			{
				FString Window;
				const int32 Last = FMath::Min(i + Size, Tokens.Num());
				for (int32 t = i; t < Last; ++t)
				{
					if (t > i)
					{
						Window.AppendChar(TEXT(' '));
					}
					Window += Tokens[t];
				}
				// very short phrases ("la", "ih") must match exactly: a fuzzy score on
				// two letters fires on almost anything
				const float Score = Letters <= 3
					? (Window == Candidate ? 100.f : 0.f)
					: FTranscriptMatcher::Similarity(Candidate, Window);
				if (Better(Score, Letters, Best.Score, Best.Length))
				{
					Best.Score = Score;
					Best.Start = i;
					Best.End = Last;
					Best.Length = Letters;
				}
			}
		}
		return Best;
	}
}

namespace
{
	/** Best similarity of a phrase's skeleton to a run of transcript words, joined. */
	FHit SkeletonScore(const FString& Candidate, const TArray<FString>& Words)
	{
		FHit Best;
		Best.Script = 2;
		const int32 Letters = Candidate.Len();
		// three consonants or fewer say too little to be matched loosely across scripts
		if (Letters < 3 || Words.Num() == 0)
		{
			return Best;
		}
		for (int32 i = 0; i < Words.Num(); ++i)
		{
			FString Joined;
			for (int32 j = i; j < FMath::Min(Words.Num(), i + 4); ++j)
			{
				Joined = CollapseRepeats(Joined + Words[j]);
				float Score = 0.f;
				if (Letters == 3)
				{
					Score = Joined == Candidate ? 100.f : 0.f;
				}
				else if (Letters == 4)
				{
					// one consonant off in four is too loose: allow only exact or one extra
					Score = FMath::Abs(Joined.Len() - Letters) <= 1 ? FTranscriptMatcher::Similarity(Candidate, Joined) : 0.f;
					Score = Score >= 88.f ? Score : 0.f;
				}
				else
				{
					Score = FTranscriptMatcher::Similarity(Candidate, Joined);
				}
				if (Better(Score, Letters, Best.Score, Best.Length))
				{
					Best.Score = Score;
					Best.Start = i;
					Best.End = j + 1;
					Best.Length = Letters;
				}
				if (Joined.Len() > Letters + 2)
				{
					break;
				}
			}
		}
		return Best;
	}
}

TArray<FString> FTranscriptMatcher::SkeletonWords(const FString& Text)
{
	TArray<FString> Words;
	FString Current;
	auto Flush = [&]()
	{
		if (!Current.IsEmpty())
		{
			const FString Word = SkeletonWord(Current);
			if (!Word.IsEmpty())
			{
				Words.Add(Word);
			}
			Current.Reset();
		}
	};
	for (const TCHAR C : Text)
	{
		if (IsSkeletonBreak(C))
		{
			Flush();
		}
		else
		{
			Current.AppendChar(C);
		}
	}
	Flush();
	return Words;
}

FString FTranscriptMatcher::Skeleton(const FString& Text)
{
	return CollapseRepeats(FString::Join(SkeletonWords(Text), TEXT("")));
}

bool FTranscriptMatcher::ContainsArabic(const FString& Text)
{
	for (const TCHAR C : Text)
	{
		if (IsArabicBlock(C))
		{
			return true;
		}
	}
	return false;
}

FString FTranscriptMatcher::NormaliseArabic(const FString& Text)
{
	FString Out;
	Out.Reserve(Text.Len());
	for (TCHAR C : Text)
	{
		// short vowels, shadda, sukun, dagger alef, tatweel
		if ((C >= 0x064B && C <= 0x065F) || C == 0x0670 || C == 0x0640)
		{
			continue;
		}
		switch (C)
		{
		case 0x0623: case 0x0625: case 0x0622: case 0x0671: C = 0x0627; break; // alef forms
		case 0x0649: case 0x0626: C = 0x064A; break;                            // alef maqsura, yeh with hamza
		case 0x0624: C = 0x0648; break;                                         // waw with hamza
		case 0x0629: C = 0x0647; break;                                         // teh marbuta
		case 0x0621: continue;                                                  // lone hamza
		case 0x06A8: case 0x06AF: C = 0x0642; break;                            // Tunisian /g/: ڨ or گ for ق
		case 0x06A4: C = 0x0641; break;                                         // ڤ
		case 0x067E: C = 0x0628; break;                                         // پ
		default: break;
		}
		Out.AppendChar(IsArabicWordChar(C) ? C : TEXT(' '));
	}
	// doubled letters stand for a shadda
	return CollapseSpaces(CollapseRepeats(Out));
}

FString FTranscriptMatcher::NormaliseLatin(const FString& Text)
{
	FString Folded;
	Folded.Reserve(Text.Len());
	for (const TCHAR C : Text)
	{
		const TCHAR F = FoldLatin(C);
		const bool bKeep = (F >= TEXT('a') && F <= TEXT('z')) || (F >= TEXT('0') && F <= TEXT('9'));
		Folded.AppendChar(bKeep ? F : TEXT(' '));
	}

	// Arabizi digits to letters, then a coarse skeleton of the vowels and the
	// letter pairs that writers swap. Order matters, as in commands.py.
	static const TCHAR* const Replacements[][2] = {
		{ TEXT("5"), TEXT("kh") }, { TEXT("7"), TEXT("h") }, { TEXT("9"), TEXT("k") }, { TEXT("q"), TEXT("k") },
		{ TEXT("3"), TEXT("a") }, { TEXT("2"), TEXT("") }, { TEXT("dh"), TEXT("d") }, { TEXT("th"), TEXT("t") },
		{ TEXT("gu"), TEXT("g") }, { TEXT("ou"), TEXT("u") }, { TEXT("o"), TEXT("u") }, { TEXT("e"), TEXT("a") },
		{ TEXT("i"), TEXT("y") }, { TEXT("y"), TEXT("i") }, { TEXT("ch"), TEXT("sh") }, { TEXT("c"), TEXT("k") },
		{ TEXT("w"), TEXT("u") },
	};
	for (const auto& Pair : Replacements)
	{
		Folded.ReplaceInline(Pair[0], Pair[1], ESearchCase::CaseSensitive);
	}

	FString Letters;
	Letters.Reserve(Folded.Len());
	for (const TCHAR C : Folded)
	{
		Letters.AppendChar((C >= TEXT('a') && C <= TEXT('z')) ? C : TEXT(' '));
	}
	return CollapseSpaces(CollapseRepeats(Letters));
}

float FTranscriptMatcher::Similarity(const FString& A, const FString& B)
{
	const int32 LenA = A.Len();
	const int32 LenB = B.Len();
	if (LenA + LenB == 0)
	{
		return 100.f;
	}
	// longest common subsequence, two rows
	TArray<int32> Prev, Cur;
	Prev.SetNumZeroed(LenB + 1);
	Cur.SetNumZeroed(LenB + 1);
	for (int32 i = 1; i <= LenA; ++i)
	{
		for (int32 j = 1; j <= LenB; ++j)
		{
			Cur[j] = A[i - 1] == B[j - 1] ? Prev[j - 1] + 1 : FMath::Max(Prev[j], Cur[j - 1]);
		}
		Swap(Prev, Cur);
	}
	return 100.f * 2.f * (float)Prev[LenB] / (float)(LenA + LenB);
}

FTranscriptMatcher::FResult FTranscriptMatcher::Match(const FString& Transcript, const TArray<TArray<FString>>& Groups, float Threshold, float Margin, bool bCrossScript)
{
	FResult Result;
	if (Groups.Num() == 0)
	{
		return Result;
	}

	TArray<FString> ArabicTokens, LatinTokens;
	NormaliseArabic(Transcript).ParseIntoArray(ArabicTokens, TEXT(" "));
	FString LatinPart = Transcript;
	for (TCHAR& C : LatinPart)
	{
		if (IsArabicBlock(C))
		{
			C = TEXT(' ');
		}
	}
	NormaliseLatin(LatinPart).ParseIntoArray(LatinTokens, TEXT(" "));
	const TArray<FString> SkeletonTokens = bCrossScript ? SkeletonWords(Transcript) : TArray<FString>();

	struct FGroupHit
	{
		FHit Hit;
		int32 Group = INDEX_NONE;
	};
	TArray<FGroupHit> Hits;
	for (int32 g = 0; g < Groups.Num(); ++g)
	{
		FGroupHit GroupBest;
		GroupBest.Group = g;
		auto Consider = [&GroupBest](FHit Hit, const FString& Phrase)
		{
			Hit.Phrase = Phrase;
			if (Better(Hit.Score, Hit.Length, GroupBest.Hit.Score, GroupBest.Hit.Length))
			{
				GroupBest.Hit = Hit;
			}
		};
		// same order as commands.py: Arabic phrases, Latin phrases, then the skeletons of both
		for (const int32 Pass : { 0, 1 })
		{
			for (const FString& Phrase : Groups[g])
			{
				const bool bArabic = ContainsArabic(Phrase);
				if (bArabic != (Pass == 0))
				{
					continue;
				}
				const FString Candidate = bArabic ? NormaliseArabic(Phrase) : NormaliseLatin(Phrase);
				const TArray<FString>& Tokens = bArabic ? ArabicTokens : LatinTokens;
				if (Candidate.IsEmpty() || Tokens.Num() == 0)
				{
					continue;
				}
				Consider(WindowScore(Candidate, Tokens, Pass), Phrase);
			}
		}
		if (bCrossScript)
		{
			for (const int32 Pass : { 0, 1 })
			{
				for (const FString& Phrase : Groups[g])
				{
					if (ContainsArabic(Phrase) == (Pass == 0))
					{
						Consider(SkeletonScore(Skeleton(Phrase), SkeletonTokens), Phrase);
					}
				}
			}
		}
		Hits.Add(GroupBest);
	}

	Hits.StableSort([](const FGroupHit& A, const FGroupHit& B)
	{
		return Better(A.Hit.Score, A.Hit.Length, B.Hit.Score, B.Hit.Length);
	});
	const FHit& Top = Hits[0].Hit;

	// A shorter command heard inside the winner's words is not a rival.
	auto IsNested = [&Top](const FHit& H)
	{
		return H.Script == Top.Script && Top.Start <= H.Start && H.End <= Top.End && H.Length < Top.Length;
	};
	const FGroupHit* Rival = Hits.Num() > 1 ? &Hits[1] : nullptr;
	for (int32 i = 1; i < Hits.Num(); ++i)
	{
		if (!IsNested(Hits[i].Hit))
		{
			Rival = &Hits[i];
			break;
		}
	}

	Result.Score = Top.Score;
	Result.RunnerUp = Rival != nullptr ? Rival->Group : INDEX_NONE;
	Result.RunnerUpScore = Rival != nullptr ? Rival->Hit.Score : 0.f;
	if (Top.Score >= Threshold && Top.Score - Result.RunnerUpScore >= Margin)
	{
		Result.Group = Hits[0].Group;
		Result.Phrase = Top.Phrase;
	}
	return Result;
}
