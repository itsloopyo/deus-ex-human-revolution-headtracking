#!/usr/bin/env pwsh
#Requires -Version 5.1
# Fully unattended release workflow for DeusExHumanRevolutionHeadTracking.
# Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>
#
# Running this command IS the authorization. There is no second gate: the
# release runs end to end with zero prompts. The preconditions below (clean
# tree, on main, tag absent, valid semver) are the safety net in place of
# any interactive confirmation - each fails fast with a non-zero exit.

[CmdletBinding()]
param(
    [Parameter(Position=0)]
    [string]$Version,
    [switch]$AllowDirty,
    # Ship a release even when there are no user-facing commits since the
    # last tag (writes a maintenance changelog entry instead of aborting).
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# THIRD-PARTY-NOTICES.md names the cameraunlock-core commit compiled into the
# release ZIPs, and bumping the submodule does not touch it. Packaging refuses
# to ship that mismatch, so a bump with no notices edit stopped the release
# here, or in CI once the tag had already been pushed. Re-sync it and let this
# release carry the correction.
$noticesRoot = Split-Path -Parent $PSScriptRoot
& git -C $noticesRoot diff --quiet -- THIRD-PARTY-NOTICES.md
if ($LASTEXITCODE -ne 0) { throw "THIRD-PARTY-NOTICES.md has uncommitted edits. Commit or discard them, then re-run." }
& (Join-Path $noticesRoot 'cameraunlock-core\scripts\sync-core-notices.ps1') -Repo $noticesRoot
if ($LASTEXITCODE -ne 0) { throw "sync-core-notices.ps1 exited $LASTEXITCODE - fix THIRD-PARTY-NOTICES.md before releasing." }
& git -C $noticesRoot diff --quiet -- THIRD-PARTY-NOTICES.md
if ($LASTEXITCODE -ne 0) {
    & git -C $noticesRoot commit -q -m 'chore: record the cameraunlock-core commit this build compiles' -- THIRD-PARTY-NOTICES.md
    if ($LASTEXITCODE -ne 0) { throw "Could not commit the re-synced THIRD-PARTY-NOTICES.md." }
    Write-Host 'THIRD-PARTY-NOTICES.md re-synced to the pinned cameraunlock-core commit.' -ForegroundColor Yellow
}

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (-not $Version) {
    Write-Error "Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>"
    exit 1
}

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1') -AllowDirty:$AllowDirty
    exit $LASTEXITCODE
}

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force

# Mirrors New-ChangelogFromCommits' insertion so a -Force maintenance entry
# lands in the same place with the same shape.
function Add-MaintenanceChangelogEntry {
    param([string]$Path, [string]$NewVersion)
    $date = Get-Date -Format 'yyyy-MM-dd'
    $entry = "## [$NewVersion] - $date`n`n### Changed`n`n- Maintenance release (no user-facing changes).`n`n"
    $changelog = Get-Content $Path -Raw
    if ($changelog -match '(?s)(# Changelog.*?)(## \[)') {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n\n)', "`$1$entry"
    } else {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n)', "`$1$entry"
    }
    $changelog = $changelog.TrimEnd() + "`n"
    Set-Content $Path $changelog -NoNewline
}

function Write-NoBom {
    param([string]$Path, [string]$Text)
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding $false))
}

# --- 1. Resolve and validate the target version ------------------------
$cmakePath = Join-Path $ProjectRoot 'CMakeLists.txt'
$cmakeText = Get-Content $cmakePath -Raw
if ($cmakeText -notmatch 'project\(DeusExHumanRevolutionHeadTracking VERSION (\d+\.\d+\.\d+)') {
    Write-Error "Could not parse current version from CMakeLists.txt"
    exit 1
}
$current = $Matches[1]

if ($Version -in @('major', 'minor', 'patch')) {
    $target = Get-NextVersion -Current $current -Bump $Version
} else {
    $target = $Version
}

if (-not (Test-SemVer -Version $target)) {
    Write-Error "Not a valid semver: $target"
    exit 1
}

# --- 2. Preconditions (these stand in for interactive confirmation) ----
$branch = (git -C $ProjectRoot rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne 'main') {
    Write-Error "Releases must run on 'main' (currently on '$branch')."
    exit 1
}

if (-not $AllowDirty) {
    $status = git -C $ProjectRoot status --porcelain
    if ($status) {
        Write-Error "Working tree is not clean. Commit or stash changes before releasing."
        exit 1
    }
}

$tag = "v$target"
if (git -C $ProjectRoot tag --list $tag) {
    Write-Error "Tag $tag already exists."
    exit 1
}

Write-Host "Releasing $current -> $target" -ForegroundColor Cyan

# --- 3. Changelog from commits since the last tag ----------------------
# This is the gate that aborts when there are no user-facing commits, so run
# it BEFORE mutating any version files or building - a failure here then
# leaves a clean tree instead of stranding a half-applied version bump with
# no tag.
$changelogPath = Join-Path $ProjectRoot 'CHANGELOG.md'
Write-Host "Generating CHANGELOG..." -ForegroundColor Cyan
$prevTag = Get-PreviousTag -ProjectRoot $ProjectRoot
if (-not $prevTag) {
    # First release - ensure a baseline CHANGELOG exists
    if (-not (Test-Path $changelogPath)) {
        $date = Get-Date -Format 'yyyy-MM-dd'
        Write-NoBom -Path $changelogPath -Text "# Changelog`n`n## [$target] - $date`n`nFirst release.`n"
    }
} else {
    try {
        $entry = New-ChangelogFromCommits -Version $target -ProjectRoot $ProjectRoot -PreviousTag $prevTag
        Update-ChangelogFile -ChangelogPath $changelogPath -NewEntry $entry
    } catch {
        if (-not $Force) {
            Write-Error "$($_.Exception.Message)"
            Write-Host "No user-facing changes to release. Re-run with -Force for a maintenance release." -ForegroundColor Yellow
            exit 1
        }
        Write-Host "No user-facing commits since last tag - writing maintenance entry (-Force)." -ForegroundColor Yellow
        Add-MaintenanceChangelogEntry -Path $changelogPath -NewVersion $target
    }
}

# --- 4. Bump the canonical version (CMakeLists.txt) + keep pixi.toml in sync
$cmakeText = $cmakeText -replace 'project\(DeusExHumanRevolutionHeadTracking VERSION \d+\.\d+\.\d+', "project(DeusExHumanRevolutionHeadTracking VERSION $target"
Write-NoBom -Path $cmakePath -Text $cmakeText

$pixiPath = Join-Path $ProjectRoot 'pixi.toml'
$pixiText = Get-Content $pixiPath -Raw
$pixiText = $pixiText -replace '(?m)^version = "\d+\.\d+\.\d+"', "version = `"$target`""
Write-NoBom -Path $pixiPath -Text $pixiText

# install.cmd's MOD_VERSION is what the install writes into the launcher's
# state file, which is where the launcher looks to spot a stale install.
$installCmdPath = Join-Path $ProjectRoot 'scripts\install.cmd'
$installCmdText = Get-Content $installCmdPath -Raw
if ($installCmdText -notmatch 'set "MOD_VERSION=[^"]+"') { throw "MOD_VERSION line not found in $installCmdPath" }
$installCmdText = $installCmdText -replace 'set "MOD_VERSION=[^"]+"', "set `"MOD_VERSION=$target`""
Write-NoBom -Path $installCmdPath -Text $installCmdText

# --- 5. Release-config build -------------------------------------------
Write-Host "Building release configuration..." -ForegroundColor Cyan
pixi run build-release
if ($LASTEXITCODE -ne 0) {
    Write-Error "Release build failed."
    exit 1
}

# --- 6. Commit the version bump + changelog ----------------------------
git -C $ProjectRoot add CMakeLists.txt pixi.toml scripts/install.cmd CHANGELOG.md
git -C $ProjectRoot commit -m "Release v$target"
if ($LASTEXITCODE -ne 0) { Write-Error "git commit failed."; exit 1 }

# --- 7. Annotated tag --------------------------------------------------
git -C $ProjectRoot tag -a $tag -m "Release v$target"
if ($LASTEXITCODE -ne 0) { Write-Error "git tag failed."; exit 1 }

# --- 8. Push commits + tag (triggers .github/workflows/release.yml) ----
git -C $ProjectRoot push origin HEAD
if ($LASTEXITCODE -ne 0) { Write-Error "git push (commits) failed."; exit 1 }
git -C $ProjectRoot push origin $tag
if ($LASTEXITCODE -ne 0) { Write-Error "git push (tag) failed."; exit 1 }

Write-Host "Released $tag" -ForegroundColor Green
