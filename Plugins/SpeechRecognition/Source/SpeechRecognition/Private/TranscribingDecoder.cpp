#include "TranscribingDecoder.h"

#include "Async/Async.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

namespace
{
	float FrameDbfs(const int16* Frame, int32 Num)
	{
		double Sum = 0.0;
		for (int32 i = 0; i < Num; ++i)
		{
			Sum += (double)Frame[i] * (double)Frame[i];
		}
		const double Rms = FMath::Sqrt(Sum / FMath::Max(1, Num)) / 32768.0;
		return Rms > 1e-6 ? 20.f * (float)FMath::LogX(10.0, Rms) : -120.f;
	}

}

FTranscribingDecoder::FTranscribingDecoder()
	: TranscribeLock(MakeShared<FCriticalSection, ESPMode::ThreadSafe>())
{
}

FTranscribingDecoder::~FTranscribingDecoder()
{
	Shutdown();
}

bool FTranscribingDecoder::Initialize(const FSphinxDecoderConfig& Config, ESpeechRecognitionError& OutError, FString& OutMessage)
{
	if (!Config.Transcriber.IsValid())
	{
		OutError = ESpeechRecognitionError::TranscriberUnavailable;
		OutMessage = TEXT("No transcriber was given for this language.");
		return false;
	}
	if (Transcriber != Config.Transcriber)
	{
		// a new engine: what the detector learnt about the room still holds, the queue does not
		for (FPending& Item : Pending)
		{
			Item.Result.Wait();
		}
		Pending.Reset();
	}
	Transcriber = Config.Transcriber;
	Phrases.Reset();
	for (const FRecognitionPhrase& Keyword : Config.Keywords)
	{
		const FString Phrase = Keyword.phrase.TrimStartAndEnd();
		if (!Phrase.IsEmpty())
		{
			Phrases.AddUnique(Phrase);
		}
	}
	return true;
}

void FTranscribingDecoder::Shutdown()
{
	for (FPending& Item : Pending)
	{
		Item.Result.Wait();
	}
	Pending.Reset();
	Transcriber.Reset();
	FrameBuffer.Reset();
	PreRoll.Reset();
	Utterance.Reset();
	bInSpeech = false;
	LoudFrames = 0;
	QuietFrames = 0;
	NoiseFloorDb = -60.f;
}

bool FTranscribingDecoder::IsInitialized() const
{
	return Transcriber.IsValid();
}

void FTranscribingDecoder::ProcessAudio(const int16* Samples, int32 NumSamples, FEventCallback OnEvent)
{
	if (!Transcriber.IsValid())
	{
		return;
	}
	int32 Offset = 0;
	while (Offset < NumSamples)
	{
		const int32 Take = FMath::Min(FrameSamples - FrameBuffer.Num(), NumSamples - Offset);
		FrameBuffer.Append(Samples + Offset, Take);
		Offset += Take;
		if (FrameBuffer.Num() == FrameSamples)
		{
			ProcessFrame(FrameBuffer.GetData(), OnEvent);
			FrameBuffer.Reset();
		}
	}
	DeliverFinished(false, OnEvent);
}

void FTranscribingDecoder::EndStream(FEventCallback OnEvent)
{
	if (bInSpeech)
	{
		FinishUtterance(OnEvent);
	}
	DeliverFinished(true, OnEvent);
}

void FTranscribingDecoder::ProcessFrame(const int16* Frame, FEventCallback OnEvent)
{
	const float FrameSeconds = (float)FrameSamples / (float)SampleRate;
	const float Db = FrameDbfs(Frame, FrameSamples);

	if (!bInSpeech)
	{
		// The noise floor follows quiet frames at once and loud ones slowly, so a
		// word does not raise it but a fan switched on does, within a few seconds.
		NoiseFloorDb = Db < NoiseFloorDb ? Db : NoiseFloorDb + (Db - NoiseFloorDb) * 0.01f;
		NoiseFloorDb = FMath::Max(NoiseFloorDb, -90.f);

		PreRoll.Append(Frame, FrameSamples);
		const int32 Keep = (int32)(Settings.PreRollSeconds * SampleRate);
		if (PreRoll.Num() > Keep)
		{
			PreRoll.RemoveAt(0, PreRoll.Num() - Keep, EAllowShrinking::No);
		}

		const bool bLoud = Db > FMath::Max(NoiseFloorDb + Settings.StartAboveNoiseDb, Settings.MinSpeechDbfs);
		LoudFrames = bLoud ? LoudFrames + 1 : 0;
		if (LoudFrames >= 3)
		{
			bInSpeech = true;
			QuietFrames = 0;
			Utterance = MoveTemp(PreRoll);
			PreRoll.Reset();
			OnEvent(ESphinxDecoderEvent::SpeechStarted, TArray<FRecognisedPhraseDetail>());
		}
		return;
	}

	Utterance.Append(Frame, FrameSamples);
	const bool bVoiced = Db > FMath::Max(NoiseFloorDb + Settings.StayAboveNoiseDb, Settings.MinSpeechDbfs);
	QuietFrames = bVoiced ? 0 : QuietFrames + 1;

	const float Seconds = (float)Utterance.Num() / (float)SampleRate;
	if ((float)QuietFrames * FrameSeconds >= Settings.HangoverSeconds || Seconds >= Settings.MaxSpeechSeconds)
	{
		FinishUtterance(OnEvent);
	}
}

void FTranscribingDecoder::FinishUtterance(FEventCallback OnEvent)
{
	bInSpeech = false;
	LoudFrames = 0;
	const float FrameSeconds = (float)FrameSamples / (float)SampleRate;
	const float SpeechSeconds = (float)Utterance.Num() / (float)SampleRate
		- Settings.PreRollSeconds - (float)QuietFrames * FrameSeconds;
	QuietFrames = 0;

	if (SpeechSeconds < Settings.MinSpeechSeconds)
	{
		// a click or a cough: not worth the engine's time, but reported in turn,
		// after the utterances still being transcribed
		Utterance.Reset();
		TPromise<FSpeechTranscription> Nothing;
		FPending Item;
		Item.Result = Nothing.GetFuture();
		Nothing.SetValue(FSpeechTranscription());
		Pending.Add(MoveTemp(Item));
		DeliverFinished(false, OnEvent);
		return;
	}

	TArray<float> Audio;
	Audio.SetNumUninitialized(Utterance.Num());
	for (int32 i = 0; i < Utterance.Num(); ++i)
	{
		Audio[i] = (float)Utterance[i] / 32768.f;
	}
	Utterance.Reset();

	FPending Item;
	Item.AudioSeconds = (float)Audio.Num() / (float)SampleRate;
	FSpeechTranscriberPtr Engine = Transcriber;
	// one transcription at a time: a model instance is not re-entrant
	TSharedPtr<FCriticalSection, ESPMode::ThreadSafe> Lock = TranscribeLock;
	Item.Result = Async(EAsyncExecution::ThreadPool, [Engine, Lock, Audio = MoveTemp(Audio), Listened = Phrases]()
	{
		FScopeLock Guard(Lock.Get());
		const double Start = FPlatformTime::Seconds();
		FSpeechTranscription Out = Engine->Transcribe(Audio, Listened);
		Out.Seconds = FPlatformTime::Seconds() - Start;
		return Out;
	});
	Pending.Add(MoveTemp(Item));
}

void FTranscribingDecoder::DeliverFinished(bool bWait, FEventCallback OnEvent)
{
	// in order: a later utterance is never reported before an earlier one
	while (Pending.Num() > 0)
	{
		if (!bWait && !Pending[0].Result.IsReady())
		{
			break;
		}
		const FSpeechTranscription Transcription = Pending[0].Result.Get();
		const float AudioSeconds = Pending[0].AudioSeconds;
		Pending.RemoveAt(0);
		Report(Transcription, AudioSeconds, OnEvent);
	}
}

void FTranscribingDecoder::Report(const FSpeechTranscription& Transcription, float AudioSeconds, FEventCallback OnEvent)
{
	if (!Transcription.Error.IsEmpty())
	{
		UE_LOG(SpeechRecognitionPlugin, Warning, TEXT("Transcription failed: %s"), *Transcription.Error);
	}

	const FString Text = Transcription.Text.TrimStartAndEnd();
	if (Text.IsEmpty() && Transcription.PhraseScores.Num() == 0)
	{
		OnEvent(ESphinxDecoderEvent::Unrecognised, TArray<FRecognisedPhraseDetail>());
	}
	else
	{
		FRecognisedPhraseDetail Detail;
		Detail.Phrase = Text;
		Detail.StartTime = 0.f;
		Detail.EndTime = AudioSeconds;
		Detail.bIsTranscript = true;
		Detail.ProcessingSeconds = (float)Transcription.Seconds;
		Detail.PhraseScores = Transcription.PhraseScores;
		UE_LOG(SpeechRecognitionPlugin, Log, TEXT("Heard \"%s\" (%.1f s of audio, transcribed in %.0f ms)"), *Text, AudioSeconds, Transcription.Seconds * 1000.0);
		OnEvent(ESphinxDecoderEvent::Recognised, TArray<FRecognisedPhraseDetail>{ Detail });
	}
	OnEvent(ESphinxDecoderEvent::SpeechEnded, TArray<FRecognisedPhraseDetail>());
}
