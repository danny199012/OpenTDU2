# sync-workspace.ps1 — report on the three OpenTDU2 agent clones; sync the tree you are in.
#
# The clone-per-agent layout (WORKSPACES.md): each tree is an ordinary clone of
# the owner's private repository and talks to it through `origin` only.
#
#   C:\OpenTDU2-win    branch win    (the integrator)
#   C:\OpenTDU2-glm    branch glm
#   C:\OpenTDU2-cline  branch cline
#
# Usage (from anywhere inside a workspace):
#
#   .\sync-workspace.ps1              -> report only (read only; never touches the network)
#   .\sync-workspace.ps1 -Merge       -> fetch origin, then merge origin/<your branch> into this tree
#   .\sync-workspace.ps1 -Integrate   -> win tree only: fetch, then merge main -> glm -> cline
#
# It refuses a dirty working tree, a tree that already has MERGE_HEAD, and this
# workspace's own game running (the exe locks the build dir, and syncing under a
# live agent is how half-integrated builds happen).
param(
    [switch] $Merge,
    [switch] $Integrate
)
$ErrorActionPreference = 'Continue'
if ($Merge -and $Integrate) { Write-Error 'pick -Merge or -Integrate, not both'; exit 2 }

# Read-only reporting must never fight a live agent: no optional locks.
$env:GIT_OPTIONAL_LOCKS = '0'

$workspaces = @(
    [pscustomobject]@{ Agent = 'Claude (win)'; Path = 'C:\OpenTDU2-win';   Branch = 'win'   },
    [pscustomobject]@{ Agent = 'GLM';          Path = 'C:\OpenTDU2-glm';   Branch = 'glm'   },
    [pscustomobject]@{ Agent = 'Cline';        Path = 'C:\OpenTDU2-cline'; Branch = 'cline' }
)

function Get-Git {
    param([string] $Repo, [string[]] $GitArgs)
    # git writes progress to stderr; PowerShell treats that as a terminating
    # error, so capture both streams and keep going. --no-optional-locks keeps
    # the read-only calls safe next to an agent working in another tree.
    & git --no-optional-locks -C $Repo @GitArgs 2>&1
}

function Get-Counts {
    param([string] $Repo, [string] $Left, [string] $Right)
    $line = "$(Get-Git $Repo @('rev-list','--left-right','--count',"$Left...$Right") | Select-Object -First 1)"
    if ($line -match '^\s*(\d+)\s+(\d+)\s*$') {
        return [pscustomobject]@{ Left = [int]$Matches[1]; Right = [int]$Matches[2] }
    }
    return $null
}

function Get-Info {
    param([string] $Repo)
    if (-not (Test-Path (Join-Path $Repo '.git'))) { return $null }
    $branch = "$(Get-Git $Repo @('rev-parse','--abbrev-ref','HEAD') | Select-Object -First 1)"
    $short  = "$(Get-Git $Repo @('rev-parse','--short','HEAD') | Select-Object -First 1)"
    $dirty  = @(Get-Git $Repo @('status','--porcelain') | Where-Object { $_ -is [string] -and $_ -notmatch '^\?\?' }).Count
    $subject = "$(Get-Git $Repo @('log','-1','--pretty=%s') | Select-Object -First 1)"
    if ($subject.Length -gt 52) { $subject = $subject.Substring(0, 52) }
    [pscustomobject]@{ Branch = $branch; Short = $short; Dirty = $dirty; Subject = $subject }
}

$me = (& git rev-parse --show-toplevel 2>$null | Select-Object -First 1)
if (-not $me) { Write-Error 'not inside a git working tree'; exit 1 }
$me = "$me" -replace '/', '\'

$myInfo = Get-Info $me
Write-Host "this workspace : $me  (branch '$($myInfo.Branch)', $($myInfo.Short))"

if ($Merge -or $Integrate) {
    Write-Host 'fetching origin ...'
    $fetchOut = Get-Git $me @('fetch','origin','--prune')
    $fetchOut | ForEach-Object { if ($_ -is [string] -and $_.Trim()) { Write-Host "  $_" } }
}

Write-Host ''
Write-Host 'workspace            branch  head      dirty  vs origin/<branch>     main/win behind  last commit'
Write-Host '-------------------  ------  --------  -----  ---------------------  ---------------  -----------'
foreach ($ws in $workspaces) {
    $info = Get-Info $ws.Path
    if (-not $info) { Write-Host ("{0,-19}  (missing: {1})" -f $ws.Agent, $ws.Path); continue }
    $own  = Get-Counts $ws.Path "origin/$($ws.Branch)" 'HEAD'
    $main = Get-Counts $ws.Path 'origin/main' 'HEAD'
    $win  = Get-Counts $ws.Path 'origin/win' 'HEAD'
    $ownText  = if ($own)  { "behind $($own.Left) / ahead $($own.Right)" } else { 'origin ref missing' }
    $integrated = "{0} / {1}" -f $(if ($main) { $main.Left } else { '-' }), $(if ($win) { $win.Left } else { '-' })
    Write-Host ("{0,-19}  {1,-6}  {2,-8}  {3,-5}  {4,-21}  {5,-15}  {6}" -f `
        $ws.Agent, $info.Branch, $info.Short, $info.Dirty, $ownText, $integrated, $info.Subject)
}

if (-not $Merge -and -not $Integrate) {
    Write-Host ''
    Write-Host 'Report only (local origin refs). -Merge syncs this tree; -Integrate syncs the win tree (RULES.md section 5).'
    exit 0
}

# --- guards ---------------------------------------------------------------
if (Test-Path (Join-Path $me '.git\MERGE_HEAD')) {
    Write-Error "$me already has MERGE_HEAD - finish or abort that merge first."
    exit 1
}
if ($Integrate -and ($myInfo.Branch -ne 'win')) {
    Write-Error "-Integrate is the win tree's sequence (branch win); this tree is '$($myInfo.Branch)'. Use -Merge here."
    exit 1
}
if ($Merge -and ($myInfo.Branch -eq 'win')) {
    Write-Error 'the win tree integrates with -Integrate (main -> glm -> cline), not -Merge.'
    exit 1
}
if ($myInfo.Dirty -gt 0) {
    Write-Error "$($myInfo.Dirty) modified file(s) here - commit or stash before syncing."
    exit 1
}
$running = @(Get-Process opentdu2* -ErrorAction SilentlyContinue |
             Where-Object { $_.Path -and $_.Path.StartsWith($me, 'OrdinalIgnoreCase') })
if ($running.Count) {
    Write-Error "opentdu2* is running from this workspace (pid $($running[0].Id)) - close it before merging."
    exit 1
}

# --- sync -----------------------------------------------------------------
$before = "$(Get-Git $me @('rev-parse','HEAD') | Select-Object -First 1)"
$sequence = if ($Integrate) { @('origin/main','origin/glm','origin/cline') }
            else            { @("origin/$($myInfo.Branch)") }
foreach ($ref in $sequence) {
    Write-Host ''
    Write-Host "=== merging $ref into '$($myInfo.Branch)' ==="
    $exists = (& git -C $me rev-parse --verify --quiet $ref 2>$null)
    if (-not $exists) { Write-Error "$ref is not fetched - run: git fetch origin"; exit 1 }
    $out = Get-Git $me @('merge',$ref,'--no-edit')
    $exit = $LASTEXITCODE
    $out | ForEach-Object { if ($_ -is [string]) { Write-Host $_ } }
    Write-Host "merge exit: $exit"
    if ($exit -ne 0) {
        Write-Host 'Merge stopped (conflicts?). Resolve them, then: git add <files> ; git commit'
        Write-Host 'To back out:  git merge --abort'
        exit $exit
    }
}

$after = "$(Get-Git $me @('rev-parse','HEAD') | Select-Object -First 1)"
if ($after -eq $before) {
    Write-Host ''
    Write-Host 'already up to date - nothing to rebuild.'
    exit 0
}

$changed = @(Get-Git $me @('diff','--name-only',$before,$after,'--','source','CMakeLists.txt','cmake') |
             Where-Object { $_ -is [string] })
$shortNow = "$(Get-Git $me @('rev-parse','--short','HEAD') | Select-Object -First 1)"
Write-Host ''
Write-Host "now at $shortNow"
if ($changed.Count) {
    Write-Host "build-affecting files changed ($($changed.Count)) - rebuild before running:"
    $changed | Select-Object -First 10 | ForEach-Object { Write-Host "    $_" }
    Write-Host "    powershell -NoProfile -ExecutionPolicy Bypass -File `"$me\build.ps1`""
} else {
    Write-Host 'no source/CMake changes - the existing build is still valid.'
}
if ($Integrate) {
    Write-Host 'next: build, smoke-check, then  git push origin win'
    Write-Host '      optional, to keep the owner''s test branch current (fast-forward only):'
    Write-Host '        git push origin win:main'
}
exit 0

