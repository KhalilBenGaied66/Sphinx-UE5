#include "SpeechRecognitionSettings.h"

#include "HAL/FileManager.h"
#include "SpeechTranscribers.h"
#include "Misc/Paths.h"

namespace
{
	bool FolderHasTranscriptionModel(const FString& Folder)
	{
		return !SpeechTranscribers::FindOnnxModel(Folder).IsEmpty() && IFileManager::Get().FileExists(*(Folder / TEXT("tokens.txt")));
	}
}

FName USpeechRecognitionSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FString USpeechRecognitionSettings::GetModelRoot()
{
	FString Directory = GetDefault<USpeechRecognitionSettings>()->ModelDirectory;
	if (Directory.IsEmpty())
	{
		Directory = TEXT("model");
	}
	if (FPaths::IsRelative(Directory))
	{
		Directory = FPaths::ProjectContentDir() / Directory;
	}
	Directory = FPaths::ConvertRelativePathToFull(Directory);
	FPaths::NormalizeDirectoryName(Directory);
	return Directory;
}

FString USpeechRecognitionSettings::GetLanguageCode(ESpeechRecognitionLanguage Language)
{
	switch (Language)
	{
	case ESpeechRecognitionLanguage::VE_English:
		return TEXT("en");
	case ESpeechRecognitionLanguage::VE_Chinese:
		// spelt this way since the first version of the plugin; kept so existing model folders still load
		return TEXT("zn");
	case ESpeechRecognitionLanguage::VE_French:
		return TEXT("fr");
	case ESpeechRecognitionLanguage::VE_Spanish:
		return TEXT("es");
	case ESpeechRecognitionLanguage::VE_Russian:
		return TEXT("ru");
	case ESpeechRecognitionLanguage::VE_German:
		return TEXT("de");
	default:
		return TEXT("en");
	}
}

TArray<FString> USpeechRecognitionSettings::FindInstalledLanguages()
{
	TArray<FString> Languages;
	const FString Root = GetModelRoot();
	IFileManager& FileManager = IFileManager::Get();

	TArray<FString> Folders;
	FileManager.FindFiles(Folders, *(Root / TEXT("*")), false, true);
	for (const FString& Folder : Folders)
	{
		if (HasSphinxModel(Folder) || FolderHasTranscriptionModel(GetLanguageDir(Folder)))
		{
			Languages.Add(Folder);
		}
	}
	Languages.Sort();
	return Languages;
}

FString USpeechRecognitionSettings::GetLanguageDir(const FString& LanguageCode)
{
	return GetModelRoot() / LanguageCode;
}

bool USpeechRecognitionSettings::HasSphinxModel(const FString& LanguageCode)
{
	IFileManager& FileManager = IFileManager::Get();
	return FileManager.FileExists(*GetDictionaryPath(LanguageCode)) && FileManager.FileExists(*(GetAcousticModelDir(LanguageCode) / TEXT("mdef")));
}

bool USpeechRecognitionSettings::IsArabicCode(const FString& LanguageCode)
{
	// Arabic (ISO 639-1 "ar", 639-3 macro "ara"), Modern Standard Arabic and the
	// varieties Omnilingual ASR was trained on
	static const TCHAR* const Codes[] = {
		TEXT("ar"), TEXT("ara"), TEXT("arb"), TEXT("aeb"), TEXT("ary"), TEXT("arq"), TEXT("arz"), TEXT("apc"),
		TEXT("ajp"), TEXT("apd"), TEXT("acm"), TEXT("acw"), TEXT("acx"), TEXT("aec"), TEXT("afb"), TEXT("ars"),
		TEXT("ayl"), TEXT("ayp"), TEXT("abv"), TEXT("shu"),
	};
	FString Code = LanguageCode.TrimStartAndEnd().ToLower();
	int32 Dash = INDEX_NONE;
	if (Code.FindChar(TEXT('-'), Dash) || Code.FindChar(TEXT('_'), Dash))
	{
		Code = Code.Left(Dash); // ar-TN, ar_MA
	}
	for (const TCHAR* Known : Codes)
	{
		if (Code == Known)
		{
			return true;
		}
	}
	return false;
}

bool USpeechRecognitionSettings::IsAnyLanguageCode(const FString& LanguageCode)
{
	return LanguageCode.TrimStartAndEnd().Equals(TEXT("any"), ESearchCase::IgnoreCase);
}

bool USpeechRecognitionSettings::UsesSharedModel(const FString& LanguageCode)
{
	return IsAnyLanguageCode(LanguageCode) || IsArabicCode(LanguageCode);
}

FString USpeechRecognitionSettings::GetTranscriptionModelDir(const FString& LanguageCode)
{
	const FString Own = GetLanguageDir(LanguageCode);
	if (FolderHasTranscriptionModel(Own) || !UsesSharedModel(LanguageCode))
	{
		return Own;
	}
	return GetModelRoot() / TEXT("omnilingual");
}

bool USpeechRecognitionSettings::HasTranscriptionModel(const FString& LanguageCode)
{
	return FolderHasTranscriptionModel(GetTranscriptionModelDir(LanguageCode));
}

bool USpeechRecognitionSettings::IsTranscribedLanguage(const FString& LanguageCode)
{
	if (LanguageCode.IsEmpty() || HasSphinxModel(LanguageCode))
	{
		return false;
	}
	return HasTranscriptionModel(LanguageCode) || UsesSharedModel(LanguageCode);
}

FString USpeechRecognitionSettings::GetAcousticModelDir(const FString& LanguageCode)
{
	return GetModelRoot() / LanguageCode / LanguageCode;
}

FString USpeechRecognitionSettings::GetDictionaryPath(const FString& LanguageCode)
{
	return GetModelRoot() / LanguageCode / (LanguageCode + TEXT(".dict"));
}

FString USpeechRecognitionSettings::GetGrammarPath(const FString& LanguageCode, const FString& GrammarName)
{
	return GetModelRoot() / LanguageCode / TEXT("grammars") / (GrammarName + TEXT(".gram"));
}

FString USpeechRecognitionSettings::GetLanguageModelPath(const FString& LanguageCode, const FString& ModelName)
{
	const FString Base = GetModelRoot() / LanguageCode / TEXT("language_models") / ModelName;
	const FString Text = Base + TEXT(".lm");
	const FString Binary = Base + TEXT(".lm.bin");
	if (!IFileManager::Get().FileExists(*Text) && IFileManager::Get().FileExists(*Binary))
	{
		return Binary;
	}
	return Text;
}
