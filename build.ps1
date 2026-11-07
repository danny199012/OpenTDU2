# Build THIS workspace - whichever folder it happens to live in.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 -Suffix ""      # legacy plain opentdu2.exe
#   powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 -Reconfigure   # force a fresh configure
#
# Nothing is hardcoded: the source folder is wherever this script is, and the
# build dir + the executable name come from the checked-out branch, so the
# folder may be renamed (C:\OpenTDU2 -> C:\OpenTDU2-win) and the three agent
# trees stay apart on their own:
#
#   branch win    -> build-win\bin\opentdu2-win.exe     (one exe name per tree,
#   branch cline  -> build-cline\bin\opentdu2-cline.exe  so a run in one tree can
#   branch glm    -> build-glm\bin\opentdu2-glm.exe      never lock or kill another)
#
# build-cline.ps1 / build-glm.ps1 / build-hub.ps1 are thin wrappers kept for
# habit; they just call this with a fixed identity.
param(
    [string] $Suffix = $null,       # exe suffix; default "-<branch>"
    [string] $BuildDir = $null,     # build folder; default "build-<branch>"
    [switch] $PlainExe,             # legacy: no suffix at all (opentdu2.exe)
    [switch] $Reconfigure
)
$ErrorActionPreference = 'Continue'

# Neither ninja nor rc.exe is on PATH for a fresh shell, and the configure needs
# the absolute path to ninja (CLAUDE.md "Run and verify").
$ninjaDir = 'C:\Users\cuz_c\Downloads\ninja-win'
$sdkBin   = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64'
foreach ($d in @($ninjaDir, $sdkBin)) {
    if (Test-Path $d) { $env:PATH = "$d;$env:PATH" } else { Write-Warning "missing: $d" }
}

$root = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }
Set-Location $root

$branch = (& git -C $root rev-parse --abbrev-ref HEAD 2>$null | Select-Object -First 1)
if ([string]::IsNullOrWhiteSpace($branch)) { $branch = 'unknown' }
$branch = $branch.Trim() -replace '[^A-Za-z0-9_.-]', '-'
# NB: a [string] parameter's $null default is coerced to "" by the binder, so
# test for empty rather than $null or the branch-derived defaults never apply.
if ($PlainExe) { $Suffix = '' }
elseif ([string]::IsNullOrEmpty( $Suffix )) { $Suffix = "-$branch" }
if ([string]::IsNullOrEmpty( $BuildDir )) { $BuildDir = "build-$branch" }

# C:\otdu2-deps caches the 9 GitHub dependencies (SDL, Jolt, imgui, volk, VMA,
# Vulkan-Headers, SPIRV-Headers, stb, earcut) so a fresh configure is offline.
$depsDir = 'C:\otdu2-deps'
$depsArg = @()
if (Test-Path (Join-Path $depsDir 'SDL')) {
    $depsArg = @("-DOTDU_DEPS_DIR=$($depsDir -replace '\\','/')")
    Write-Host "deps cache: $depsDir"
} else {
    Write-Warning "no deps cache at $depsDir - dependencies will be cloned from GitHub"
}

Write-Host "workspace: $root"
Write-Host "branch:    $branch"
Write-Host "build dir: $BuildDir"
Write-Host "exe:       opentdu2$Suffix.exe"

# The build dir may have been configured in a different folder and then renamed
# with the tree (the workspace swap in WORKSPACES.md renames C:\OpenTDU2-cline2
# -> C:\OpenTDU2-cline). CMake stores the absolute source and cache paths, and
# then refuses both configure and build:
#   "The current CMakeCache.txt directory ... is different than the directory
#    ... where CMakeCache.txt was created" / "The source ... does not match".
# -Reconfigure cannot rescue it either, so start the build dir over.
$cacheFile = Join-Path $BuildDir 'CMakeCache.txt'
if (Test-Path $cacheFile) {
    $cachedHome = (Select-String -Path $cacheFile -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$' -ErrorAction SilentlyContinue | Select-Object -First 1)
    if ($cachedHome) {
        $cachedRoot = $cachedHome.Matches[0].Groups[1].Value.Trim() -replace '/', '\'
        if ($cachedRoot.TrimEnd('\') -ne $root.TrimEnd('\')) {
            Write-Warning "'$BuildDir' was configured in '$cachedRoot' but this workspace is '$root' - starting the build dir over"
            Remove-Item -LiteralPath $BuildDir -Recurse -Force -ErrorAction SilentlyContinue
            if (Test-Path $cacheFile) {
                Write-Error "could not remove '$BuildDir' - is opentdu2$Suffix.exe still running?"
                exit 1
            }
        }
    }
}

# The configure writes the suffix into the cache: changing it (or the build dir)
# needs a fresh configure, which -Reconfigure forces.
$needConfigure = $Reconfigure -or -not (Test-Path (Join-Path $BuildDir 'CMakeCache.txt'))
if (-not $needConfigure) {
    $cached = Select-String -Path (Join-Path $BuildDir 'CMakeCache.txt') -Pattern '^OTDU_EXE_SUFFIX:' -ErrorAction SilentlyContinue
    if ($cached -and ($cached.Line -notmatch [regex]::Escape("OTDU_EXE_SUFFIX:STRING=$Suffix"))) {
        Write-Host "cached suffix differs from '$Suffix' - reconfiguring"
        $needConfigure = $true
    }
}

if ($needConfigure) {
    Write-Host '=== configure ==='
    cmake -S $root -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release @depsArg `
          -DOTDU_EXE_SUFFIX="$Suffix" `
          -DCMAKE_MAKE_PROGRAM="$ninjaDir\ninja.exe" `
          -DCMAKE_C_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" `
          -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe"
    Write-Host "configure exit: $LASTEXITCODE"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host '=== build ==='
cmake --build $BuildDir --parallel
$buildCode = $LASTEXITCODE
Write-Host "build exit: $buildCode"

# "It seemed fine but the launch .bat runs an old build" (owner report): the
# build step is skipped whenever a merge above stopped on a conflict, and a
# skipped build leaves the previous exe in place with no obvious sign. Print the
# exe and its timestamp, and say so when a source file is newer than it.
$exePath = Join-Path (Join-Path $BuildDir 'bin') "opentdu2$Suffix.exe"
if (Test-Path $exePath) {
    $exe = Get-Item $exePath
    Write-Host ("exe:       {0}  {1:yyyy-MM-dd HH:mm:ss}  {2:N0} bytes" -f $exe.FullName, $exe.LastWriteTime, $exe.Length)
    $newest = Get-ChildItem -Path @((Join-Path $root 'source'), (Join-Path $root 'resources')) -Recurse -File -ErrorAction SilentlyContinue |
              Where-Object { $_.Extension -in '.cpp', '.h', '.vert', '.frag', '.comp' } |
              Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($newest -and $newest.LastWriteTime -gt $exe.LastWriteTime) {
        Write-Warning ("STALE BUILD: '{0}' ({1:HH:mm:ss}) is newer than the exe ({2:HH:mm:ss}) - a launch would run the OLD binary. Re-run this script and read the output above." -f $newest.Name, $newest.LastWriteTime, $exe.LastWriteTime)
    }
} else {
    Write-Warning "no exe at '$exePath' - the build did not produce one"
}
# cmake --build reports ninja failures as a NEGATIVE code (-1), and a .bat
# caller's "if errorlevel 1" cannot see a negative errorlevel - normalize to
# 0/1 so update-and-rebuild.bat stops calling a failed build a success.
if ($buildCode -ne 0) { exit 1 }
exit 0
