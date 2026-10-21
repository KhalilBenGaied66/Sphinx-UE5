<#
.SYNOPSIS
    Downloads CMUSphinx speech models and lays them out the way the plugin expects.

.DESCRIPTION
    Models are not kept in this repository: they are large, and each has its own
    licence. This script fetches them from where their authors publish them.

      en  US English   acoustic model: CMUSphinx (BSD-style)   dictionary: CMUdict (BSD-style)
      fr  French       acoustic model and dictionary: CMUSphinx project (see the LICENSE it downloads)
      de  German       acoustic model, dictionary, language model: VoxForge / Zamia (LGPL 3)
      omnilingual  the second engine (PocketSphinx has no Arabic model): Omnilingual ASR
          CTC 300M (Meta, Apache-2.0), int8 ONNX export by OpenVoiceOS, run by Unreal's
          Neural Network Engine. Its training corpus includes Modern Standard Arabic,
          Tunisian (aeb_Arab), Moroccan, Egyptian, Levantine and others. One folder
          serves the code "any" (any phrase, in any alphabet, invented words
          included) and every Arabic code: ar, aeb, ary, arz, apc... (any, arabic,
          ar and aeb are accepted here as other names for it).

    Result, for each language code <c>, under Content\model:

      <c>\<c>\                       acoustic model
      <c>\<c>.dict                   pronunciation dictionary
      <c>\grammars\                  put your JSGF grammars here (a sample is copied for en)
      <c>\language_models\<c>.lm.bin n-gram model, only with -LanguageModel

    Download sizes: en 14 MB (+27 MB with the language model), fr 9 MB (+17 MB),
    de 44 MB (its language model comes in the same archive), omnilingual 328 MB.

    The omnilingual folder holds model.int8.onnx, tokens.txt and the model card
    instead of an acoustic model and a dictionary.

    Any other CMUSphinx model works too: give it the same layout under a folder of
    your choosing and pass that folder's name to Init With Language Code.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Tools\Get-Models.ps1
    powershell -ExecutionPolicy Bypass -File Tools\Get-Models.ps1 -Language en,de -LanguageModel
#>
[CmdletBinding()]
param(
    [string[]]$Language = @('en'),
    [string]$Destination,
    [switch]$LanguageModel,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

# "powershell -File ... -Language en,de" hands the list over as one string: split it here
$Language = @($Language | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().ToLowerInvariant() } | Where-Object { $_ })
# every Arabic code shares one model folder
$Language = @($Language | ForEach-Object { if (@('any', 'arabic', 'aeb', 'ar') -contains $_) { 'omnilingual' } else { $_ } } | Select-Object -Unique)
foreach ($code in $Language) {
    if (@('en', 'fr', 'de', 'omnilingual') -notcontains $code) { throw "No download recipe for '$code'. This script knows en, fr, de and omnilingual; see docs/models.md for other languages." }
}
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Destination) { $Destination = Join-Path $root 'Content\model' }
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$Destination = (Resolve-Path $Destination).Path

$sourceForge = 'https://sourceforge.net/projects/cmusphinx/files/Acoustic%20and%20Language%20Models'
$pocketSphinx = 'https://raw.githubusercontent.com/cmusphinx/pocketsphinx/last-pre-1.0/model/en-us'
# a fixed revision, so that every download is the model the measurements used
$omnilingual = 'https://huggingface.co/OpenVoiceOS/omnilingual-asr-ctc-300m-onnx/resolve/bc55e1de19af8a907b0a010d06fc4637a2d5bf0c'

function Get-File([string]$Url, [string]$Target) {
    # curl.exe ships with Windows 10 and later, and follows SourceForge's mirror redirects.
    # A mirror drops the connection now and then: try a few times before giving up.
    # A transfer that crawls below 2 KB/s for 20 seconds is abandoned and started again.
    foreach ($attempt in 1..4) {
        & curl.exe --silent --location --fail --connect-timeout 20 --speed-limit 2000 --speed-time 20 --output $Target $Url
        if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $Target)) { return }
        Start-Sleep -Seconds (2 * $attempt)
    }
    throw "Download failed: $Url"
}

function Expand-Archive2([string]$Archive, [string]$Target) {
    # tar.exe (bsdtar) reads gzip and xz alike, whatever the file is called
    New-Item -ItemType Directory -Force -Path $Target | Out-Null
    & tar.exe -xf $Archive -C $Target
    if ($LASTEXITCODE -ne 0) { throw "Could not extract $Archive" }
}

$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("sphinx-models-" + [System.Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force -Path $temp | Out-Null

try {
    foreach ($code in $Language) {
        $target = Join-Path $Destination $code

        if ($code -eq 'omnilingual') {
            # a transcription model, not a PocketSphinx one
            if ((Test-Path (Join-Path $target 'model.int8.onnx')) -and -not $Force) {
                "$code already installed in $target (use -Force to download it again)."
                continue
            }
            New-Item -ItemType Directory -Force -Path $target | Out-Null
            "Downloading the Omnilingual ASR model (328 MB)..."
            Get-File "$omnilingual/model.int8.onnx" (Join-Path $target 'model.int8.onnx')
            Get-File "$omnilingual/tokens.txt" (Join-Path $target 'tokens.txt')
            Get-File "$omnilingual/README.md" (Join-Path $target 'MODEL_CARD.md')
            $size = (Get-ChildItem $target -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
            "{0}: installed in {1} ({2:N0} MB)" -f $code, $target, $size
            continue
        }

        $acoustic = Join-Path $target $code
        if ((Test-Path (Join-Path $acoustic 'mdef')) -and -not $Force) {
            "$code already installed in $target (use -Force to download it again)."
            continue
        }
        if (Test-Path $target) { Remove-Item -Recurse -Force $target }
        New-Item -ItemType Directory -Force -Path $acoustic, (Join-Path $target 'grammars') | Out-Null
        $languageModels = Join-Path $target 'language_models'

        switch ($code) {
            'en' {
                "Downloading the US English model..."
                $archive = Join-Path $temp 'en.tar.gz'
                Get-File "$sourceForge/US%20English/cmusphinx-en-us-ptm-5.2.tar.gz/download" $archive
                Expand-Archive2 $archive (Join-Path $temp 'en')
                Copy-Item (Join-Path $temp 'en\cmusphinx-en-us-ptm-5.2\*') $acoustic -Recurse
                Get-File "$pocketSphinx/cmudict-en-us.dict" (Join-Path $target 'en.dict')
                if ($LanguageModel) {
                    New-Item -ItemType Directory -Force -Path $languageModels | Out-Null
                    Get-File "$pocketSphinx/en-us.lm.bin" (Join-Path $languageModels 'en.lm.bin')
                }
                # a grammar to try grammar mode with: Enable Grammar Mode "move"
                Copy-Item (Join-Path $root 'Plugins\SpeechRecognition\Resources\TestAudio\grammars\en_move.gram') (Join-Path $target 'grammars\move.gram')
            }
            'fr' {
                "Downloading the French model..."
                $archive = Join-Path $temp 'fr.tar.gz'
                Get-File "$sourceForge/French/cmusphinx-fr-ptm-5.2.tar.gz/download" $archive
                Expand-Archive2 $archive (Join-Path $temp 'fr')
                Copy-Item (Join-Path $temp 'fr\cmusphinx-fr-ptm-5.2\*') $acoustic -Recurse
                Get-File "$sourceForge/French/fr.dict/download" (Join-Path $target 'fr.dict')
                Get-File "$sourceForge/French/LICENSE/download" (Join-Path $target 'LICENSE')
                if ($LanguageModel) {
                    New-Item -ItemType Directory -Force -Path $languageModels | Out-Null
                    Get-File "$sourceForge/French/fr-small.lm.bin/download" (Join-Path $languageModels 'fr.lm.bin')
                }
            }
            'de' {
                "Downloading the German model..."
                $archive = Join-Path $temp 'de.tar'
                Get-File "$sourceForge/German/cmusphinx-de-voxforge-5.2.tar.gz/download" $archive
                Expand-Archive2 $archive (Join-Path $temp 'de')
                $extracted = (Get-ChildItem (Join-Path $temp 'de') -Directory | Select-Object -First 1).FullName
                Copy-Item (Join-Path $extracted 'model_parameters\voxforge.cd_cont_6000\*') $acoustic -Recurse
                Copy-Item (Join-Path $extracted 'etc\voxforge.dic') (Join-Path $target 'de.dict')
                Copy-Item (Join-Path $extracted 'LICENSE') (Join-Path $target 'LICENSE')
                if ($LanguageModel) {
                    New-Item -ItemType Directory -Force -Path $languageModels | Out-Null
                    Copy-Item (Join-Path $extracted 'etc\voxforge.lm.bin') (Join-Path $languageModels 'de.lm.bin')
                }
            }
        }

        if (-not (Test-Path (Join-Path $acoustic 'mdef')) -or -not (Test-Path (Join-Path $target "$code.dict"))) {
            throw "The $code model did not come out as expected in $target."
        }
        $size = (Get-ChildItem $target -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
        "{0}: installed in {1} ({2:N0} MB)" -f $code, $target, $size
    }
}
finally {
    Remove-Item -Recurse -Force $temp -ErrorAction SilentlyContinue
}

''
"Model folder: $Destination"
'The models keep the licences of their authors. Read them before you ship a game with one.'
