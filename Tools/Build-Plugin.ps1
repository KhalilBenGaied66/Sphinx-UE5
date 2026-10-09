<#
.SYNOPSIS
    Packages the plugin with RunUAT BuildPlugin, for one engine version or several.

.DESCRIPTION
    BuildPlugin compiles the plugin on its own (editor, game, shipping) against an
    installed engine and writes a copy with binaries to the output folder. It is the
    check that the plugin builds outside the sample project, and the way to make a
    copy to drop into a Blueprint-only project.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Tools\Build-Plugin.ps1 -Version 5.5,5.6,5.8
#>
[CmdletBinding()]
param(
    [string[]]$Version = @('5.8'),
    [string]$EngineRoot = 'C:\Program Files\Epic Games',
    [string]$Output
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Output) { $Output = Join-Path $root 'Saved\Packages' }
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$plugin = Join-Path $root 'Plugins\SpeechRecognition\SpeechRecognition.uplugin'

$Version = @($Version | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$failed = 0
foreach ($v in $Version) {
    $uat = Join-Path $EngineRoot "UE_$v\Engine\Build\BatchFiles\RunUAT.bat"
    if (-not (Test-Path $uat)) { "UE {0}: not installed under {1}" -f $v, $EngineRoot; $failed++; continue }

    $package = Join-Path $Output "SpeechRecognition-UE$v"
    $log = Join-Path $Output "SpeechRecognition-UE$v.log"
    $process = Start-Process -FilePath $uat -ArgumentList @('BuildPlugin', "-Plugin=`"$plugin`"", "-Package=`"$package`"", '-TargetPlatforms=Win64', '-Rocket') `
        -NoNewWindow -Wait -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"

    $succeeded = [bool](Select-String -Path $log -Pattern 'BUILD SUCCESSFUL' -Quiet)
    $warnings = @(Select-String -Path $log -Pattern 'warning C\d+').Count
    'UE {0}: {1}, {2} compiler warning(s)   ({3})' -f $v, $(if ($succeeded) { 'built' } else { 'FAILED' }), $warnings, $log
    if (-not $succeeded) {
        $failed++
        Select-String -Path $log -Pattern ' error ' | Select-Object -First 15 | ForEach-Object { '    ' + $_.Line }
    }
}
if ($failed -gt 0) { exit 1 }
