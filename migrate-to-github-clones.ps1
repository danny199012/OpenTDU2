#Requires -Version 5.1
<#
    migrate-to-github-clones.ps1

    Moves the three agent workspaces onto independent clones of the owner's
    GitHub repository, one branch each (win / glm / cline). Afterwards every tree
    has `origin` = GitHub and can `git push` with no setup at all, and the
    cross-tree path remotes / sync-workspace.ps1 stop being needed.

    It deliberately does NOT replace a tree that a live agent is working in:

      C:\OpenTDU2        (hub)   -> deleted, cloned as C:\OpenTDU2-win     (branch win)
      C:\OpenTDU2-cline  (Cline) -> staged as C:\OpenTDU2-cline2           (branch cline)
      C:\OpenTDU2-glm    (GLM)   -> staged as C:\OpenTDU2-glm2             (branch glm)

    ...so the two live trees are left completely alone. The swap is done by hand
    when each agent is idle (the script prints the exact commands), which is why
    the staging folders exist: nothing is ever deleted out from under an agent.

    Usage (from anywhere):
        powershell -NoProfile -ExecutionPolicy Bypass -File migrate-to-github-clones.ps1
        ... -NoBuild          # skip the three rebuilds (do them later per tree)
        ... -SkipHub          # only stage the two new agent trees
        ... -Force            # proceed even if the backup bundles are missing
#>
param(
    [string] $RepoUrl    = 'https://github.com/danny199012/Open-TestDrive-Unlimited-2-',
    [string] $HubOld     = 'C:\OpenTDU2',
    [string] $HubNew     = 'C:\OpenTDU2-win',
    [string] $ClineLive  = 'C:\OpenTDU2-cline',
    [string] $ClineStage = 'C:\OpenTDU2-cline2',
    [string] $GlmLive    = 'C:\OpenTDU2-glm',
    [string] $GlmStage   = 'C:\OpenTDU2-glm2',
    [string] $Backup     = 'C:\otdu2-backup-20261030-0532',
    [switch] $NoBuild,
    [switch] $SkipHub,
    [switch] $Force
)
$ErrorActionPreference = 'Continue'

function Step( $t ) { Write-Host "`n=== $t" -ForegroundColor Cyan }
function Ok( $t )   { Write-Host "    $t" -ForegroundColor Green }
function Warn( $t ) { Write-Host "    $t" -ForegroundColor Yellow }
function Die( $t )  { Write-Host "`nABORT: $t" -ForegroundColor Red; exit 1 }

Step 'Preconditions'

# A running game locks its own build dir and exe; nothing here may be in use.
$running = @(Get-Process -ErrorAction SilentlyContinue |
             Where-Object { $_.ProcessName -like 'opentdu2*' })
if ( $running.Count -gt 0 ) {
    Die ( "a game is running ({0}) - close every OpenTDU2 instance first" -f
          ( ( $running | ForEach-Object { $_.ProcessName } ) -join ', ' ) )
}
Ok 'no OpenTDU2 process is running'

# The bundles are the safety net: refuse to move anything without them unless -Force.
$missing = @( 'hub-full.bundle', 'glm-full.bundle', 'cline-full.bundle' |
              Where-Object { -not (Test-Path (Join-Path $Backup $_)) } )
if ( $missing.Count -gt 0 ) {
    if ( -not $Force ) {
        Die ( "backup bundles missing in {0}: {1}`n      (re-run with -Force to proceed anyway)" -f
              $Backup, ( $missing -join ', ' ) )
    }
    Warn "backup bundles missing: $($missing -join ', ') (continuing because -Force)"
} else {
    Ok "backup bundles present in $Backup"
}

# Auth/URL check - `git ls-remote` uses the cached credential, no prompt.
& git ls-remote --heads $RepoUrl *> $null
if ( $LASTEXITCODE -ne 0 ) { Die "cannot reach $RepoUrl (check the URL and your GitHub credentials)" }
Ok "repository reachable: $RepoUrl"

function New-Tree( [string] $Path, [string] $Branch, [string] $Label ) {
    Step "$Label -> $Path (branch $Branch)"
    if ( Test-Path $Path ) {
        Warn 'already exists - refreshing it instead of cloning'
        & git -C $Path fetch origin
        & git -C $Path switch $Branch 2>$null
        if ( $LASTEXITCODE -ne 0 ) { & git -C $Path switch -c $Branch }
        & git -C $Path reset --hard origin/main
    } else {
        & git clone $RepoUrl $Path
        if ( $LASTEXITCODE -ne 0 ) { Die "clone into $Path failed" }
        & git -C $Path switch -c $Branch
    }
    # Publish the branch so the other trees (and the hub) can merge it.
    & git -C $Path push -u origin $Branch
    Ok "$Path on branch $Branch (origin = $RepoUrl)"
}

if ( -not $SkipHub ) {
    if ( Test-Path $HubOld ) {
        Step "removing the old hub $HubOld (its content is on GitHub and in $Backup)"
        Remove-Item $HubOld -Recurse -Force -ErrorAction SilentlyContinue
        if ( Test-Path $HubOld ) {
            Warn "could not remove $HubOld - something is using it; it is harmless now, nothing depends on it"
        } else {
            Ok 'removed'
        }
    }
    New-Tree $HubNew 'win' 'HUB'
} else {
    Warn 'hub skipped (-SkipHub)'
}

# The two live trees are never touched - only staged beside them.
New-Tree $ClineStage 'cline' 'CLINE (staging)'
New-Tree $GlmStage   'glm'   'GLM (staging)'

Step 'Carrying over the things that are not in the repository'
$pauseMenu = Join-Path $Backup 'uncommitted\cline-pause_menu.cpp'
if ( Test-Path $pauseMenu ) {
    Copy-Item $pauseMenu (Join-Path $ClineStage 'source\game\pause_menu.cpp') -Force
    Ok "restored source\game\pause_menu.cpp into $ClineStage (the -logpause island-badge diagnostic)"
} else {
    Warn "no $pauseMenu to restore"
}

if ( -not $NoBuild ) {
    foreach ( $t in @( @( 'win', $HubNew ), @( 'cline', $ClineStage ), @( 'glm', $GlmStage ) ) ) {
        Step "building the $($t[0]) tree ($($t[1]))"
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $t[1] 'build.ps1')
        if ( $LASTEXITCODE -ne 0 ) { Warn "build failed in $($t[1]) - fix it there later" }
        else { Ok 'built' }
    }
} else {
    Warn 'builds skipped (-NoBuild) - run build.ps1 in each tree when convenient'
}

Step 'Done - now do these swaps when each agent is idle'
Write-Host @"

  CLINE (after the Cline session has stopped - it cannot be replaced while running):
      rmdir /s /q "$ClineLive"
      move "$ClineStage" "$ClineLive"

  GLM (after GLM has pushed and its agent has stopped):
      rmdir /s /q "$GlmLive"
      move "$GlmStage" "$GlmLive"

  The hub is already at $HubNew (branch win) - nothing else to do.

  Afterwards every tree is an ordinary clone: `git fetch origin`, `git push`,
  and integration is `git fetch origin` + `git merge origin/main` in whichever
  tree integrates (the `win` one), then `git push origin main`.

"@ -ForegroundColor Gray

