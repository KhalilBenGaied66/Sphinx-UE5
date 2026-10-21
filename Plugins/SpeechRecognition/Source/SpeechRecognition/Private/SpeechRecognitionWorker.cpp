
#include "SpeechRecognitionWorker.h"
#include "ISpeechRecognition.h"
#include "TranscribingDecoder.h"

#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"

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
	int16 adbuf[AudioBufferSamples];
	uint32 generation = 0;

	auto CloseDevice = [&ad]()
	{
		if (ad != nullptr) {
			ad_close(ad);
			ad = nullptr;
		}
	};

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
					if (ad == nullptr || bTranscribing || !decoder.CanReuse(request.Config)) {
						StopListening();
					}

					if (!decoder.Initialize(request.Config, error, message)) {
						Fail(error, message);
						continue;
					}
				}

				if (ad == nullptr) {
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
		if (ad == nullptr) {
			FPlatformProcess::Sleep(IdleSleepSeconds);
			continue;
		}

		const int32 k = ad_read(ad, adbuf, AudioBufferSamples);
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
