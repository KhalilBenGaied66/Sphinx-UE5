#include "VoiceCommandSet.h"

bool UVoiceCommandSet::IsForLanguage(const FString& LanguageCode) const
{
	return Language.TrimStartAndEnd().Equals(LanguageCode.TrimStartAndEnd(), ESearchCase::IgnoreCase);
}
