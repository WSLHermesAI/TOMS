<#
.SYNOPSIS
  Publishes Build\dist\TOMS-web (made by build_web.bat) to GitHub Pages: the gh-pages branch of origin.
  Site: https://<owner>.github.io/<repo>/  (for WSLHermesAI/TOMS: https://wslhermesai.github.io/TOMS/)

  1. checks out gh-pages in a temporary git worktree (your working copy is not touched)
  2. replaces the site with the new package; keeps the previous version's stamped files so a page
     that a browser or the CDN still has cached keeps working for a few minutes
  3. shows what changes and ASKS before it commits and pushes (pushing makes it public)

.PARAMETER DryRun   Prepare and show the changes, then stop (no commit, no push).
.PARAMETER Yes      Do not ask (for scripts / CI).
.PARAMETER Keep     How many previous versions to keep on the site (default 1).
#>
param([switch]$DryRun, [switch]$Yes, [int]$Keep = 1, [string]$Remote = 'origin', [string]$Branch = 'gh-pages')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pkg  = Join-Path $root 'Build\dist\TOMS-web'
$versionFile = Join-Path $pkg 'version.txt'
if (-not (Test-Path $versionFile)) { throw "No packaged web build in $pkg. Run build_web.bat first." }
$stamp = (Get-Content $versionFile -Raw).Trim()

function Invoke-PagesGit { & git.exe -C $script:wt @args; if ($LASTEXITCODE) { throw "git $($args -join ' ') failed ($LASTEXITCODE)" } }
function Invoke-RepoGit { & git.exe -C $root @args; if ($LASTEXITCODE) { throw "git $($args -join ' ') failed ($LASTEXITCODE)" } }

# ---- where it goes ----
$remoteUrl = (& git.exe -C $root remote get-url $Remote).Trim()
$pagesUrl = $null
if ($remoteUrl -match 'github\.com[:/]+([^/]+)/([^/.]+?)(\.git)?$') { $pagesUrl = "https://$($Matches[1].ToLower()).github.io/$($Matches[2])/" }
Write-Host "[toms] publishing web build $stamp"
Write-Host "[toms] to       $Remote/$Branch  ($remoteUrl)"
if ($pagesUrl) { Write-Host "[toms] site     $pagesUrl" }

# ---- 1. gh-pages in a temporary worktree ----
$script:wt = Join-Path $env:TEMP "toms-pages-$stamp"
$hasBranch = [bool](& git.exe -C $root ls-remote --heads $Remote $Branch)
try {
    if ($hasBranch) {
        Invoke-RepoGit fetch --quiet $Remote $Branch
        Invoke-RepoGit worktree add --quiet --detach $script:wt FETCH_HEAD
    } else {
        Write-Host "[toms] $Remote has no '$Branch' branch yet: creating it"
        Invoke-RepoGit worktree add --quiet --detach $script:wt HEAD
        Invoke-PagesGit checkout --quiet --orphan "toms-pages-$stamp"
        Invoke-PagesGit rm -r -q -f --ignore-unmatch .
    }

    # ---- 2. replace the site, keep the newest $Keep previous stamped versions ----
    $stampOf = { param($name) if ($name -match '^toms_game(?:_mt)?\.(.+)\.(js|wasm|data)$') { $Matches[1] } }
    $previous = @(Get-ChildItem $script:wt -File | ForEach-Object { & $stampOf $_.Name } | Where-Object { $_ } |
                  Sort-Object -Unique | Sort-Object { ($_ -split '-')[-1] } -Descending | Select-Object -First $Keep)
    Get-ChildItem $script:wt -Force | Where-Object { $_.Name -ne '.git' } | ForEach-Object {
        $s = & $stampOf $_.Name
        if (-not ($s -and $previous -contains $s)) { Remove-Item -LiteralPath $_.FullName -Recurse -Force }
    }
    Copy-Item (Join-Path $pkg '*') $script:wt -Recurse -Force
    Copy-Item (Join-Path $pkg '.htaccess') $script:wt -Force -ErrorAction SilentlyContinue
    Set-Content -Encoding ASCII (Join-Path $script:wt '.nojekyll') ''   # serve files as they are (no Jekyll)
    Invoke-PagesGit add -A

    $changes = @(& git.exe -C $script:wt status --short)
    Write-Host ""
    Write-Host "[toms] site after publishing:"
    Get-ChildItem $script:wt -Force -File | Where-Object { $_.Name -ne '.git' } | Sort-Object Name | ForEach-Object { Write-Host ("         {0,-44} {1,8:N0} KB" -f $_.Name, ($_.Length / 1KB)) }
    Write-Host ("[toms] kept previous version(s): {0}" -f $(if ($previous) { $previous -join ', ' } else { 'none' }))
    Write-Host ("[toms] {0} file change(s)" -f $changes.Count)
    if ($changes.Count -eq 0) { Write-Host "[toms] nothing to publish (the site already has this build)"; return }

    if ($DryRun) { Write-Host "[toms] dry run: stopping before commit and push"; return }
    if (-not $Yes) {
        $answer = Read-Host "Publish $stamp to $(if ($pagesUrl) { $pagesUrl } else { "$Remote/$Branch" }) now? This makes it public. (y/N)"
        if ($answer -notmatch '^(y|yes)$') { Write-Host "[toms] cancelled, nothing pushed"; return }
    }

    # ---- 3. commit and push ----
    Invoke-PagesGit commit --quiet -m "Deploy web build $stamp"
    Invoke-PagesGit push $Remote "HEAD:refs/heads/$Branch"
    Write-Host ""
    Write-Host "[toms] published $stamp."
    if ($pagesUrl) { Write-Host "[toms] live in about 1-2 minutes at $pagesUrl  (GitHub > Actions shows the 'pages build and deployment' run)" }
}
finally {
    if (Test-Path $script:wt) { & git.exe -C $root worktree remove --force $script:wt 2>$null | Out-Null }
    & git.exe -C $root worktree prune 2>$null | Out-Null
}
