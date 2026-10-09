#include "SpeechTestSupport.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SpeechRecognitionSettings.h"
#include "SpeechRecognitionSubsystem.h"

namespace SpeechTest
{
	FString FDecodeResult::Joined() const
	{
		TArray<FString> Words;
		for (const FRecognisedPhraseDetail& Phrase : Phrases)
		{
			Words.Add(Phrase.Phrase);
		}
		return FString::Join(Words, TEXT(" "));
	}

	bool FDecodeResult::Contains(const FString& Phrase) const
	{
		return Phrases.ContainsByPredicate([&Phrase](const FRecognisedPhraseDetail& Candidate) { return Candidate.Phrase == Phrase; });
	}

	bool FDecodeResult::GetReportLag(const FString& Phrase, double& OutSeconds) const
	{
		const int32 Index = Phrases.IndexOfByPredicate([&Phrase](const FRecognisedPhraseDetail& Candidate) { return Candidate.Phrase == Phrase; });
		if (Index == INDEX_NONE)
		{
			return false;
		}
		OutSeconds = ReportTimes[Index] - SpeechEndSeconds;
		return true;
	}

	FString GetAudioRoot()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SpeechRecognition"));
		check(Plugin.IsValid());
		return FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Resources/TestAudio"));
	}

	bool LoadPcm16(const FString& Path, TArray<int16>& OutSamples, int32& OutSampleRate, FString& OutError)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path))
		{
			OutError = FString::Printf(TEXT("Cannot read '%s'."), *Path);
			return false;
		}

		const uint8* Data = Bytes.GetData();
		int32 DataOffset = 0;
		int32 DataSize = Bytes.Num();
		OutSampleRate = 16000;

		if (Path.EndsWith(TEXT(".wav"), ESearchCase::IgnoreCase))
		{
			auto ReadU32 = [Data](int32 Offset) { return (uint32)Data[Offset] | ((uint32)Data[Offset + 1] << 8) | ((uint32)Data[Offset + 2] << 16) | ((uint32)Data[Offset + 3] << 24); };
			auto ReadU16 = [Data](int32 Offset) { return (uint16)((uint16)Data[Offset] | ((uint16)Data[Offset + 1] << 8)); };
			auto IsTag = [Data](int32 Offset, const char* Tag) { return FMemory::Memcmp(Data + Offset, Tag, 4) == 0; };

			if (Bytes.Num() < 12 || !IsTag(0, "RIFF") || !IsTag(8, "WAVE"))
			{
				OutError = FString::Printf(TEXT("'%s' is not a WAV file."), *Path);
				return false;
			}

			bool bFoundFormat = false;
			bool bFoundData = false;
			for (int32 Offset = 12; Offset + 8 <= Bytes.Num(); )
			{
				const int32 ChunkSize = (int32)ReadU32(Offset + 4);
				const int32 Body = Offset + 8;
				if (IsTag(Offset, "fmt ") && Body + 16 <= Bytes.Num())
				{
					const uint16 Format = ReadU16(Body);
					const uint16 Channels = ReadU16(Body + 2);
					const uint16 BitsPerSample = ReadU16(Body + 14);
					OutSampleRate = (int32)ReadU32(Body + 4);
					if (Format != 1 || Channels != 1 || BitsPerSample != 16)
					{
						OutError = FString::Printf(TEXT("'%s' must be 16-bit PCM mono (it is format %d, %d channel(s), %d bits)."), *Path, Format, Channels, BitsPerSample);
						return false;
					}
					bFoundFormat = true;
				}
				else if (IsTag(Offset, "data"))
				{
					DataOffset = Body;
					DataSize = FMath::Min(ChunkSize, Bytes.Num() - Body);
					bFoundData = true;
					break;
				}
				Offset = Body + ChunkSize + (ChunkSize & 1);
			}
			if (!bFoundFormat || !bFoundData)
			{
				OutError = FString::Printf(TEXT("'%s' has no format or no data chunk."), *Path);
				return false;
			}
		}

		const int32 NumSamples = DataSize / 2;
		OutSamples.SetNumUninitialized(NumSamples);
		FMemory::Memcpy(OutSamples.GetData(), Data + DataOffset, (SIZE_T)NumSamples * 2);
		return true;
	}

	static bool LoadTsv(const FString& FileName, TArray<TArray<FString>>& OutRows)
	{
		// the manifests are UTF-8 without a byte order mark: decode them as such, explicitly
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *(GetAudioRoot() / FileName)))
		{
			return false;
		}
		const FUTF8ToTCHAR Converted((const ANSICHAR*)Bytes.GetData(), Bytes.Num());
		const FString Text(Converted.Length(), Converted.Get());
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, false);
		for (int32 Index = 1; Index < Lines.Num(); ++Index) // the first line names the columns
		{
			if (Lines[Index].TrimStartAndEnd().IsEmpty())
			{
				continue;
			}
			TArray<FString> Cells;
			Lines[Index].ParseIntoArray(Cells, TEXT("\t"), false);
			OutRows.Add(MoveTemp(Cells));
		}
		return true;
	}

	bool LoadUtterances(TArray<FUtterance>& OutUtterances)
	{
		TArray<TArray<FString>> Rows;
		if (!LoadTsv(TEXT("utterances.tsv"), Rows))
		{
			return false;
		}
		for (const TArray<FString>& Cells : Rows)
		{
			if (Cells.Num() < 8)
			{
				continue;
			}
			FUtterance Utterance;
			Utterance.Id = Cells[0];
			Utterance.Language = Cells[1];
			Utterance.Source = Cells[2];
			Utterance.File = GetAudioRoot() / Cells[4];
			Utterance.Expected = Cells[5];
			Utterance.bGrammarSentence = Cells[6] == TEXT("1");
			Utterance.Text = Cells[7];
			OutUtterances.Add(MoveTemp(Utterance));
		}
		return true;
	}

	bool LoadCommands(const FString& Language, TArray<FRecognitionPhrase>& OutCommands)
	{
		TArray<TArray<FString>> Rows;
		if (!LoadTsv(TEXT("commands.tsv"), Rows))
		{
			return false;
		}
		for (const TArray<FString>& Cells : Rows)
		{
			if (Cells.Num() >= 3 && Cells[0] == Language)
			{
				const int32 Level = FMath::Clamp(FCString::Atoi(*Cells[2]), 1, 10);
				OutCommands.Emplace(Cells[1], (EPhraseRecognitionTolerance)(Level - 1));
			}
		}
		return true;
	}

	bool IsLanguageInstalled(const FString& Language)
	{
		return IFileManager::Get().FileExists(*(USpeechRecognitionSettings::GetAcousticModelDir(Language) / TEXT("mdef")))
			&& IFileManager::Get().FileExists(*USpeechRecognitionSettings::GetDictionaryPath(Language));
	}

	FSphinxDecoderConfig MakeConfig(const FString& Language, ESpeechRecognitionMode Mode)
	{
		FSphinxDecoderConfig Config;
		Config.AcousticModelDir = USpeechRecognitionSettings::GetAcousticModelDir(Language);
		Config.DictionaryPath = USpeechRecognitionSettings::GetDictionaryPath(Language);
		Config.Mode = Mode;
		Config.Params = USpeechRecognitionSubsystem::GetDefaultConfigParams();
		return Config;
	}

	FDecodeResult DecodeSamples(FSphinxDecoder& Decoder, const TArray<int16>& Samples, int32 SampleRate, float LeadInSeconds)
	{
		// A microphone never delivers digital silence: pad with a faint, repeatable hiss.
		const int32 LeadIn = (int32)((float)SampleRate * LeadInSeconds);
		const int32 Tail = SampleRate * 3 / 2;
		TArray<int16> Stream;
		Stream.Reserve(LeadIn + Samples.Num() + Tail);
		uint32 Seed = 12345u;
		auto Hiss = [&Seed]()
		{
			Seed = Seed * 1664525u + 1013904223u;
			return (int16)((int32)((Seed >> 16) % 7u) - 3);
		};
		for (int32 Index = 0; Index < LeadIn; ++Index)
		{
			Stream.Add(Hiss());
		}
		Stream.Append(Samples);
		for (int32 Index = 0; Index < Tail; ++Index)
		{
			Stream.Add(Hiss());
		}

		FDecodeResult Result;
		Result.AudioSeconds = (double)Stream.Num() / (double)SampleRate;
		int32 Fed = 0;

		// where the speech sits in the file, by amplitude
		int32 Peak = 0;
		for (const int16 Sample : Samples)
		{
			Peak = FMath::Max(Peak, FMath::Abs((int32)Sample));
		}
		const int32 Loud = FMath::Max(200, Peak / 20);
		int32 FirstLoud = INDEX_NONE;
		int32 LastLoud = INDEX_NONE;
		for (int32 Index = 0; Index < Samples.Num(); ++Index)
		{
			if (FMath::Abs((int32)Samples[Index]) > Loud)
			{
				if (FirstLoud == INDEX_NONE)
				{
					FirstLoud = Index;
				}
				LastLoud = Index;
			}
		}
		Result.SpeechStartSeconds = (double)FMath::Max(FirstLoud, 0) / (double)SampleRate;
		Result.SpeechEndSeconds = (double)(LastLoud == INDEX_NONE ? Samples.Num() : LastLoud + 1) / (double)SampleRate;

		auto OnEvent = [&](ESphinxDecoderEvent Event, const TArray<FRecognisedPhraseDetail>& Phrases)
		{
			switch (Event)
			{
			case ESphinxDecoderEvent::SpeechStarted:
				++Result.SpeechStartedEvents;
				break;
			case ESphinxDecoderEvent::Recognised:
				++Result.RecognisedEvents;
				for (const FRecognisedPhraseDetail& Phrase : Phrases)
				{
					Result.Phrases.Add(Phrase);
					Result.ReportTimes.Add((double)(Fed - LeadIn) / (double)SampleRate);
				}
				break;
			case ESphinxDecoderEvent::Unrecognised:
				++Result.UnrecognisedEvents;
				break;
			case ESphinxDecoderEvent::SpeechEnded:
				++Result.SpeechEndedEvents;
				break;
			}
		};

		const int32 ChunkSize = 1024;
		const double StartTime = FPlatformTime::Seconds();
		for (int32 Offset = 0; Offset < Stream.Num(); Offset += ChunkSize)
		{
			const int32 Count = FMath::Min(ChunkSize, Stream.Num() - Offset);
			Fed = Offset + Count;
			Decoder.ProcessAudio(Stream.GetData() + Offset, Count, OnEvent);
		}
		Decoder.EndStream(OnEvent);
		Result.DecodeSeconds = FPlatformTime::Seconds() - StartTime;
		return Result;
	}

	bool DecodeFile(FSphinxDecoder& Decoder, const FString& Path, FDecodeResult& OutResult, FString& OutError, float LeadInSeconds)
	{
		TArray<int16> Samples;
		int32 SampleRate = 0;
		if (!LoadPcm16(Path, Samples, SampleRate, OutError))
		{
			return false;
		}
		if (SampleRate != Decoder.GetSampleRate())
		{
			OutError = FString::Printf(TEXT("'%s' is sampled at %d Hz; the model expects %d Hz."), *Path, SampleRate, Decoder.GetSampleRate());
			return false;
		}
		OutResult = DecodeSamples(Decoder, Samples, SampleRate, LeadInSeconds);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
