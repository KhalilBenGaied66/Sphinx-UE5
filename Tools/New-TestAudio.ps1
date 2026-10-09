<#
.SYNOPSIS
    Synthesises the test utterances listed in utterances.tsv with the Windows speech voices.

.DESCRIPTION
    The plugin's tests and benchmark need audio. One recording ships with the
    repository (from the PocketSphinx test suite). The rest is generated here, on
    your machine, with the text-to-speech voices Windows already has, so that no
    audio of unclear licence is committed.

    Output: 16 kHz, 16-bit, mono WAV files under
    Plugins/SpeechRecognition/Resources/TestAudio/generated/ (ignored by git).

    Rows whose language has no installed voice are skipped and listed at the end.
    Synthetic speech is clean and regular: the figures it produces are a regression
    baseline, not a measure of how the recogniser does with real players.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Tools\New-TestAudio.ps1
#>
[CmdletBinding()]
param(
    [string]$AudioRoot,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech

if (-not $AudioRoot) {
    # $PSScriptRoot is still empty while Windows PowerShell 5.1 binds parameter defaults
    $AudioRoot = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) '..\Plugins\SpeechRecognition\Resources\TestAudio'
}
$AudioRoot = (Resolve-Path $AudioRoot).Path
$manifest = Join-Path $AudioRoot 'utterances.tsv'
$rows = Get-Content -LiteralPath $manifest -Encoding UTF8 | ConvertFrom-Csv -Delimiter "`t"

$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
$format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)

# language code -> the culture of the voice to use
$cultures = @{ en = 'en-US'; fr = 'fr-FR'; de = 'de-DE'; es = 'es-ES'; ru = 'ru-RU' }
$voices = @{}
foreach ($voice in $synth.GetInstalledVoices()) {
    if ($voice.Enabled) { $voices[$voice.VoiceInfo.Culture.Name] = $voice.VoiceInfo.Name }
}

$written = 0; $kept = 0; $skipped = @{}
foreach ($row in $rows) {
    if ($row.source -ne 'synthetic') { continue }

    $culture = $cultures[$row.language]
    if (-not $culture -or -not $voices.ContainsKey($culture)) {
        $skipped[$row.language] = 1 + [int]$skipped[$row.language]
        continue
    }

    $target = Join-Path $AudioRoot ($row.file -replace '/', '\')
    if ((Test-Path -LiteralPath $target) -and -not $Force) { $kept++; continue }

    New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
    $synth.SelectVoice($voices[$culture])
    $synth.Rate = [int]$row.rate
    $synth.SetOutputToWaveFile($target, $format)
    $synth.Speak($row.text)
    $synth.SetOutputToNull()
    $written++
}
$synth.Dispose()

"{0} files written, {1} already there." -f $written, $kept
"Voices used: " + (($voices.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name) = $($_.Value)" }) -join '; ')
foreach ($language in $skipped.Keys) {
    "Skipped {0} '{1}' utterances: no Windows voice for {2}. Add one in Settings > Time & language > Speech." -f $skipped[$language], $language, $cultures[$language]
}
