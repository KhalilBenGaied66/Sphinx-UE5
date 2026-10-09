#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "HAL/Runnable.h"
#include "SphinxDecoder.h"

#include <atomic>

class FRunnableThread;

/** Game thread to worker: start listening with this configuration, or stop listening. */
struct FSpeechWorkerRequest
{
	/** Stamped on every event the request produces, so that late events of an older request can be told apart. */
	uint32 Generation = 0;

	/** False closes the microphone and the decoder without starting new ones. */
	bool bListen = true;

	FSphinxDecoderConfig Config;
};

/** Worker to game thread. */
struct FSpeechWorkerEvent
{
	enum class EType : uint8
	{
		/** The model is loaded and the microphone is open. */
		Listening,
		/** The request could not be honoured, or the microphone failed later on. */
		Failed,
		SpeechStarted,
		Recognised,
		Unrecognised,
		SpeechEnded
	};

	EType Type = EType::Listening;
	uint32 Generation = 0;

	/** Recognised: what was heard. */
	TArray<FRecognisedPhraseDetail> Phrases;

	/** Listening: key phrases left out because a word is not in the dictionary. */
	TArray<FString> RejectedPhrases;

	/** Failed: why. */
	ESpeechRecognitionError Error = ESpeechRecognitionError::None;
	FString Message;
};

/**
 * Owns the recording device and the decoder, on its own thread.
 *
 * Nothing here is shared with the game thread except the two queues and the
 * volume: requests come in through one queue, events leave through the other,
 * and the worker never touches a UObject.
 */
class FSpeechRecognitionWorker : public FRunnable
{

private:
	//Thread
	FRunnableThread* Thread = nullptr;

	//Set by Stop(), read by the worker loop
	std::atomic<bool> bStopRequested{ false };

	//Stores the current, peak volume.
	//Written by the worker thread, read from the game thread.
	std::atomic<int32> currentVolume{ 0 };

	//Requests from the game thread. Only the most recent one matters.
	TQueue<FSpeechWorkerRequest, EQueueMode::Mpsc> Requests;

	//Events for the game thread, in the order they happened.
	TQueue<FSpeechWorkerEvent, EQueueMode::Spsc> Events;

public:
	FSpeechRecognitionWorker();
	virtual ~FSpeechRecognitionWorker() override;

	//FRunnable interface
	virtual void Stop() override;
	virtual uint32 Run() override;

	//Starts the thread. It idles until the first request.
	bool StartThread();

	//Stops the thread and waits for it. Safe to call twice.
	void ShutDown();

	//Game thread: start listening with this configuration (or stop listening).
	//A request replaces whatever the worker was doing.
	void Request(FSpeechWorkerRequest&& InRequest);

	//Game thread: takes the next event, if there is one.
	bool PollEvent(FSpeechWorkerEvent& OutEvent);

	int16 GetCurrentVolume() const;

};
