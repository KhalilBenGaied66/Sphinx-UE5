# Fixes & Improvements over the upstream UE5 fork

> This is the record of the nine fixes made in July 2026, kept as written. The
> code has moved on since (see `CHANGELOG.md` at the root of the repository);
> the note at the end says which of the manual checks below an automated test
> now covers.

Baseline: [ukustra/sphinx-ue4](https://github.com/ukustra/sphinx-ue4) (UE5 fork of
[shanecolb/sphinx-ue4](https://github.com/shanecolb/sphinx-ue4)), source state as of the
"5.8 update" commit. Files changed: `SpeechRecognitionWorker.h/.cpp`,
`SpeechRecognitionSubsystem.h/.cpp`, `SpeechRecognition.h/.cpp`, `ISpeechRecognition.h`.
`Build.cs` and `GameStateBase` are unchanged.

---

## 1. Worker/thread teardown — memory leak and latent double-delete (FIXED)

**Upstream behavior:** `USpeechRecognitionSubsystem::Shutdown()` called
`listenerThread->ShutDown()` and then set `listenerThread = NULL` without ever
deleting the worker object — one `FSpeechRecognitionWorker` leaked per
`Init`/`Shutdown` cycle (i.e., per level travel, since `Deinitialize()` calls
`Shutdown()`). Separately, `ShutDown()` deleted `Thread` without nulling it while
the destructor deleted it again — a latent double-delete masked only by the leak.

**Fix:** `ShutDown()` now joins, deletes, and nulls `Thread` (idempotent); the
destructor simply calls `ShutDown()`; the subsystem's `Shutdown()` deletes the
worker after `ShutDown()` returns. `listenerThread` and all raw pointers are
explicitly nullptr-initialized. `StartThread()` now reports failure
(`Thread != nullptr`), and `Init()` cleans the worker up if the thread failed
to start.

**Test:** start PIE, `Init` + `EnableKeywordMode`, speak, stop PIE, repeat 10x.
No growth in worker threads (check `stat threads` or the thread list in the
debugger); no crash on exit.

## 2. Thread-safety — unguarded shared state between game and worker threads (FIXED)

**Upstream behavior:** `EnableKeywordMode`, `EnableGrammarMode`,
`EnableLanguageModel`, `SetLanguage`, and `SetConfigParam` ran on the game
thread and directly mutated `config`, `keywords`, `sphinxParams`,
`detectionMode`, `initRequired`, and the model paths — all of which the worker
thread reads concurrently in `Run()`. No locks, no atomics: a data race
(undefined behavior) that happened to work on x86.

**Fix:** a `TQueue<TFunction<void()>, EQueueMode::Mpsc>` command queue. The
public methods validate on the calling thread, then enqueue; the worker drains
the queue at the top of every loop iteration, so **all Sphinx state is owned by
exactly one thread**. Public API signatures are unchanged.

**Test:** while recognition is running, switch modes repeatedly at runtime
(keyword → grammar → keyword) from Blueprint on a timer. Upstream this is a
race; now each switch applies cleanly between audio buffers.

## 3. Volume tracking was broken (`adbuf[0]` vs `adbuf[i]`) (FIXED)

**Upstream behavior:** the peak-volume loop gated peak capture on
`adbuf[0] > 0` — the first sample of the buffer — instead of the current
sample, so `GetCurrentVolume()` returned garbage or froze depending on buffer
alignment. It also allocated a `std::vector` per 64 ms audio buffer, and wrote
`currentVolume` from the worker while the game thread read it (another race).

**Fix:** correct per-sample peak logic, no per-buffer heap allocation, and
`currentVolume` is now `std::atomic<int32>` (published with relaxed ordering,
read by `GetCurrentVolume()` on the game thread).

**Test:** print `GetCurrentVolume()` to screen every tick; it should now track
your voice level responsively and sit near 0 in silence.

## 4. Editor hard-crash when DLLs are missing (FIXED)

**Upstream behavior:** `StartupModule()` ran `checkf(false, TEXT("Failed to
load dll"))` if `SphinxBase.dll`/`PocketSphinx.dll` weren't found — killing the
entire editor on startup for anyone who cloned a repo with missing binaries.

**Fix:** the module logs a clear `Error` and records the failure;
`ISpeechRecognition::AreLibrariesLoaded()` exposes it, and
`USpeechRecognitionSubsystem::Init()` fails gracefully (returns `false` with a
log message) before any delay-loaded Sphinx call can crash.

**Test:** temporarily rename `PocketSphinx.dll`, start the editor: it opens
normally, the log shows the error, `Init` returns false. Restore the DLL.

## 5. Memory leaks in keyword setup and config params (FIXED)

**Upstream behavior:** in `Run()`, the `phrases` string array and `tolerances`
array were `malloc`'d and never freed (once per init / mode switch).
`FSpeechRecognitionParam` `new[]`'d two `char` buffers in its constructor with
no destructor — leaked on every `SetConfigParam` call — and was copied into a
`TArray` sharing raw pointers.

**Fix:** the bundled PocketSphinx deep-copies keyphrases and tolerances
(`kws_search_init` uses `ckd_salloc`/`ckd_calloc` — verified in the modified
sources shipped in the original repo), so the temporaries are freed right after
`ps_set_keyphrase`. `FSpeechRecognitionParam` now uses `std::string` — no
manual memory management at all.

## 6. Decoder / audio device leak on runtime mode switch (FIXED)

**Upstream behavior:** switching modes at runtime re-entered the init block,
which called `ps_init` and `ad_open_dev` again without freeing the previous
decoder or closing the previous audio device — leaking both, and potentially
failing to reopen the device.

**Fix:** `ReleaseSphinxResources()` closes the device and frees the decoder
before every re-init, on every init-failure path (which previously leaked and
just returned), and on normal shutdown (which previously called `ad_close` on
a possibly-garbage pointer if init never ran). Config reference counting
follows the library's semantics: `ps_free` releases the decoder's reference,
the worker releases its own exactly once.

## 7. Multi-word keyword validation checked the wrong word (FIXED)

**Upstream behavior:** the dictionary check inside the keyword loop called
`GetOriginalString(splitString[0])` for *every* word of a multi-word phrase —
validating the first word repeatedly instead of each word ("open the door"
validated "open" three times).

**Fix:** `GetOriginalString(*v_It)` — each word is validated, restoring the
original plugin's intent.

## 8. Dangling-pointer hazard in `SetConfigParam` (FIXED)

**Upstream behavior:** `const char* paramName = TCHAR_TO_UTF8(*param)` stored a
pointer into a temporary conversion buffer that is only valid for the enclosing
expression, then used it in later statements.

**Fix:** immediate copies into `std::string`.

## 9. Minor

- `if (!Manager | !ps)` — bitwise OR replaced with logical `||`.
- `StartThread` returns the actual thread-creation result.
- Grammar/LM filename conversion switched from `TCHAR_TO_ANSI` to
  `TCHAR_TO_UTF8` for non-ASCII file names.

---

## Not changed (by design)

- Public Blueprint/C++ API: identical signatures; existing projects work unmodified.
- `SpeechRecognition.Build.cs`, `SpeechRecognitionGameStateBase.*`: untouched.
- The bundled third-party binaries: same modified PocketSphinx/SphinxBase build
  the original plugin has always shipped.

## Full in-engine regression checklist

1. Build under UE 5.6 (Win64, VS2022) with no new warnings in plugin code.
2. Keyword mode: define 3+ phrases incl. one multi-word; each detected; a
   phrase with a non-dictionary word is skipped with the log warning.
3. Grammar mode: `.gram` file loads; phrases detected; `sil` filtered.
4. Language model mode: `.lm` loads; free dictation returns words.
5. Runtime mode switching while listening: no crash, new mode takes effect.
6. `GetCurrentVolume` tracks voice level (fix #3).
7. Missing-DLL start: editor survives, `Init` returns false (fix #4).
8. 10x PIE start/stop: thread count stable, exit clean (fix #1).
9. Level travel (`open <map>`) mid-listen: clean teardown via `Deinitialize`.

---

## October 2026: what is checked automatically now

`Tools/Run-Tests.ps1` runs these in a headless editor (UE 5.8).

| Fix | Automated check | Still by hand |
|---|---|---|
| 1. Worker teardown | `Subsystem.Live`: four start / switch mode / shutdown cycles on the microphone | thread count over many PIE sessions |
| 2. Shared state | The shared fields no longer exist: the worker only receives copies. `Subsystem.Live` switches mode while listening | |
| 3. Volume | `Unit.Volume`: a buffer that starts below zero is measured like any other | the meter on screen with a real voice |
| 4. Missing DLLs | none | rename a DLL and start the editor |
| 5. Keyword and parameter leaks, 6. Decoder leak | `Decoder.Reinitialise`: 40 switches on a kept decoder, then 40 with a full reload, watching the process's memory | |
| 7. Multi-word validation | `Decoder.KeywordValidation`: a phrase whose last word is unknown is rejected | |
| 8. Dangling pointer | The code is gone: parameters are stored as `FString` on the game thread | |
| 9. Logical OR, UTF-8 names | `Decoder.NonAsciiPath`: a model under a folder with accents in its name | |

Checklist items 2 to 5 (the three modes, switching between them) are covered
on audio files by `Decoder.RecordedSpeech` and on the microphone by
`Subsystem.Live`, which opens the device but has nobody speaking into it.
Items 7 (missing DLL) and 9 (level travel while listening) are still manual.
