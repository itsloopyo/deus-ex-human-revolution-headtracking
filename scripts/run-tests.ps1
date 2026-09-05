[CmdletBinding()]
param([string]$Config = 'Debug')
$ErrorActionPreference = 'Stop'
$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$BuildDir = Join-Path $ProjectRoot 'build-tests'

cmake -B $BuildDir -A Win32 -DDXHR_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

foreach ($target in 'dxhr_config_sanitize_tests', 'dxhr_ads_tests') {
    cmake --build $BuildDir --config $Config --target $target
    if ($LASTEXITCODE -ne 0) { throw "Test build failed for $target ($LASTEXITCODE)" }
}

ctest --test-dir $BuildDir -C $Config --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)" }

Write-Host 'All tests passed' -ForegroundColor Green
