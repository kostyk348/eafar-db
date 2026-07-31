# Build + run tests in one shot (MinGW toolchain).
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$toolchain = "C:\Users\Lab\Documents\toolchains\winlibs\mingw64\bin"
$env:Path = "$toolchain;$env:Path"

cmake -S $root -B "$root\build" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build "$root\build"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& "$root\build\eafardb_tests.exe"
exit $LASTEXITCODE
