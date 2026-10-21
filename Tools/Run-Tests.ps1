<#
.SYNOPSIS
    Builds the sample project and runs the plugin's automated tests in a headless editor.

.DESCRIPTION
    -Filter SpeechRecognition   the tests (default)
    -Filter SpeechBenchmark     the benchmark; its report lands in Saved/SpeechRecognition/benchmark.md

    The Subsystem.Live test opens the default microphone for a few seconds. Nothing
    is recorded or kept.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Tools\Run-Tests.ps1
    powershell -ExecutionPolicy Bypass -File Tools\Run-Tests.ps1 -Filter SpeechBenchmark -NoBuild
    powershell -ExecutionPolicy Bypass -File Tools\Run-Tests.ps1 -Filter SpeechRecognition.Transcriber -NoBuild -OrtThreads 4
#>
[CmdletBinding()]
param(
    [string]$Engine = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Filter = 'SpeechRecognition',
    [switch]$NoBuild,
    # Also write the decoder's own log lines (verbose: several hundred per model load)
    [switch]$DecoderLog,
    # ONNX Runtime intra-op threads for the Darija model in the editor (default: ONNX Runtime's own choice)
    [int]$OrtThreads = -1
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$project = Join-Path $root 'SphinxSample.uproject'
$saved = Join-Path $root 'Saved\SpeechRecognition'
New-Item -ItemType Directory -Force -Path $saved | Out-Null

# Runs a program with its output in a file. Start-Process, because Windows
# PowerShell 5.1 turns anything a native program writes to stderr into an error.
function Invoke-Logged([string]$FilePath, [string[]]$Arguments, [string]$Log) {
    $process = Start-Process -FilePath $FilePath -ArgumentList $Arguments -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $Log -RedirectStandardError "$Log.err"
    return $process.ExitCode
}

if (-not $NoBuild) {
    $buildLog = Join-Path $saved 'build.log'
    $code = Invoke-Logged (Join-Path $Engine 'Engine\Build\BatchFiles\Build.bat') @('SphinxSampleEditor', 'Win64', 'Development', "-Project=`"$project`"", '-WaitMutex', '-NoHotReload') $buildLog
    if ($code -ne 0) {
        Select-String -Path $buildLog -Pattern 'error ' | Select-Object -First 30 | ForEach-Object { $_.Line }
        throw "Build failed (see $buildLog)."
    }
    'Build succeeded.'
}

$log = Join-Path $saved "tests-$Filter.log"
$report = Join-Path $saved "report-$Filter"
if (Test-Path $report) { Remove-Item -Recurse -Force $report }

$editor = Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$arguments = @("`"$project`"", "-ExecCmds=`"Automation RunTests $Filter;Quit`"", '-TestExit="Automation Test Queue Empty"', "-ReportExportPath=`"$report`"",
    '-unattended', '-nopause', '-nosplash', '-nullrhi', '-nosound', '-stdout', '-FullStdOutLogOutput')
if ($DecoderLog) { $arguments += '-LogCmds="SpeechRecognitionPlugin Verbose"' }
if ($OrtThreads -ge 0) {
    $arguments += "-ini:Engine:[/Script/NNERuntimeORT.NNERuntimeORTSettings]:EditorThreadingOptions=(bUseGlobalThreadPool=False,IntraOpNumThreads=$OrtThreads,InterOpNumThreads=1,ExecutionMode=SEQUENTIAL)"
}
$code = Invoke-Logged $editor $arguments $log

$index = Join-Path $report 'index.json'
if (-not (Test-Path $index)) {
    throw "The editor wrote no test report (exit code $code, see $log)."
}

$results = Get-Content -LiteralPath $index -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($test in $results.tests) {
    '{0,-8} {1}' -f $test.state, $test.fullTestPath
    foreach ($entry in $test.entries) {
        $type = $entry.event.type
        if ($type -eq 'Error' -or $type -eq 'Info') {
            '         {0}: {1}' -f $type, $entry.event.message
        }
    }
}
''
'{0} passed, {1} with warnings, {2} failed, {3} not run   ({4:N1} s)' -f $results.succeeded, $results.succeededWithWarnings, $results.failed, $results.notRun, $results.totalDuration
"Log: $log"
if ($results.failed -gt 0) { exit 1 }
