[CmdletBinding()]
param([string]$Config = 'Debug')
$ErrorActionPreference = 'Stop'
$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$BuildDir = Join-Path $ProjectRoot 'build-tests'

# The differential test is only as good as its claim about what it compiled.
$provenance = Join-Path $ProjectRoot 'tests/config_differential/provenance.txt'
foreach ($line in Get-Content $provenance) {
    if ($line -match '^\s*(#|$)') { continue }
    $hash, $path = ($line -split '\s+', 3)[0, 1]
    $actual = (Get-FileHash -Algorithm SHA256 (Join-Path $ProjectRoot $path)).Hash.ToLowerInvariant()
    if ($actual -ne $hash) { throw "$path has changed: sha256 $actual, provenance.txt records $hash" }
}

cmake -B $BuildDir -A Win32 -DDXHR_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

foreach ($target in 'dxhr_config_sanitize_tests', 'dxhr_ads_tests', 'dxhr_config_differential_tests') {
    cmake --build $BuildDir --config $Config --target $target
    if ($LASTEXITCODE -ne 0) { throw "Test build failed for $target ($LASTEXITCODE)" }
}

ctest --test-dir $BuildDir -C $Config --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)" }

Write-Host 'All tests passed' -ForegroundColor Green
