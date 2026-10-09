#pragma once

#include "CoreMinimal.h"

/** Process-wide plumbing shared by every decoder: where PocketSphinx's own log lines go. */
namespace SphinxRuntime
{
	/** Routes PocketSphinx's log into the plugin's log category. Call once the DLLs are loaded. */
	void InstallLogCallback();

	/** Restores PocketSphinx's default logging. Call before the DLLs are unloaded. */
	void RemoveLogCallback();

	/** Forgets the last error line, so that the next one belongs to the operation about to start. */
	void ClearLastError();

	/** The last line PocketSphinx logged as an error since ClearLastError, or an empty string. */
	FString GetLastError();
}
