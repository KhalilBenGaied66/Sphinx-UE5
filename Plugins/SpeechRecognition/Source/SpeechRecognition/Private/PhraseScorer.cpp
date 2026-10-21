#include "PhraseScorer.h"
#include "TranscriptMatcher.h"

#include <limits>

namespace
{
	bool IsArabicChar(TCHAR C)
	{
		return C >= 0x0600 && C <= 0x06FF;
	}

	bool IsHaraka(TCHAR C)
	{
		return (C >= 0x064B && C <= 0x065F) || C == 0x0670 || C == 0x0640;
	}

	/** Lower case, accents off: the Latin letters French and Arabizi use. */
	FString FoldLatinText(const FString& Text)
	{
		FString Out;
		Out.Reserve(Text.Len());
		for (TCHAR C : Text)
		{
			if (C >= TEXT('A') && C <= TEXT('Z'))
			{
				C += TEXT('a') - TEXT('A');
			}
			switch (C)
			{
			case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00C0: case 0x00C2: C = TEXT('a'); break;
			case 0x00E7: case 0x00C7: C = TEXT('c'); break;
			case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB: case 0x00C8: case 0x00C9: case 0x00CA: C = TEXT('e'); break;
			case 0x00EE: case 0x00EF: case 0x00CE: C = TEXT('i'); break;
			case 0x00F4: case 0x00F6: case 0x00D4: C = TEXT('o'); break;
			case 0x00F9: case 0x00FB: case 0x00FC: case 0x00D9: case 0x00DB: C = TEXT('u'); break;
			default: break;
			}
			Out.AppendChar(C);
		}
		return Out;
	}

	FString CollapseWords(const FString& Text)
	{
		TArray<FString> Words;
		Text.ParseIntoArrayWS(Words);
		return FString::Join(Words, TEXT(" "));
	}

	FString CollapseRepeatsOf(const FString& In)
	{
		FString Out;
		for (int32 i = 0; i < In.Len(); ++i)
		{
			if (i == 0 || In[i] != In[i - 1])
			{
				Out.AppendChar(In[i]);
			}
		}
		return Out;
	}

	const TCHAR* ArabicFor(TCHAR C)
	{
		switch (C)
		{
		case TEXT('3'): return TEXT("ع");
		case TEXT('7'): return TEXT("ح");
		case TEXT('9'): return TEXT("ق");
		case TEXT('5'): return TEXT("خ");
		case TEXT('2'): return TEXT("ء");
		case TEXT('8'): return TEXT("غ");
		case TEXT('b'): case TEXT('p'): return TEXT("ب");
		case TEXT('t'): return TEXT("ت");
		case TEXT('j'): return TEXT("ج");
		case TEXT('d'): return TEXT("د");
		case TEXT('r'): return TEXT("ر");
		case TEXT('z'): return TEXT("ز");
		case TEXT('s'): return TEXT("س");
		case TEXT('f'): case TEXT('v'): return TEXT("ف");
		case TEXT('k'): case TEXT('c'): return TEXT("ك");
		case TEXT('q'): case TEXT('g'): return TEXT("ق");
		case TEXT('l'): return TEXT("ل");
		case TEXT('m'): return TEXT("م");
		case TEXT('n'): return TEXT("ن");
		case TEXT('h'): return TEXT("ه");
		case TEXT('w'): return TEXT("و");
		case TEXT('y'): return TEXT("ي");
		case TEXT('x'): return TEXT("كس");
		default: return nullptr;
		}
	}
}

FString FPhraseScorer::ArabiziToArabic(const FString& Phrase, bool bWriteVowels)
{
	static const TCHAR* const Digraphs[][2] = {
		{ TEXT("ch"), TEXT("ش") }, { TEXT("sh"), TEXT("ش") }, { TEXT("kh"), TEXT("خ") }, { TEXT("gh"), TEXT("غ") },
		{ TEXT("th"), TEXT("ت") }, { TEXT("dh"), TEXT("ض") }, { TEXT("ou"), TEXT("و") }, { TEXT("oo"), TEXT("و") },
		{ TEXT("ee"), TEXT("ي") }, { TEXT("aa"), TEXT("ا") },
	};
	TArray<FString> Words;
	FoldLatinText(Phrase).ParseIntoArrayWS(Words);
	TArray<FString> Out;
	for (const FString& Word : Words)
	{
		if (Word == TEXT("el") || Word == TEXT("al") || Word == TEXT("l"))
		{
			Out.Add(TEXT("ال"));
			continue;
		}
		FString Arabic;
		int32 i = 0;
		while (i < Word.Len())
		{
			const FString Pair = Word.Mid(i, 2);
			const TCHAR* Hit = nullptr;
			for (const auto& Digraph : Digraphs)
			{
				if (Pair == Digraph[0])
				{
					Hit = Digraph[1];
					break;
				}
			}
			if (Hit != nullptr)
			{
				Arabic += Hit;
				i += 2;
				continue;
			}
			const TCHAR C = Word[i];
			if (C == TEXT('a') || C == TEXT('e') || C == TEXT('i') || C == TEXT('o') || C == TEXT('u'))
			{
				const bool bFirst = i == 0;
				const bool bLast = i == Word.Len() - 1;
				if (bFirst)
				{
					Arabic += TEXT("ا");
				}
				else if (C == TEXT('i'))
				{
					Arabic += TEXT("ي");
				}
				else if (C == TEXT('o') || C == TEXT('u'))
				{
					Arabic += TEXT("و");
				}
				else if (C == TEXT('a') && (bWriteVowels || bLast))
				{
					Arabic += TEXT("ا");
				}
			}
			else if (const TCHAR* Letter = ArabicFor(C))
			{
				Arabic += Letter;
			}
			++i;
		}
		Arabic = CollapseRepeatsOf(Arabic);
		if (!Arabic.IsEmpty())
		{
			Out.Add(Arabic);
		}
	}
	return FString::Join(Out, TEXT(" "));
}

TArray<FString> FPhraseScorer::Variants(const FString& Phrase)
{
	TArray<FString> Out;
	if (FTranscriptMatcher::ContainsArabic(Phrase))
	{
		FString Base;
		for (const TCHAR C : Phrase)
		{
			if (!IsHaraka(C))
			{
				Base.AppendChar(C);
			}
		}
		Base = CollapseWords(Base);
		Out.Add(Base);
		FString Unified = Base;
		for (TCHAR& C : Unified)
		{
			switch (C)
			{
			case 0x0623: case 0x0625: case 0x0622: C = 0x0627; break; // أ إ آ -> ا
			case 0x0649: C = 0x064A; break;                            // ى -> ي
			case 0x06A8: case 0x06AF: C = 0x0642; break;               // ڨ گ -> ق
			default: break;
			}
		}
		Out.Add(Unified);
	}
	else
	{
		const FString Latin = CollapseWords(FoldLatinText(Phrase));
		FString NoDigits;
		for (const TCHAR C : Latin)
		{
			if (C < TEXT('0') || C > TEXT('9'))
			{
				NoDigits.AppendChar(C);
			}
		}
		Out.Add(NoDigits);
		FString Spelled = Latin;
		Spelled.ReplaceInline(TEXT("9"), TEXT("q"));
		Spelled.ReplaceInline(TEXT("7"), TEXT("h"));
		Spelled.ReplaceInline(TEXT("5"), TEXT("kh"));
		Spelled.ReplaceInline(TEXT("3"), TEXT("a"));
		Spelled.ReplaceInline(TEXT("2"), TEXT(""));
		Out.Add(Spelled);
		Out.Add(ArabiziToArabic(Phrase, true));
		Out.Add(ArabiziToArabic(Phrase, false));
	}

	// word breaks are optional: "3al lisar" is often said, and written, as one word
	const int32 Count = Out.Num();
	for (int32 i = 0; i < Count; ++i)
	{
		if (Out[i].Contains(TEXT(" ")))
		{
			Out.Add(Out[i].Replace(TEXT(" "), TEXT("")));
		}
	}
	TArray<FString> Unique;
	for (const FString& V : Out)
	{
		if (!V.IsEmpty() && !Unique.Contains(V))
		{
			Unique.Add(V);
		}
	}
	return Unique;
}

float FPhraseScorer::AlignScore(const float* Relative, int32 Frames, int32 Vocab, TConstArrayView<int32> Ids)
{
	if (Ids.Num() == 0 || Frames == 0)
	{
		return -std::numeric_limits<float>::infinity();
	}
	const double Neg = -std::numeric_limits<double>::infinity();
	const int32 Blank = 0;

	// blank, id1, blank, id2, ..., idN, blank
	TArray<int32> Ext;
	Ext.Reserve(2 * Ids.Num() + 1);
	Ext.Add(Blank);
	for (const int32 Id : Ids)
	{
		Ext.Add(Id);
		Ext.Add(Blank);
	}
	const int32 S = Ext.Num();
	TArray<bool> CanSkip;
	CanSkip.SetNumZeroed(S);
	for (int32 s = 2; s < S; ++s)
	{
		CanSkip[s] = Ext[s] != Blank && Ext[s] != Ext[s - 2];
	}

	TArray<double> D, Next;
	D.Init(Neg, S);
	Next.Init(Neg, S);
	for (int32 t = 0; t < Frames; ++t)
	{
		const float* Row = Relative + (int64)t * Vocab;
		for (int32 s = 0; s < S; ++s)
		{
			double Best = D[s];
			if (s >= 1)
			{
				Best = FMath::Max(Best, D[s - 1]);
			}
			if (s >= 2 && CanSkip[s])
			{
				Best = FMath::Max(Best, D[s - 2]);
			}
			if (t == 0 && s <= 1)
			{
				Best = FMath::Max(Best, 0.0);
			}
			Next[s] = Best == Neg ? Neg : Best + (double)Row[Ext[s]];
		}
		Swap(D, Next);
	}
	const double Final = FMath::Max(D[S - 1], D[S - 2]);
	return (float)(Final / Ids.Num());
}
