# Thin wrapper kept for habit: build the CLINE workspace (branch `cline`) as
# build-cline\bin\opentdu2-cline.exe. The real work is in build.ps1, which takes
# its folder from $PSScriptRoot - so this file is safe in any folder name.
#
# Run from anywhere:  powershell -NoProfile -ExecutionPolicy Bypass -File build-cline.ps1
$ErrorActionPreference = 'Continue'
$script = Join-Path $PSScriptRoot 'build.ps1'
& $script -Suffix '-cline' -BuildDir 'build-cline' @args
exit $LASTEXITCODE

