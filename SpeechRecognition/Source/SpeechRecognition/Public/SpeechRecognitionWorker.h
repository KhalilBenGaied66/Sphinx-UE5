#pragma once

#include <sphinxbase/err.h>
#include <sphinxbase/ad.h>
#include <pocketsphinx.h>
#include <stdio.h>
#include <time.h>
#include <regex>
#include <string>
#include <set>
#include <map>
#include <fstream>
#include <iostream>
#include <sstream>
#include <locale>
#include <cstdio>
#include <vector>
#include <utility>
#include <atomic>

#include "SpeechRecognition.h"
#include "Chaos/AABB.h"

#define SENSCR_SHIFT 10

class USpeechRecognitionSubsystem;

using namespace std;

//Common structures and enumerations
struct FSpeechRecognitionParam
{
	std::string name;
	ESpeechRecognitionParamType type;
	std::string value;

	// constructor
	FSpeechRecognitionParam(const char* InName, ESpeechRecognitionParamType InType, const char* InValue)
		: name(InName)
		, type(InType)
		, value(InValue)
	{
	}
};

class FSpeechRecognitionWorker : public FRunnable
{

private:
	// Sphinx
	ps_decoder_t *ps = nullptr;
	cmd_ln_t *config = nullptr;
	ad_rec_t *ad = nullptr;
	int16 adbuf[1024];
	uint8 utt_started, in_speech;
	int32 k;
	bool initRequired = false;
	bool wordsAdded = false;

	//Stores the current, peak volume.
	//Written by the worker thread, read from the game thread.
	std::atomic<int32> currentVolume{0};

	//A set of params to apply to Sphinx initialisation
	TArray<FSpeechRecognitionParam> sphinxParams;

	//Speech detection mode
	ESpeechRecognitionMode detectionMode;
	
	//Language
	ESpeechRecognitionLanguage language;

	//Thread
	FRunnableThread* Thread = nullptr;

	//Pointer to our manager
	USpeechRecognitionSubsystem* Manager = nullptr;

	//Thread safe counter 
	FThreadSafeCounter StopTaskCounter;

	//Language
	const char* langStr = nullptr;

	//Paths
	std::string argFilePath;
	std::string contentPath_str;
	std::string logPath;
	std::string modelPath;
	std::string languageModel;
	std::string dictionaryPath;

	//Stores the recognition keywords, along with their tolerances
	std::map <string , char*> keywords;

	//Dictionary
	std::map <string, set<string>> dictionary;

	//Splits a string by whitespace
	vector<string> Split(string s);
	//Removes brackets, and 1-9 characters, from a string
	string GetOriginalString(string s) const;

	//Releases the audio device and decoder prior to a re-initialisation,
	//shutdown, or failed initialisation (worker thread only)
	void ReleaseSphinxResources();

public:
	FSpeechRecognitionWorker();
	virtual ~FSpeechRecognitionWorker() override;

	//FRunnable interface
	virtual void Stop() override;
	virtual uint32 Run() override;

	//Methods to switch recognition modes
	bool EnableKeywordMode(const TArray<FRecognitionPhrase>& wordList);
	bool EnableGrammarMode(FString grammarName);
	bool EnableLanguageModel(FString InLanguageModel);

	//Action methods
	void AddWords(const TArray<FRecognitionPhrase>& InKeywords);
	int16 GetCurrentVolume() const;
	void InitConfig();
	bool SetConfigParam(const FString& param, ESpeechRecognitionParamType type, const FString& value);
	void SetLanguage(ESpeechRecognitionLanguage InLanguage);
	bool StartThread(USpeechRecognitionSubsystem* manager);
	void ShutDown();

	// Print Debug Text
	static void ClientMessage(const FString& txt);

};

