# QOS Unreal benchmark runner: launches the four arms back to back in -game mode
# and collects one CSV per run under <Project>/Saved/QOSBench.
#
# Usage (PowerShell):
#   .\run_bench.ps1 -Project "C:\...\Otuken.uproject" -Map "/Game/RealBiomes/Maps/RealBiomes_Pine_Example_Storm" -Runs 3
#
# Requires the Otuken module with the QOS bench subsystem built for the editor target.

param(
    [Parameter(Mandatory = $true)] [string] $Project,
    [Parameter(Mandatory = $true)] [string] $Map,
    [string] $Engine = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe",
    [string] $QosModel = "$PSScriptRoot\..\..\qos-core\artifacts\otuken_v2\qos_model.onnx",
    [string] $ClassicalModel = "$PSScriptRoot\..\..\qos-core\artifacts\ablation_logistic\qos_model.onnx",
    [string[]] $Arms = @("baseline", "threshold", "classical", "qos"),
    [int] $Runs = 3,
    [int] $Duration = 60,
    [int] $Warmup = 10,
    [int] $ResX = 3840,
    [int] $ResY = 2160,
    [int] $StressLights = 128,
    [int] $StressMeshes = 96,
    [int] $Pool = 1500
)

# Copy the models next to the project: Unreal reads the command line as ANSI on
# Windows, so a repo path with non-ASCII characters would not resolve.
$ModelDir = Join-Path (Split-Path $Project) "QOSModels"
New-Item -ItemType Directory -Force $ModelDir | Out-Null
Copy-Item (Resolve-Path $QosModel).Path (Join-Path $ModelDir "qos_active.onnx") -Force
Copy-Item (Resolve-Path $ClassicalModel).Path (Join-Path $ModelDir "classical_active.onnx") -Force
$QosModel = Join-Path $ModelDir "qos_active.onnx"
$ClassicalModel = Join-Path $ModelDir "classical_active.onnx"
$OutDir = Join-Path (Split-Path $Project) "Saved\QOSBench"

Write-Host "QOS bench | map $Map | arms $($Arms -join ', ') | $Runs run(s) x ${Duration}s"
Write-Host "output -> $OutDir"

for ($run = 1; $run -le $Runs; $run++) {
    foreach ($arm in $Arms) {
        $model = ""
        if ($arm -eq "qos") { $model = "-QOSModel=`"$QosModel`"" }
        if ($arm -eq "classical") { $model = "-QOSModel=`"$ClassicalModel`"" }

        $args = @(
            "`"$Project`"",
            "${Map}?game=/Script/Engine.GameModeBase",
            "-game", "-windowed", "-ResX=$ResX", "-ResY=$ResY",
            "-nosplash", "-unattended", "-log",
            "-QOSArm=$arm", "-QOSRun=$run", "-QOSDuration=$Duration", "-QOSWarmup=$Warmup", "-QOSStressLights=$StressLights", "-QOSStressMeshes=$StressMeshes", "-QOSPool=$Pool"
        )
        if ($model) { $args += $model }

        Write-Host ("[{0}] run {1}: {2}" -f (Get-Date -Format HH:mm:ss), $run, $arm)
        $p = Start-Process -FilePath $Engine -ArgumentList $args -PassThru -Wait
        Write-Host ("        exit {0}" -f $p.ExitCode)
        Start-Sleep -Seconds 3
    }
}

Write-Host "`nAnalyzing..."
$csvs = @()
foreach ($arm in $Arms) { $csvs += (Get-ChildItem $OutDir -Filter "${arm}_run*.csv" | Select-Object -ExpandProperty FullName) }
python "$PSScriptRoot\..\analyze.py" $csvs
