#include "SpeechRecognitionSettings.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

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
		if (FileManager.FileExists(*GetDictionaryPath(Folder)) && FileManager.FileExists(*(GetAcousticModelDir(Folder) / TEXT("mdef"))))
		{
			Languages.Add(Folder);
		}
	}
	Languages.Sort();
	return Languages;
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
