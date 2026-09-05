#!/usr/bin/env pwsh
#Requires -Version 5.1
# Custom packaging for Deus Ex: Human Revolution Head Tracking (C++ project, no .csproj).
# Produces two ZIPs:
#   - DeusExHumanRevolutionHeadTracking-v{version}-installer.zip (GitHub Release)
#   - DeusExHumanRevolutionHeadTracking-v{version}-nexus.zip     (Nexus, extract to game folder)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectDir "cameraunlock-core\powershell\ReleaseWorkflow.psm1") -Force

$cmakeLists = Get-Content (Join-Path $projectDir 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch 'project\(DeusExHumanRevolutionHeadTracking VERSION (\d+\.\d+\.\d+)') {
    throw "Could not parse version from CMakeLists.txt"
}
$version = $Matches[1]
$modName = 'DeusExHumanRevolutionHeadTracking'

Write-Host ""
Write-Host "=== Packaging $modName v$version ===" -ForegroundColor Magenta
Write-Host ""

$releaseDir = Join-Path $projectDir 'release'
if (-not (Test-Path $releaseDir)) { New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null }

$asiPath = Join-Path $projectDir "bin/Release/$modName.asi"
if (-not (Test-Path $asiPath)) {
    throw "$modName.asi not found at: $asiPath. Run 'pixi run build-release' first."
}

$vendorAsiDir = Join-Path $projectDir 'vendor/ultimate-asi-loader'
$vendorAsiDll = Join-Path $vendorAsiDir 'dinput8.dll'
if (-not (Test-Path $vendorAsiDll)) {
    throw "Bundled ASI loader missing: $vendorAsiDll. Run 'pixi run update-deps' first."
}

# The installer ZIP redistributes that binary, and the upstream x86 loader carries
# binkw32.dll (RAD Game Tools, proprietary), wndmode.dll and vorbisfile.dll as
# RCDATA resources. None of the three is ours to ship, so a loader that still
# has them never reaches a release. See vendor/ultimate-asi-loader/README.md.
& (Join-Path $scriptDir 'strip-loader-payload.ps1') -Path $vendorAsiDll -VerifyOnly

$scriptsDir = Join-Path $projectDir 'scripts'
foreach ($s in @('install.cmd', 'uninstall.cmd')) {
    if (-not (Test-Path (Join-Path $scriptsDir $s))) {
        throw "Required script not found: $s"
    }
}

# --- Installer ZIP -----------------------------------------------------
Write-Host '--- Installer ZIP ---' -ForegroundColor Yellow

$ghStaging = Join-Path $releaseDir 'staging-installer'
if (Test-Path $ghStaging) { Remove-Item -Recurse -Force $ghStaging }
New-Item -ItemType Directory -Path $ghStaging -Force | Out-Null

foreach ($s in @('install.cmd', 'uninstall.cmd')) {
    Copy-Item (Join-Path $scriptsDir $s) -Destination $ghStaging -Force
}

# install.cmd and uninstall.cmd are thin wrappers: the body they call lives in
# shared/ at the ZIP root, and without it the installer aborts at its own layout
# check and exits 1 on every run. Copy-SharedBundle stages every body there,
# alongside find-game.ps1, GamePathDetection.psm1 and games.json at the paths
# find-game.ps1 actually looks in.
Copy-SharedBundle -StagingDir $ghStaging

$pluginsDir = Join-Path $ghStaging 'plugins'
New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null
Copy-Item $asiPath -Destination $pluginsDir -Force

# Ultimate ASI Loader is MIT: its notice has to travel with the binary, so a
# missing LICENSE here is fatal rather than a silent skip. Shipping the DLL
# without it is a licence breach that nothing downstream would catch.
$ghVendorDir = Join-Path $ghStaging 'vendor/ultimate-asi-loader'
New-Item -ItemType Directory -Path $ghVendorDir -Force | Out-Null
foreach ($vendorFile in @('dinput8.dll', 'LICENSE', 'README.md')) {
    $src = Join-Path $vendorAsiDir $vendorFile
    if (-not (Test-Path $src)) {
        throw "Vendored ASI loader file not found: $vendorFile. It ships beside dinput8.dll and its licence must travel with it. Run 'pixi run update-deps'."
    }
    Copy-Item $src -Destination $ghVendorDir -Force
}

# Same rule as the Nexus ZIP below: the installer ZIP is a binary distribution,
# so every notice it owes is a hard requirement, not a best effort.
foreach ($doc in @('README.md', 'LICENSE', 'CHANGELOG.md', 'THIRD-PARTY-NOTICES.md')) {
    $docPath = Join-Path $projectDir $doc
    if (-not (Test-Path $docPath)) {
        throw "Required document not found: $doc. Every published ZIP is a binary distribution and must carry it."
    }
    Copy-Item -Path $docPath -Destination $ghStaging -Force
}

$installerZip = Join-Path $releaseDir "$modName-v$version-installer.zip"
if (Test-Path $installerZip) { Remove-Item $installerZip -Force }
Push-Location $ghStaging
try { Compress-Archive -Path '.\*' -DestinationPath $installerZip -Force } finally { Pop-Location }
Remove-Item -Recurse -Force $ghStaging

$installerKb = [math]::Round((Get-Item $installerZip).Length / 1KB, 1)
Write-Host ("  $installerZip ({0:N1} KB)" -f $installerKb) -ForegroundColor Green

# --- Nexus ZIP ---------------------------------------------------------
Write-Host ''
Write-Host '--- Nexus ZIP ---' -ForegroundColor Yellow

$nexusStaging = Join-Path $releaseDir 'staging-nexus'
if (Test-Path $nexusStaging) { Remove-Item -Recurse -Force $nexusStaging }

# Nexus users manage their own ASI loader, so the nexus ZIP ships only the
# mod's .asi - never the vendored dinput8.dll. DXHRDC.exe sits at the game
# root, so the .asi extracts straight there.
$nexusGameDir = $nexusStaging
New-Item -ItemType Directory -Path $nexusGameDir -Force | Out-Null

Copy-Item $asiPath -Destination $nexusGameDir -Force

$nexusZip = Join-Path $releaseDir "$modName-v$version-nexus.zip"
if (Test-Path $nexusZip) { Remove-Item $nexusZip -Force }
# The Nexus ZIP is a binary distribution too: the licences of everything
# compiled into or bundled with the payload require their notices to travel
# with it, so LICENSE and THIRD-PARTY-NOTICES.md ship at its root.
foreach ($noticeDoc in @('LICENSE', 'THIRD-PARTY-NOTICES.md', 'README.md')) {
    $noticeSrc = Join-Path $projectDir $noticeDoc
    if (-not (Test-Path $noticeSrc)) {
        throw "Required notice file not found: $noticeDoc. Every published ZIP is a binary distribution and must carry it."
    }
    Copy-Item $noticeSrc -Destination $nexusStaging -Force
    Write-Host "  $noticeDoc" -ForegroundColor Green
}
Push-Location $nexusStaging
try { Compress-Archive -Path '.\*' -DestinationPath $nexusZip -Force } finally { Pop-Location }
Remove-Item -Recurse -Force $nexusStaging

$nexusKb = [math]::Round((Get-Item $nexusZip).Length / 1KB, 1)
Write-Host ("  $nexusZip ({0:N1} KB)" -f $nexusKb) -ForegroundColor Green

Write-Host ''
Write-Host '=== Package Complete ===' -ForegroundColor Magenta

Write-Output $installerZip
Write-Output $nexusZip
