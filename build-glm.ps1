# Thin wrapper kept for habit: build the GLM workspace (branch `glm`) as
# build-glm\bin\opentdu2-glm.exe. The real work is in build.ps1, which takes its
# folder from $PSScriptRoot - so this file is safe in any folder name.
#
# Run from anywhere:  powershell -NoProfile -ExecutionPolicy Bypass -File build-glm.ps1
$ErrorActionPreference = 'Continue'
$script = Join-Path $PSScriptRoot 'build.ps1'
& $script -Suffix '-glm' -BuildDir 'build-glm' @args
exit $LASTEXITCODE
