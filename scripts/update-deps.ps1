#!/usr/bin/env pwsh
#Requires -Version 5.1
# Bump vendored Ultimate ASI Loader (dinput8.dll) to the latest upstream
# within the pinned range, strip the third-party DLLs it carries as resources,
# and rewrite vendor/ultimate-asi-loader/{LICENSE,README.md}.
# Manual: dev runs this when they want a fresh upstream bump, then commits the
# result. CI never refreshes.
#
# Special case: Ultimate-ASI-Loader ships a DLL inside a release zip, not as a
# standalone asset. We extract dinput8.dll and vendor it directly so install.cmd
# can copy it straight into the game install root as winmm.dll.
#
# The extracted DLL is NOT vendored as it comes: the x86 build embeds binkw32.dll
# (RAD Game Tools, proprietary), wndmode.dll (VEG / menopem, no licence) and
# vorbisfile.dll (Xiph.Org) as RCDATA resources, and the installer ZIP we publish
# would redistribute all three. strip-loader-payload.ps1 zeroes them before the
# copy is hashed and committed. Never skip that step.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$module = Join-Path $projectDir 'cameraunlock-core/powershell/ModLoaderSetup.psm1'
if (-not (Test-Path $module)) {
    throw "ModLoaderSetup.psm1 not found at $module. Run 'pixi run sync' to update the cameraunlock-core submodule."
}
Import-Module $module -Force

$vendorAsiDir = Join-Path $projectDir 'vendor/ultimate-asi-loader'
$vendorAsiDll = Join-Path $vendorAsiDir 'dinput8.dll'
if (-not (Test-Path $vendorAsiDir)) {
    New-Item -ItemType Directory -Path $vendorAsiDir -Force | Out-Null
}

# Deus Ex: Human Revolution DC is a 32-bit Crystal Engine game. The 32-bit ASI loader is shipped inside
# the Ultimate-ASI-Loader_x86.zip release asset.
$tempZip = Join-Path $env:TEMP ("asi-update-" + [IO.Path]::GetRandomFileName() + ".zip")
try {
    Write-Host "Refreshing vendor/ultimate-asi-loader from upstream..." -ForegroundColor Cyan
    $meta = Invoke-FetchLatestLoader `
        -OutputPath $tempZip `
        -Owner 'ThirteenAG' -Repo 'Ultimate-ASI-Loader' `
        -VersionPrefix 'v9.' `
        -AssetPattern '^Ultimate-ASI-Loader\.zip$'

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($tempZip)
    try {
        $dllEntry = $zip.Entries | Where-Object { $_.Name -eq 'dinput8.dll' } | Select-Object -First 1
        if (-not $dllEntry) { throw "Upstream zip $($meta.AssetName) does not contain dinput8.dll." }
        $out = [System.IO.File]::Create($vendorAsiDll)
        try { $in = $dllEntry.Open(); try { $in.CopyTo($out) } finally { $in.Dispose() } } finally { $out.Dispose() }

        $licenseEntry = $zip.Entries | Where-Object { $_.Name -match '^(license|LICENSE)(\..+)?$' -and $_.FullName -notmatch '/.+/' } | Select-Object -First 1
        if ($licenseEntry) {
            $out = [System.IO.File]::Create((Join-Path $vendorAsiDir 'LICENSE'))
            try { $in = $licenseEntry.Open(); try { $in.CopyTo($out) } finally { $in.Dispose() } } finally { $out.Dispose() }
        }
    } finally { $zip.Dispose() }

    if (-not (Test-Path (Join-Path $vendorAsiDir 'LICENSE'))) {
        $licenseUrl = "https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/$($meta.Tag)/license"
        Invoke-WebRequest -Uri $licenseUrl -OutFile (Join-Path $vendorAsiDir 'LICENSE') -UseBasicParsing -TimeoutSec 30 -Headers @{ "User-Agent" = "CameraUnlock-HeadTracking" }
    }

    $upstreamSha = (Get-FileHash -Path $vendorAsiDll -Algorithm SHA256).Hash.ToLower()

    Write-Host "Stripping the loader's embedded third-party DLLs..." -ForegroundColor Cyan
    $strip = Join-Path $scriptDir 'strip-loader-payload.ps1'
    & $strip -Path $vendorAsiDll
    & $strip -Path $vendorAsiDll -VerifyOnly   # throws if anything survived

    $dllSha = (Get-FileHash -Path $vendorAsiDll -Algorithm SHA256).Hash.ToLower()
    $readme = @(
        '# Ultimate ASI Loader (vendored)',
        '',
        'Bundled copy of Ultimate ASI Loader (x86), the install-time source of truth.',
        'Refresh manually with `pixi run update-deps`, then commit.',
        '',
        '## Snapshot',
        '',
        '- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader',
        "- Tag: ``$($meta.Tag)``",
        "- Commit: ``$($meta.CommitSha)``",
        "- Asset: ``$($meta.AssetName)``",
        "- Upstream dinput8.dll SHA-256: ``$upstreamSha``",
        "- Vendored dinput8.dll SHA-256: ``$dllSha`` (after the strip below)",
        "- Fetched at: $($meta.FetchedAt)",
        '',
        'install.cmd copies `dinput8.dll` to the game install root (next to DXHRDC.exe) as',
        '`winmm.dll` (a DLL DXHRDC.exe imports directly, so the ASI loader engages at process',
        'start).',
        '',
        '## Modified: third-party payload stripped',
        '',
        'The upstream x86 loader carries three complete third-party DLLs as RCDATA resources,',
        'so that a user who renames it over one of those libraries still gets the original',
        'exports, plus the ini template one of them reads:',
        '',
        '- `binkw32.dll` - RAD Game Tools, Inc., Bink and Smacker 1.994i. Proprietary',
        '  middleware licensed per title; we have no right to redistribute it.',
        '- `wndmode.dll` - DirectX Windower Embedded v2.3, (C) 2008 VEG, (C) 2004 menopem.',
        '  No licence accompanies it.',
        '- `vorbisfile.dll` - Xiph.Org, BSD-3-Clause. Redistributable only with its notice.',
        '',
        '`scripts/strip-loader-payload.ps1` zeroes all three, and the windower ini template,',
        'before the file is committed. Only the `.rsrc` section changes: the loader code, its',
        'imports, relocations and appended PDB are byte-identical to upstream. Nothing in this',
        'mod can reach the stripped resources - the two library payloads are keyed off the',
        "loader's own filename, and we deploy it as `winmm.dll`, while the windower needs a",
        '`wndmode.ini` we never ship. MIT permits the modification; it is recorded here and in',
        'THIRD-PARTY-NOTICES.md so this copy is not mistaken for stock upstream.'
    ) -join "`n"
    Set-Content -Path (Join-Path $vendorAsiDir 'README.md') -Value $readme -Encoding UTF8

    Write-Host "  tag=$($meta.Tag) sha256=$($dllSha.Substring(0,12))..." -ForegroundColor DarkGray
} finally {
    Remove-Item $tempZip -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "vendor/ultimate-asi-loader refreshed. Review and commit." -ForegroundColor Green
