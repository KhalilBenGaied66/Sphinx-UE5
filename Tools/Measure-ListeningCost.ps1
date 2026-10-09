<#
.SYNOPSIS
    Measures the CPU time the plugin adds while it listens and nobody speaks.

.DESCRIPTION
    Starts a headless editor on a project, leaves it idle, then starts keyword
    recognition on the default microphone and leaves it listening. The process's
    CPU time is read in both phases; the difference is the plugin's.

    The measurement only uses the plugin's Blueprint API, so the same script can
    be pointed at a project that uses an older copy of the plugin (-Project).
    The project needs the English model in Content/model/en and a recording
    device. Nothing is recorded or kept.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Tools\Measure-ListeningCost.ps1
#>
[CmdletBinding()]
param(
    [string]$Engine = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Project,
    [int]$IdleSeconds = 15,
    [int]$ListenSeconds = 30
)

$ErrorActionPreference = 'Stop'
$tools = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Project) { $Project = Join-Path (Split-Path -Parent $tools) 'SphinxSample.uproject' }
$Project = (Resolve-Path $Project).Path
$saved = Join-Path (Split-Path -Parent $Project) 'Saved\SpeechRecognition'
New-Item -ItemType Directory -Force -Path $saved | Out-Null

$output = Join-Path $saved 'listening-cost.json'
if (Test-Path $output) { Remove-Item $output }
$env:SPHINX_MEASURE_IDLE = "$IdleSeconds"
$env:SPHINX_MEASURE_LISTEN = "$ListenSeconds"
$env:SPHINX_MEASURE_OUTPUT = $output

$script = (Join-Path $tools 'measure_listening_cost.py') -replace '\\', '/'
$log = Join-Path $saved 'listening-cost.log'
$editor = Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$arguments = @("`"$Project`"", "-ExecCmds=`"py $script`"", '-unattended', '-nopause', '-nosplash', '-nullrhi', '-nosound', '-stdout', '-FullStdOutLogOutput')
$process = Start-Process -FilePath $editor -ArgumentList $arguments -NoNewWindow -Wait -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"

if (-not (Test-Path $output)) {
    throw "No measurement was written (exit code $($process.ExitCode), see $log)."
}
$result = Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
'Idle:      {0:P1} of one core over {1} s' -f $result.idle.cores, $result.idle.seconds
'Listening: {0:P1} of one core over {1} s' -f $result.listening.cores, $result.listening.seconds
'Added by listening: {0:P1} of one core' -f $result.added_by_listening_cores
if (-not $result.started) { 'Warning: recognition did not start; see the log.' }
"Details: $output"
