# Ultimate ASI Loader (vendored)

Bundled copy of Ultimate ASI Loader (x86), the install-time source of truth.
Refresh manually with `pixi run update-deps`, then commit.

## Snapshot

- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader
- Tag: `v9.7.2`
- Commit: `ab722befd52581a34449b603926cfab476e66b05`
- Asset: `Ultimate-ASI-Loader.zip`
- Upstream dinput8.dll SHA-256: `c7277e832f6f07af64903a99ecebab2936260cbf55eda70787c5d7b2d5b9fe60`
- Vendored dinput8.dll SHA-256: `867f4cbc96cc2a13d1bd9bf717a6d84c07e0ff72c1a8c171d00b2b8a506ebe90` (after the strip below)
- Fetched at: 2026-08-20T09:11:00.7724092+01:00

install.cmd copies `dinput8.dll` to the game install root (next to DXHRDC.exe) as
`winmm.dll` (a DLL DXHRDC.exe imports directly, so the ASI loader engages at process
start).

## Modified: third-party payload stripped

The upstream x86 loader carries three complete third-party DLLs as RCDATA resources,
so that a user who renames it over one of those libraries still gets the original
exports, plus the ini template one of them reads:

- `binkw32.dll` - RAD Game Tools, Inc., Bink and Smacker 1.994i. Proprietary
  middleware licensed per title; we have no right to redistribute it.
- `wndmode.dll` - DirectX Windower Embedded v2.3, (C) 2008 VEG, (C) 2004 menopem.
  No licence accompanies it.
- `vorbisfile.dll` - Xiph.Org, BSD-3-Clause. Redistributable only with its notice.

`scripts/strip-loader-payload.ps1` zeroes all three, and the windower ini template,
before the file is committed. Only the `.rsrc` section changes: the loader code, its
imports, relocations and appended PDB are byte-identical to upstream. Nothing in this
mod can reach the stripped resources - the two library payloads are keyed off the
loader's own filename, and we deploy it as `winmm.dll`, while the windower needs a
`wndmode.ini` we never ship. MIT permits the modification; it is recorded here and in
THIRD-PARTY-NOTICES.md so this copy is not mistaken for stock upstream.
