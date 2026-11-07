# seed-deps-cache.ps1 — seed the shared third-party dependency cache.
#
# cmake/Dependencies.cmake clones 13 repos from GitHub (SDL, Jolt, imgui, volk,
# VMA, Vulkan-Headers, SPIRV-Headers, stb, earcut, dr_libs, ogg, opus, opusfile)
# every time a *new* build dir is
# configured - minutes of network, per workspace. Its own header documents the
# offline route: pass -DOTDU_DEPS_DIR=<folder> with those repos pre-cloned.
#
# This script builds that folder by copying the sources CMake has already
# downloaded into an existing build dir. The three workspaces then share one
# 347 MB copy and a fresh configure makes no network access at all.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\seed-deps-cache.ps1
#
# Re-run it any time; folders that already exist are skipped. The cache lives
# outside every repo (C:\otdu2-deps by default), so deleting it costs nothing but
# a re-clone - it is never a source of truth.
param(
    [string] $From = '',                        # a build dir's _deps folder to copy from
    [string] $To   = 'C:\otdu2-deps'            # the shared cache
)

$ErrorActionPreference = 'Continue'

# FetchContent name -> folder name Dependencies.cmake looks for -> '<name>-src' on disk
$map = [ordered]@{
    'SDL'            = 'sdl3-src'
    'volk'           = 'volk-src'
    'VMA'            = 'vma-src'
    'Vulkan-Headers' = 'vulkan_headers-src'
    'SPIRV-Headers'  = 'spirv_headers-src'
    'JoltPhysics'    = 'jolt-src'
    'imgui'          = 'imgui-src'
    'stb'            = 'stb-src'
    'earcut'         = 'earcut-src'
    'dr_libs'        = 'dr_libs-src'
    'ogg'            = 'ogg-src'
    'opus'           = 'opus-src'
    'opusfile'       = 'opusfile-src'
}

if (-not $From) {
    $candidates = @(
        'C:\OpenTDU2-cline\build-cline\_deps',
        'C:\OpenTDU2-glm\build-glm\_deps',
        'C:\OpenTDU2\build\_deps'
    )
    $From = ($candidates | Where-Object { Test-Path (Join-Path $_ 'sdl3-src') } | Select-Object -First 1)
}
if (-not $From) { Write-Error 'no configured build dir with a _deps folder found - pass -From'; exit 1 }
Write-Host "source : $From"
Write-Host "cache  : $To"
New-Item -ItemType Directory -Force -Path $To | Out-Null

foreach ($folder in $map.Keys) {
    $src = Join-Path $From $map[$folder]
    $dst = Join-Path $To $folder
    if (-not (Test-Path $src)) { Write-Warning "missing source: $src"; continue }
    if (Test-Path $dst)        { Write-Host "already cached, skipping: $folder"; continue }
    Write-Host "=== copying $($map[$folder]) -> $folder ==="
    robocopy $src $dst /E /NFL /NDL /NJH /NJS /NP /MT:8 | Out-Null
    Write-Host "robocopy exit $LASTEXITCODE (0-7 is success)"
}

Write-Host '=== cache contents ==='
Get-ChildItem $To -Directory | ForEach-Object {
    $mb = (Get-ChildItem $_.FullName -Recurse -File -Force -ErrorAction SilentlyContinue |
           Measure-Object Length -Sum).Sum / 1MB
    "{0,-18} {1,8:N1} MB" -f $_.Name, $mb
}
$total = (Get-ChildItem $To -Recurse -File -Force -ErrorAction SilentlyContinue |
          Measure-Object Length -Sum).Sum / 1MB
Write-Host ("total: {0:N1} MB" -f $total)
Write-Host '=== done ==='
