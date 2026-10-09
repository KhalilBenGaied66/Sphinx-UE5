<#
.SYNOPSIS
    Fetches recorded German speech for the German tests and benchmark.

.DESCRIPTION
    Windows rarely has a German text-to-speech voice, so German test audio cannot be
    synthesised the way English and French are. This script downloads one set of
    recordings from VoxForge instead: 99 sentences read by one speaker
    (ralfherzog-20080214-de115, (C) 2008 Ralf Herzog, GPL 3), and converts the ones
    the test manifest lists to 16 kHz, 16-bit, mono WAV.

    The recordings stay on your machine, under Resources/TestAudio/generated/de
    (ignored by git). Needs ffmpeg on the PATH; download size 7 MB.

    Mind what they can show: these recordings are part of the corpus the German
    model was trained on. They prove that German works from end to end. They say
    little about how well it will do with another voice.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Tools\Get-TestSpeech.ps1
#>
[CmdletBinding()]
param(
    [string]$AudioRoot,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
if (-not $AudioRoot) {
    $AudioRoot = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) '..\Plugins\SpeechRecognition\Resources\TestAudio'
}
$AudioRoot = (Resolve-Path $AudioRoot).Path

if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
    throw 'ffmpeg was not found on the PATH. Install it (for example: winget install Gyan.FFmpeg) and run this script again.'
}

$rows = Get-Content -LiteralPath (Join-Path $AudioRoot 'utterances.tsv') -Encoding UTF8 | ConvertFrom-Csv -Delimiter "`t" |
    Where-Object { $_.language -eq 'de' -and $_.source -eq 'recorded' }
$missing = $rows | Where-Object { $Force -or -not (Test-Path -LiteralPath (Join-Path $AudioRoot ($_.file -replace '/', '\'))) }
if (-not $missing) {
    "All {0} German recordings are already there." -f @($rows).Count
    return
}

$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("sphinx-speech-" + [System.Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force -Path $temp | Out-Null
try {
    $archive = Join-Path $temp 'de115.tgz'
    'Downloading ralfherzog-20080214-de115 from VoxForge...'
    & curl.exe --silent --show-error --location --fail --output $archive 'https://repository.voxforge1.org/downloads/de/Trunk/Audio/Main/16kHz_16bit/ralfherzog-20080214-de115.tgz'
    if ($LASTEXITCODE -ne 0) { throw 'Download failed.' }
    & tar.exe -xf $archive -C $temp
    if ($LASTEXITCODE -ne 0) { throw 'Could not extract the archive.' }

    $written = 0
    foreach ($row in $missing) {
        $target = Join-Path $AudioRoot ($row.file -replace '/', '\')
        $source = Join-Path $temp ('ralfherzog-20080214-de115\flac\' + [System.IO.Path]::GetFileNameWithoutExtension($target) + '.flac')
        New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
        # Start-Process: Windows PowerShell 5.1 treats what ffmpeg writes to stderr as an error
        $process = Start-Process -FilePath 'ffmpeg' -ArgumentList @('-y', '-loglevel', 'error', '-i', "`"$source`"", '-ar', '16000', '-ac', '1', '-sample_fmt', 's16', "`"$target`"") -NoNewWindow -Wait -PassThru
        if ($process.ExitCode -ne 0) { throw "ffmpeg could not convert $source" }
        $written++
    }
    Copy-Item (Join-Path $temp 'ralfherzog-20080214-de115\etc\GPL_License') (Join-Path $AudioRoot 'generated\de\LICENSE-voxforge.txt') -Force
    "{0} recordings written under {1}" -f $written, (Join-Path $AudioRoot 'generated\de')
}
finally {
    Remove-Item -Recurse -Force $temp -ErrorAction SilentlyContinue
}
