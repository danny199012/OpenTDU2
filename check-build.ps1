# Which executable would a launch straight out of THIS tree run, and is it the
# build of the current sources? Answers the owner's report: "the updater said it
# was fine but launch\*.bat keeps running an older build".
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File check-build.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File check-build.ps1 -Tree E:\OpenTDU2
#
# It reads only: nothing is built, nothing is changed.
param(
    [string] $Tree = $PSScriptRoot,
    # Fail (exit 1) when the branch is behind its origin/<branch> ref: the
    # RULES.md session-start gate. Without it this stays a report.
    [switch] $RequireSynced,
    # Fail (exit 1) when the branch is behind the integrated line - origin/main
    # or the integrator's origin/win, whichever it is further behind. Use it at
    # session start once the integrator has pushed win (RULES.md section 2).
    [switch] $RequireIntegrated
)
$ErrorActionPreference = 'Continue'

function Get-Branch( $path ) {
    $b = (& git -C $path rev-parse --abbrev-ref HEAD 2>$null | Select-Object -First 1)
    if ([string]::IsNullOrWhiteSpace( $b )) { return '(not a git tree)' }
    return $b.Trim()
}

Write-Host ""
Write-Host "tree:    $Tree"
if (-not (Test-Path $Tree)) { Write-Host "  does not exist"; exit 1 }
$branch = Get-Branch $Tree
Write-Host "branch:  $branch"
$head = (& git -C $Tree --no-pager log --oneline -1 2>$null | Select-Object -First 1)
Write-Host "head:    $head"

# --- the state of the tree: an unfinished merge means the updater stopped, and
# its build step (the one after the merges) never ran ----------------------
$mergeHeads = @(Get-ChildItem -Path (Join-Path $Tree '.git') -Filter 'MERGE_HEAD' -ErrorAction SilentlyContinue)
$unmerged = @(& git -C $Tree diff --name-only --diff-filter=U 2>$null)
if ($mergeHeads.Count -gt 0) {
    Write-Host "MERGE IN PROGRESS - the updater stopped before its build step:" -ForegroundColor Yellow
    if ($unmerged.Count -gt 0) {
        $unmerged | ForEach-Object { Write-Host "  conflict: $_" -ForegroundColor Yellow }
        Write-Host "  fix each file, then: git add -A ; git commit --no-edit ; then re-run the updater"
    } else {
        Write-Host "  the merge is resolved but not committed: git commit --no-edit"
    }
}

# --- which exe a launch picks: the SAME order as launch/resolve_exe.bat -----
$candidates = @(
    "build-main\bin\opentdu2-main.exe",
    "build-win\bin\opentdu2-win.exe",
    "build-cline\bin\opentdu2-cline.exe",
    "build-glm\bin\opentdu2-glm.exe",
    "build\bin\opentdu2.exe",
    "build\bin\opentdu2-main.exe",
    "build\bin\opentdu2-win.exe",
    "build\bin\opentdu2-cline.exe",
    "build\bin\opentdu2-glm.exe"
)
$found = @()
foreach ($c in $candidates) {
    $full = Join-Path $Tree $c
    if (Test-Path $full) {
        $f = Get-Item $full
        $found += [pscustomobject]@{ Rel = $c; Full = $full; Time = $f.LastWriteTime; Size = $f.Length }
    }
}
if ($found.Count -eq 0) {
    Write-Host "NO EXE in this tree - nothing to launch (build it: powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1)" -ForegroundColor Red
    exit 1
}
$chosen = $found[0]
Write-Host ("launch\*.bat would run:  {0}   {1:yyyy-MM-dd HH:mm:ss}   {2:N0} bytes" -f $chosen.Rel, $chosen.Time, $chosen.Size)
if ($found.Count -gt 1) {
    Write-Host "  (other exes in this tree, not used unless the first one is deleted:)"
    $found | Select-Object -Skip 1 | ForEach-Object { Write-Host ("    {0}   {1:yyyy-MM-dd HH:mm:ss}" -f $_.Rel, $_.Time) -ForegroundColor DarkGray }
}

# --- is that exe the build of the current sources? -------------------------
$sources = @(Get-ChildItem -Path @((Join-Path $Tree 'source'), (Join-Path $Tree 'resources')) -Recurse -File -ErrorAction SilentlyContinue |
             Where-Object { $_.Extension -in '.cpp', '.h', '.vert', '.frag', '.comp' })
$cmake = Get-Item (Join-Path $Tree 'CMakeLists.txt') -ErrorAction SilentlyContinue
$newest = $sources | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($cmake -and (-not $newest -or $cmake.LastWriteTime -gt $newest.LastWriteTime)) { $newest = $cmake }

if (-not $newest) {
    Write-Host "no sources found under source\ / resources\" -ForegroundColor Yellow
    exit 0
}
Write-Host ("newest source:           {0}   {1:yyyy-MM-dd HH:mm:ss}" -f $newest.Name, $newest.LastWriteTime)
if ($newest.LastWriteTime -gt $chosen.Time) {
    Write-Host ""
    Write-Host "STALE: the sources are newer than the exe a launch would run." -ForegroundColor Red
    Write-Host "  the build step did not run or did not finish. Fix the merge state above, then:" -ForegroundColor Red
    Write-Host "  powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "OK: the exe is newer than every source file - a launch runs this tree's build." -ForegroundColor Green

# --- the build stamp the exe carries ---------------------------------------
# build.ps1 stamps <build>/generated/build_info.h from git on every build, so
# reading the header next to the chosen exe names the build without running it.
$buildDir = ($chosen.Rel -split '[\\/]')[0]
$stampPath = Join-Path $Tree (Join-Path $buildDir 'generated\build_info.h')
if (Test-Path $stampPath) {
    $stamp = Get-Content $stampPath -Raw -Encoding UTF8
    $full = [regex]::Match($stamp, '#define OTDU_BUILD_FULL_STRING "(.*)"').Groups[1].Value
    if ([string]::IsNullOrWhiteSpace($full)) {
        $full = [regex]::Match($stamp, '#define OTDU_BUILD_STRING "(.*)"').Groups[1].Value
        $branchMacro = [regex]::Match($stamp, '#define OTDU_BUILD_BRANCH "(.*)"').Groups[1].Value
        if ($branchMacro) { $full = "$full · $branchMacro" }
    }
    Write-Host ("build stamp:             {0}" -f $full)
} else {
    Write-Host "build stamp:             (no build_info.h in $buildDir - build again to stamp it)" -ForegroundColor Yellow
}

# --- sync state against origin --------------------------------------------
# Read-only: compares with the local origin refs (no fetch), so a stale ref is
# called out rather than silently trusted.
$origin = (& git -C $Tree remote get-url origin 2>$null | Select-Object -First 1)
if ([string]::IsNullOrWhiteSpace($origin)) {
    Write-Host "sync:                    no 'origin' remote in this tree" -ForegroundColor DarkGray
} else {
    $behind = 0
    $upstream = "origin/$branch"
    $hasUpstream = (& git -C $Tree rev-parse --verify --quiet $upstream 2>$null)
    if ($hasUpstream) {
        $counts = @((& git -C $Tree rev-list --left-right --count "$upstream...HEAD" 2>$null | Select-Object -First 1) -split '\s+')
        if ($counts.Count -ge 2) {
            $behind = [int]$counts[0]
            $ahead = [int]$counts[1]
        }
        $colour = if ($behind -gt 0) { 'Red' } elseif ($ahead -gt 0) { 'Yellow' } else { 'Green' }
        Write-Host ("sync:                    {0} behind, {1} ahead of {2}" -f $behind, $ahead, $upstream) -ForegroundColor $colour
        $mainCounts = @((& git -C $Tree rev-list --left-right --count "origin/main...HEAD" 2>$null | Select-Object -First 1) -split '\s+')
        if ($mainCounts.Count -ge 2) {
            Write-Host ("                         {0} behind origin/main (integration lag)" -f [int]$mainCounts[0])
        }
        $winCounts = @((& git -C $Tree rev-list --left-right --count "origin/win...HEAD" 2>$null | Select-Object -First 1) -split '\s+')
        if ($winCounts.Count -ge 2) {
            Write-Host ("                         {0} behind origin/win (integrator lag)" -f [int]$winCounts[0])
        }
        $lag = 0
        if ($mainCounts.Count -ge 2) { $lag = [Math]::Max($lag, [int]$mainCounts[0]) }
        if ($winCounts.Count -ge 2) { $lag = [Math]::Max($lag, [int]$winCounts[0]) }
        if ($RequireIntegrated -and ($lag -gt 0)) {
            Write-Host ""
            Write-Host "NOT INTEGRATED: this branch is behind the integrated line (RULES.md section 2):" -ForegroundColor Red
            Write-Host "  git fetch origin ; git merge origin/win    # origin/main once main is ahead" -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "sync:                    $upstream is not fetched yet - run: git fetch origin" -ForegroundColor Yellow
    }
    if ($RequireSynced -and $behind -gt 0) {
        Write-Host ""
        Write-Host "NOT SYNCED: sync before working (RULES.md §2):" -ForegroundColor Red
        Write-Host "  git fetch origin ; git merge origin/$branch" -ForegroundColor Red
        exit 1
    }
}

# --- the handoff + the WIP block (RULES.md §3/§8) -------------------------
# The win branch's handoff shard is docs/state/main.md (the hub shard).
$shardName = if ($branch -eq 'win') { 'main' } else { $branch }
$shard = Join-Path $Tree ("docs\state\{0}.md" -f $shardName)
if (Test-Path $shard) {
    $lastSession = (Get-Content $shard -Encoding UTF8 | Select-String -Pattern '^## ' | Select-Object -First 1)
    if ($lastSession) { Write-Host ("last session entry:      {0}" -f $lastSession.Line) }
}
$board = Join-Path $Tree 'PROGRESS.md'
if (Test-Path $board) {
    $inWip = $false
    $wipRows = @()
    foreach ($line in Get-Content $board -Encoding UTF8) {
        if ($line -match '^## ') { $inWip = ($line -match '^##\s+WIP'); continue }
        if (-not $inWip) { continue }
        if ($line -match '^\|\s*-') { continue }            # separator row
        if ($line -match '^\|\s*Area') { continue }         # header row
        if ($line -match '^\|') { $wipRows += $line }
    }
    Write-Host ""
    if ($wipRows.Count -gt 0) {
        Write-Host "WIP (read before starting - PROGRESS.md):" -ForegroundColor Yellow
        $wipRows | ForEach-Object { Write-Host "  $_" }
    } else {
        Write-Host "WIP: none listed in PROGRESS.md"
    }
}
exit 0
