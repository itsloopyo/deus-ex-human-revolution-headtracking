#!/usr/bin/env pwsh
#Requires -Version 5.1
# Remove the third-party DLLs Ultimate ASI Loader carries as RCDATA resources
# from the vendored copy we redistribute.
#
# The upstream Win32 loader embeds three complete DLLs so that a user who
# renames it over one of those libraries still gets the original exports, plus
# the ini template one of them reads:
#
#   binkw32.dll    RAD Game Tools, Inc., "Bink and Smacker" 1.994i. Proprietary
#                  middleware licensed per title. We have no right to
#                  redistribute it, and the release ZIPs are a redistribution.
#   wndmode.dll    "DirectX Windower Embedded" v2.3, (C) 2008 VEG,
#                  (C) 2004 menopem. No licence accompanies it.
#   vorbisfile.dll Xiph.Org. Redistributable under BSD-3-Clause, but only with
#                  its notice, and we neither use it nor want the obligation.
#
# We deploy the loader as winmm.dll and ship no wndmode.ini, so none of the
# three is reachable in this mod: the two library payloads are keyed off the
# loader's own filename and the windower off that ini. Zeroing them changes
# nothing a user of this mod can observe, and takes the whole question of
# whether we may redistribute them off the table.
#
# The bytes are zeroed in place rather than the resource entries deleted, so
# every offset in the PE stays exactly where the upstream build put it and the
# loader's own code is untouched. MIT permits the modification; it is recorded
# in vendor/ultimate-asi-loader/README.md and THIRD-PARTY-NOTICES.md so the
# copy is not mistaken for stock upstream.
#
#   scripts/strip-loader-payload.ps1 -Path vendor/ultimate-asi-loader/dinput8.dll
#   scripts/strip-loader-payload.ps1 -Path <same> -VerifyOnly   # release gate

param(
    [Parameter(Mandatory = $true)][string]$Path,
    [switch]$VerifyOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Path)) { throw "Loader not found: $Path" }
# .NET resolves a relative path against the PROCESS working directory, which is
# not PowerShell's current location. Pin it to one absolute path here: without
# this, Test-Path can pass on the file you meant while ReadAllBytes reads a
# different one, and the write below then lands on a third.
$Path = (Resolve-Path -LiteralPath $Path).ProviderPath
$bytes = [System.IO.File]::ReadAllBytes($Path)

function Get-U16([byte[]]$b, [int]$o) { [BitConverter]::ToUInt16($b, $o) }
function Get-U32([byte[]]$b, [int]$o) { [BitConverter]::ToUInt32($b, $o) }

if ($bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) { throw "$Path is not a PE image (no MZ)." }
$pe = [int](Get-U32 $bytes 0x3C)
if ((Get-U32 $bytes $pe) -ne 0x00004550) { throw "$Path is not a PE image (no PE signature)." }

$numSections = [int](Get-U16 $bytes ($pe + 6))
$optSize     = [int](Get-U16 $bytes ($pe + 20))
$optMagic    = Get-U16 $bytes ($pe + 24)
$dirBase     = $pe + 24 + $(if ($optMagic -eq 0x20B) { 112 } else { 96 })

$sections = @()
$secBase = $pe + 24 + $optSize
for ($i = 0; $i -lt $numSections; $i++) {
    $s = $secBase + $i * 40
    $sections += [pscustomobject]@{
        VirtualSize    = Get-U32 $bytes ($s + 8)
        VirtualAddress = Get-U32 $bytes ($s + 12)
        RawSize        = Get-U32 $bytes ($s + 16)
        RawOffset      = Get-U32 $bytes ($s + 20)
    }
}

function Convert-RvaToOffset([uint32]$rva) {
    foreach ($s in $sections) {
        $span = [Math]::Max($s.VirtualSize, $s.RawSize)
        if ($rva -ge $s.VirtualAddress -and $rva -lt ($s.VirtualAddress + $span)) {
            return [int]($s.RawOffset + ($rva - $s.VirtualAddress))
        }
    }
    throw "RVA 0x$($rva.ToString('x')) is outside every section."
}

$rsrcRva = Get-U32 $bytes ($dirBase + 2 * 8)
if ($rsrcRva -eq 0) { throw "$Path has no resource directory." }
$rsrcBase = Convert-RvaToOffset $rsrcRva

# Walk type -> name -> language and collect the RCDATA (type 10) leaves.
function Get-ResourceLeaves([int]$dirOffset, [int]$level, [uint32]$typeId) {
    $named = [int](Get-U16 $bytes ($dirOffset + 12))
    $ids   = [int](Get-U16 $bytes ($dirOffset + 14))
    $out = @()
    for ($i = 0; $i -lt ($named + $ids); $i++) {
        $entry = $dirOffset + 16 + $i * 8
        $nameField = Get-U32 $bytes $entry
        $dataField = Get-U32 $bytes ($entry + 4)
        $thisType = $(if ($level -eq 0) { $nameField } else { $typeId })
        if (($dataField -band 0x80000000) -ne 0) {
            $child = $rsrcBase + [int]($dataField -band 0x7FFFFFFF)
            $out += Get-ResourceLeaves $child ($level + 1) $thisType
        } elseif ($level -ge 1 -and $typeId -eq 10) {
            $leaf = $rsrcBase + [int]$dataField
            $out += [pscustomobject]@{
                Offset = Convert-RvaToOffset (Get-U32 $bytes $leaf)
                Size   = [int](Get-U32 $bytes ($leaf + 4))
                Id     = $nameField
            }
        }
    }
    return $out
}

$payloads = @()
foreach ($leaf in (Get-ResourceLeaves $rsrcBase 0 0)) {
    if ($leaf.Size -lt 2) { continue }
    $head = [System.Text.Encoding]::ASCII.GetString($bytes, $leaf.Offset, [Math]::Min(12, $leaf.Size))
    $what = $null
    if ($head.StartsWith('MZ')) { $what = 'embedded DLL' }
    elseif ($head.StartsWith('[WINDOWMODE]')) { $what = 'windower ini template' }
    if ($what) { $payloads += [pscustomobject]@{ Offset = $leaf.Offset; Size = $leaf.Size; What = $what } }
}

if ($payloads.Count -eq 0) {
    Write-Host "  loader payload: already stripped (no third-party RCDATA)" -ForegroundColor DarkGray
    exit 0
}

if ($VerifyOnly) {
    foreach ($p in $payloads) {
        Write-Host ("  {0} at 0x{1:x} ({2:N0} bytes)" -f $p.What, $p.Offset, $p.Size) -ForegroundColor Red
    }
    throw "$Path still carries $($payloads.Count) third-party RCDATA payload(s) (binkw32.dll, wndmode.dll, vorbisfile.dll). Run: pixi run strip-loader"
}

$total = 0
foreach ($p in $payloads) {
    [Array]::Clear($bytes, $p.Offset, $p.Size)
    $total += $p.Size
    Write-Host ("  stripped {0} at 0x{1:x} ({2:N0} bytes)" -f $p.What, $p.Offset, $p.Size) -ForegroundColor Yellow
}
[System.IO.File]::WriteAllBytes($Path, $bytes)
Write-Host ("  {0:N0} bytes of third-party payload removed from {1}" -f $total, (Split-Path $Path -Leaf)) -ForegroundColor Green
