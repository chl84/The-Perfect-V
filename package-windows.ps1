param([string]$ShaderDirectory = "$PSScriptRoot/build-windows/shaders")
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
Set-Location $PSScriptRoot
$build = "$PSScriptRoot/build-windows"
$prefix = "$build/dependencies"
cmake -S packaging/windows/dependencies -B "$build/dependency-build" -A x64
cmake --build "$build/dependency-build" --config Release --parallel 4
cmake --install "$build/dependency-build" --config Release --prefix $prefix
cmake -S . -B "$build/game" -A x64 -DCMAKE_PREFIX_PATH="$prefix" `
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DTPV_PRECOMPILED_SHADER_DIR="$ShaderDirectory" `
    -DSJP_REFERENCE_TESTS=OFF
cmake --build "$build/game" --config Release --parallel 4
$env:PATH = "$prefix/bin;$env:PATH"
ctest --test-dir "$build/game" -C Release --output-on-failure
$stage = "$build/package/ThePerfectV"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
cmake --install "$build/game" --config Release --prefix $stage
Copy-Item "$prefix/bin/SDL3.dll" $stage
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$dumpbin = & $vswhere -latest -products '*' -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe' | Select-Object -Last 1
if (!$dumpbin) { throw "Visual Studio dumpbin was not found" }
foreach ($binary in @("$stage/the-perfect-v.exe", "$stage/SDL3.dll")) {
    $imports = & $dumpbin /DEPENDENTS $binary
    $imports | Write-Output
    if ($imports -match '(?i)(VCRUNTIME|MSVCP|MSVCR)[0-9]+.*\.dll') {
        throw "Unexpected Visual C++ runtime DLL dependency in $binary"
    }
}
New-Item -ItemType Directory -Force "$stage/docs/third-party" | Out-Null
Copy-Item "$build/dependency-build/_deps/sdl-src/LICENSE.txt" "$stage/docs/third-party/SDL3-LICENSE.txt"
Copy-Item "$build/dependency-build/_deps/freetype-src/docs/FTL.TXT" "$stage/docs/third-party/FreeType-LICENSE.txt"
python packaging/windows/package.py $stage "$build/dependency-build/downloads"
