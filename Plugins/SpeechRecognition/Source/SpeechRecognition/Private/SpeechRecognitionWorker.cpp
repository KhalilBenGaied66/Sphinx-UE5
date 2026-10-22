
#include "SpeechRecognitionWorker.h"
#include "ISpeechRecognition.h"
#include "TranscribingDecoder.h"

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "Math/RandomStream.h"
#include "Misc/FileHelper.h"

THIRD_PARTY_INCLUDES_START
#include <sphinxbase/ad.h>
THIRD_PARTY_INCLUDES_END

namespace
{
	// 64 ms of audio at 16 kHz
	const int32 AudioBufferSamples = 1024;

	// How long to sleep when the recording device has nothing new. The device
	// buffers several seconds, so this costs no audio.
	const float EmptyReadSleepSeconds = 0.005f;

	// How long to sleep while there is nothing to listen to.
	const float IdleSleepSeconds = 0.01f;

	/**
	 * A WAV file standing in for the microphone: its samples are handed out at the
	 * pace a recording device would deliver them, then faint noise forever, so that
	 * the speech detector sees the utterance end.
	 */
	struct FFileInput
	{
		TArray<int16> Samples;
		int32 SampleRate = 16000;
		double StartSeconds = 0.0;
		int64 Delivered = 0;
		FRandomStream Noise{ 7 };

		bool Load(const FString& Path, FString& OutError)
		{
			TArray<uint8> Bytes;
			if (!FFileHelper::LoadFileToArray(Bytes, *Path))
			{
				OutError = FString::Printf(TEXT("Could not read the input file %s."), *Path);
				return false;
			}
			// RIFF header, then chunks: "fmt " says the format, "data" holds the samples
			if (Bytes.Num() < 12 || FMemory::Memcmp(Bytes.GetData(), "RIFF", 4) != 0 || FMemory::Memcmp(Bytes.GetData() + 8, "WAVE", 4) != 0)
			{
				OutError = FString::Printf(TEXT("%s is not a WAV file."), *Path);
				return false;
			}
			int32 Channels = 0, Bits = 0, Format = 0;
			int64 Offset = 12;
			while (Offset + 8 <= Bytes.Num())
			{
				const uint8* Chunk = Bytes.GetData() + Offset;
				const uint32 Size = Chunk[4] | (Chunk[5] << 8) | (Chunk[6] << 16) | ((uint32)Chunk[7] << 24);
				const uint8* Body = Chunk + 8;
				if (FMemory::Memcmp(Chunk, "fmt ", 4) == 0 && Size >= 16 && Offset + 8 + 16 <= Bytes.Num())
				{
					Format = Body[0] | (Body[1] << 8);
					Channels = Body[2] | (Body[3] << 8);
					SampleRate = Body[4] | (Body[5] << 8) | (Body[6] << 16) | (Body[7] << 24);
					Bits = Body[14] | (Body[15] << 8);
				}
				else if (FMemory::Memcmp(Chunk, "data", 4) == 0)
				{
					const int64 Available = FMath::Min<int64>(Size, Bytes.Num() - (Offset + 8));
					Samples.SetNumUninitialized((int32)(Available / 2));
					FMemory::Memcpy(Samples.GetData(), Body, Samples.Num() * 2);
				}
				Offset += 8 + Size + (Size & 1);
			}
			if (Format != 1 || Channels != 1 || Bits != 16 || SampleRate != 16000 || Samples.Num() == 0)
			{
				OutError = FString::Printf(TEXT("%s must be 16 kHz, 16-bit, mono PCM (it is %d Hz, %d-bit, %d channel(s))."), *Path, SampleRate, Bits, Channels);
				return false;
			}
			StartSeconds = FPlatformTime::Seconds();
			Delivered = 0;
			return true;
		}

		/** What a recording device would have delivered by now, up to Max samples. */
		int32 Read(int16* Out, int32 Max)
		{
			const int64 Due = (int64)((FPlatformTime::Seconds() - StartSeconds) * SampleRate);
			const int32 Count = (int32)FMath::Clamp<int64>(Due - Delivered, 0, Max);
			for (int32 i = 0; i < Count; ++i)
			{
				const int64 Index = Delivered + i;
				Out[i] = Index < Samples.Num() ? Samples[(int32)Index] : (int16)Noise.RandRange(-20, 20);
			}
			Delivered += Count;
			return Count;
		}
	};
}

FSpeechRecognitionWorker::FSpeechRecognitionWorker() {}

FSpeechRecognitionWorker::~FSpeechRecognitionWorker() {
	// Idempotent: if ShutDown() was already called by the owning subsystem,
	// this is a no-op. Guarantees the thread is stopped and joined before
	// the worker's memory is released.
	ShutDown();
}

void FSpeechRecognitionWorker::ShutDown() {
	Stop();
	if (Thread != nullptr) {
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
}

void FSpeechRecognitionWorker::Stop() {
	bStopRequested.store(true);
}

bool FSpeechRecognitionWorker::StartThread() {
	const int32 threadIdx = ISpeechRecognition::Get().GetInstanceCounter();
	const FString threadName = FString("FSpeechRecognitionWorker:") + FString::FromInt(threadIdx);
	Thread = FRunnableThread::Create(this, *threadName, 0U, TPri_Normal);
	return Thread != nullptr;
}

void FSpeechRecognitionWorker::Request(FSpeechWorkerRequest&& InRequest)
{
	Requests.Enqueue(MoveTemp(InRequest));
}

bool FSpeechRecognitionWorker::PollEvent(FSpeechWorkerEvent& OutEvent)
{
	return Events.Dequeue(OutEvent);
}

int16 FSpeechRecognitionWorker::GetCurrentVolume() const
{
	return (int16)currentVolume.load(std::memory_order_relaxed);
}

uint32 FSpeechRecognitionWorker::Run() {

	// Everything Sphinx lives on this thread's stack: no other thread can reach it.
	FSphinxDecoder decoder;
	// Languages without a PocketSphinx model: the same microphone, another decoder.
	FTranscribingDecoder transcriber;
	bool bTranscribing = false;
	ad_rec_t* ad = nullptr;
	// or, for tests and for replaying recordings, a file in place of the microphone
	TUniquePtr<FFileInput> file;
	FString inputFile;
	int16 adbuf[AudioBufferSamples];
	uint32 generation = 0;

	auto CloseDevice = [&ad, &file]()
	{
		if (ad != nullptr) {
			ad_close(ad);
			ad = nullptr;
		}
		file.Reset();
	};
	auto IsInputOpen = [&ad, &file]() { return ad != nullptr || file.IsValid(); };

	auto StopListening = [&]()
	{
		CloseDevice();
		decoder.Shutdown();
		transcriber.Shutdown();
		bTranscribing = false;
		currentVolume.store(0, std::memory_order_relaxed);
	};

	auto Fail = [&](ESpeechRecognitionError Error, const FString& Message)
	{
		StopListening();
		FSpeechWorkerEvent Event;
		Event.Type = FSpeechWorkerEvent::EType::Failed;
		Event.Generation = generation;
		Event.Error = Error;
		Event.Message = Message;
		Events.Enqueue(MoveTemp(Event));
	};

	while (!bStopRequested.load()) {

		// take the most recent request from the game thread, if there is one
		FSpeechWorkerRequest request;
		bool hasRequest = false;
		{
			FSpeechWorkerRequest next;
			while (Requests.Dequeue(next)) {
				request = MoveTemp(next);
				hasRequest = true;
			}
		}

		if (hasRequest) {

			generation = request.Generation;

			if (!request.bListen) {
				StopListening();
			}
			else {
				ESpeechRecognitionError error = ESpeechRecognitionError::None;
				FString message;
				const bool bWantTranscriber = request.Config.Transcriber.IsValid();

				// another input (a file, another file, the microphone again): reopen it
				if (request.InputFile != inputFile) {
					CloseDevice();
					inputFile = request.InputFile;
				}

				if (bWantTranscriber) {
					// The microphone stays open across a change of phrases: the
					// transcriber listens to everything and the phrases only matter
					// to the components that match its text.
					if (!bTranscribing) {
						StopListening();
					}
					if (!transcriber.Initialize(request.Config, error, message)) {
						Fail(error, message);
						continue;
					}
					bTranscribing = true;
				}
				else {
					// Another mode or another list of phrases on the same model: the
					// decoder is kept, and the recording device stays open. Otherwise
					// release both before starting again.
					if (!IsInputOpen() || bTranscribing || !decoder.CanReuse(request.Config)) {
						StopListening();
					}

					if (!decoder.Initialize(request.Config, error, message)) {
						Fail(error, message);
						continue;
					}
				}

				if (!IsInputOpen() && !inputFile.IsEmpty()) {
					file = MakeUnique<FFileInput>();
					if (!file->Load(inputFile, message)) {
						Fail(ESpeechRecognitionError::AudioDeviceFailed, message);
						continue;
					}
				}
				if (!IsInputOpen()) {
					// attempt to open the default recording device
					const int32 sampleRate = bTranscribing ? FTranscribingDecoder::SampleRate : decoder.GetSampleRate();
					if ((ad = ad_open_dev(nullptr, sampleRate)) == nullptr) {
						Fail(ESpeechRecognitionError::AudioDeviceFailed, TEXT("Failed to open the default recording device. Check that a microphone is plugged in and that Windows allows desktop apps to use it."));
						continue;
					}

					if (ad_start_rec(ad) < 0) {
						Fail(ESpeechRecognitionError::AudioDeviceFailed, TEXT("Failed to start recording from the default recording device."));
						continue;
					}
				}

				FSpeechWorkerEvent Event;
				Event.Type = FSpeechWorkerEvent::EType::Listening;
				Event.Generation = generation;
				if (!bTranscribing) {
					Event.RejectedPhrases = decoder.GetRejectedPhrases();
				}
				Events.Enqueue(MoveTemp(Event));
			}
		}

		// nothing to listen to yet
		if (!IsInputOpen()) {
			FPlatformProcess::Sleep(IdleSleepSeconds);
			continue;
		}

		const int32 k = file.IsValid() ? file->Read(adbuf, AudioBufferSamples) : ad_read(ad, adbuf, AudioBufferSamples);
		if (k < 0) {
			Fail(ESpeechRecognitionError::AudioDeviceFailed, TEXT("Failed to read audio: the recording device stopped delivering samples."));
			continue;
		}
		if (k == 0) {
			// ad_read does not block. Without this pause the loop spins on an
			// empty device and keeps one core busy for nothing.
			FPlatformProcess::Sleep(EmptyReadSleepSeconds);
			continue;
		}

		// publish the volume for the game thread
		currentVolume.store(FSphinxDecoder::ComputeAveragePeak(adbuf, k), std::memory_order_relaxed);

		auto OnDecoderEvent = [&](ESphinxDecoderEvent DecoderEvent, const TArray<FRecognisedPhraseDetail>& Phrases)
		{
			FSpeechWorkerEvent Event;
			Event.Generation = generation;
			switch (DecoderEvent)
			{
			case ESphinxDecoderEvent::SpeechStarted:
				Event.Type = FSpeechWorkerEvent::EType::SpeechStarted;
				break;
			case ESphinxDecoderEvent::Recognised:
				Event.Type = FSpeechWorkerEvent::EType::Recognised;
				Event.Phrases = Phrases;
				break;
			case ESphinxDecoderEvent::Unrecognised:
				Event.Type = FSpeechWorkerEvent::EType::Unrecognised;
				break;
			default:
				Event.Type = FSpeechWorkerEvent::EType::SpeechEnded;
				break;
			}
			Events.Enqueue(MoveTemp(Event));
		};
		if (bTranscribing) {
			transcriber.ProcessAudio(adbuf, k, OnDecoderEvent);
		}
		else {
			decoder.ProcessAudio(adbuf, k, OnDecoderEvent);
		}
	}

	// normal shutdown: release the audio device and the decoder
	StopListening();

	return 0;
}
