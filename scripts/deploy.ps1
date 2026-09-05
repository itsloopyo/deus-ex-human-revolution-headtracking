#!/usr/bin/env pwsh
#Requires -Version 5.1
# Thin wrapper - dev-deploy orchestration lives in
# cameraunlock-core/powershell/DevDeploy.psm1.

param(
    [Parameter(Mandatory=$false, Position=0)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [Parameter(Mandatory=$false, Position=1)]
    [string]$GivenPath,
    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$RemainingArgs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = 'SilentlyContinue'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectRoot "cameraunlock-core\powershell\DevDeploy.psm1") -Force
Import-Module (Join-Path $projectRoot "cameraunlock-core\powershell\ModDeployment.psm1") -Force

$buildOutput = Join-Path $projectRoot "bin\$Configuration"
$vendorLoader = Join-Path $projectRoot 'vendor\ultimate-asi-loader\dinput8.dll'

$result = Invoke-DevDeployASILoader `
    -GameId 'deus-ex-human-revolution' `
    -GameDisplayName "Deus Ex: Human Revolution - Director's Cut" `
    -BuildOutputPath $buildOutput `
    -ModDllName 'DeusExHumanRevolutionHeadTracking.asi' `
    -VendorLoaderDll $vendorLoader `
    -AsiLoaderName 'winmm.dll' `
    -GivenPath $GivenPath

Write-DeploymentSuccess `
    -ModName "Head Tracking mod" `
    -DeployPath $result.DeployedDllPath `
    -Controls @(
        "End       - Toggle head tracking on/off",
        "Page Up   - Cycle tracking mode (rotation+position / rotation-only / position-only)",
        "Page Down - Toggle yaw mode (world / local)",
        "Insert    - Cycle ADS mode (paused / marker / tracked)",
        "",
        "No nav cluster? Chords: Ctrl+Shift+ Y=Toggle G=Mode H=Yaw U=ADS"
    )
