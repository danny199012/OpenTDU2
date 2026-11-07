# Thin wrapper for the HUB workspace (branch `win`) - the tree whose exe keeps
# the plain name `build\bin\opentdu2.exe` that CLAUDE.md, the launch\*.bat files
# and the owner's own copy expect. Pass -PlainExe so the name does not change.
#
# For the forward-looking per-tree name (`opentdu2-win.exe`) call build.ps1 with
# no arguments instead, or `build.ps1 -Suffix -win` explicitly.
#
# Run from anywhere:  powershell -NoProfile -ExecutionPolicy Bypass -File build-hub.ps1
$ErrorActionPreference = 'Continue'
$script = Join-Path $PSScriptRoot 'build.ps1'
& $script -PlainExe -BuildDir 'build' @args
exit $LASTEXITCODE
