#include "SphinxSampleSettings.h"

namespace
{
	FVoiceCommand MakeCommand(FName Command, std::initializer_list<const TCHAR*> Phrases, EPhraseRecognitionTolerance Tolerance)
	{
		FVoiceCommand Result;
		Result.Command = Command;
		Result.Tolerance = Tolerance;
		for (const TCHAR* Phrase : Phrases)
		{
			Result.Phrases.Add(Phrase);
		}
		return Result;
	}
}

USphinxSampleSettings::USphinxSampleSettings()
{
	// The defaults. Project Settings shows them with a reset arrow once changed.

	// English, PocketSphinx. Longer phrases are easier to tell apart, so they can
	// be given a looser tolerance; a single short word needs a strict one or it
	// fires on anything.
	English = {
		MakeCommand(TEXT("Forward"), { TEXT("go forward"), TEXT("move forward") }, EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Back"),    { TEXT("go back"), TEXT("move back") },       EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Left"),    { TEXT("turn left") },                         EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Right"),   { TEXT("turn right") },                        EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Jump"),    { TEXT("jump") },                              EPhraseRecognitionTolerance::VE_8),
		MakeCommand(TEXT("Stop"),    { TEXT("stop") },                              EPhraseRecognitionTolerance::VE_8),
	};

	// The languages below have no PocketSphinx model: the plugin scores each phrase
	// against what the second engine's speech model heard. A phrase may be written
	// in Arabic script or in Latin letters, as it sounds; one spelling is enough,
	// more spellings help. The tolerance is only used by PocketSphinx.
	//
	// The screen shows how close each utterance came, so a wording that is said
	// often but scores below -2.5 is the one to add.

	// Tunisian Darija, as in Tools/DarijaEval/commands.toml
	Tunisian = {
		MakeCommand(TEXT("Forward"), { TEXT("قدّام"), TEXT("إمشي لقدّام"), TEXT("9oddem"), TEXT("emchi l9oddem") }, EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Back"),    { TEXT("لتالي"), TEXT("ارجع"), TEXT("وخّر"), TEXT("ltali"), TEXT("arja3"), TEXT("wakhar") }, EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Left"),    { TEXT("على ليسار"), TEXT("دور ليسار"), TEXT("امشي ليسار"), TEXT("3al lisar"), TEXT("imchi lisar"), TEXT("a gauche") }, EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Right"),   { TEXT("على ليمين"), TEXT("دور ليمين"), TEXT("امشي ليمين"), TEXT("3al limin"), TEXT("imchi limin"), TEXT("a droite") }, EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Jump"),    { TEXT("اُنڨز"), TEXT("onguez") }, EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Stop"),    { TEXT("أُقف"), TEXT("حبس"), TEXT("بركا"), TEXT("o9ef"), TEXT("barka"), TEXT("stop") }, EPhraseRecognitionTolerance::VE_5),
	};

	// Modern Standard Arabic, written once each, in Arabic script only
	Arabic = {
		MakeCommand(TEXT("Forward"), { TEXT("تقدّم"), TEXT("إلى الأمام") }, EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Back"),    { TEXT("ارجع"), TEXT("إلى الخلف") }, EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Left"),    { TEXT("يسارا"), TEXT("انعطف يسارا") }, EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Right"),   { TEXT("يمينا"), TEXT("انعطف يمينا") }, EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Jump"),    { TEXT("اقفز") }, EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Stop"),    { TEXT("توقف"), TEXT("قف") }, EPhraseRecognitionTolerance::VE_5),
	};

	// No language: invented words, written as they sound. A command may also mix
	// languages, as Stop does.
	Spells = {
		MakeCommand(TEXT("Forward"), { TEXT("velaris") }, EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Back"),    { TEXT("zelthar morun") }, EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Left"),    { TEXT("sanarel vitae") }, EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Right"),   { TEXT("kraveth ulnar") }, EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Jump"),    { TEXT("amorix valor corva aeterna") }, EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Stop"),    { TEXT("silentium"), TEXT("stop"), TEXT("o9ef"), TEXT("قف") }, EPhraseRecognitionTolerance::VE_5),
	};
}

const TArray<FVoiceCommand>& USphinxSampleSettings::CommandsFor(const FString& LanguageCode) const
{
	if (LanguageCode.Equals(TEXT("aeb"), ESearchCase::IgnoreCase))
	{
		return Tunisian;
	}
	if (LanguageCode.Equals(TEXT("ar"), ESearchCase::IgnoreCase))
	{
		return Arabic;
	}
	if (LanguageCode.Equals(TEXT("any"), ESearchCase::IgnoreCase))
	{
		return Spells;
	}
	return English;
}
