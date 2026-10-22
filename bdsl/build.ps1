param([switch]$SkipCore, [switch]$Package)
$ErrorActionPreference = 'Stop'
$bdslSource = Split-Path -Parent $PSScriptRoot
$bdslBuild = Join-Path $bdslSource 'out/build/bdsl'
$bdslCoreFlag = if ($SkipCore) { 'OFF' } else { 'ON' }
cmake -S $bdslSource -B $bdslBuild -DBLACKSMITH_BUILD_BDSL=ON -DBLACKSMITH_BUILD_BINDINGS=OFF "-DBDSL_TEST_CORE=$bdslCoreFlag" -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed. On Windows use a Visual Studio Developer PowerShell.' }
$bdslTargets = @('bdslc', 'bdsl-compiler-test')
if (-not $SkipCore) { $bdslTargets += @('bdsl-core-test', 'bdsl-features-test') }
cmake --build $bdslBuild --config Release --target @bdslTargets --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
ctest --test-dir $bdslBuild -C Release -R '^bdsl-' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
if ($Package) {
    cmake --build $bdslBuild --config Release --target bdsl-vsix
    if ($LASTEXITCODE -ne 0) { throw 'Packaging failed' }
}
