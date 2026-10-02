<#
.SYNOPSIS
  Checks (and offers to fix) everything git needs to fetch and push TOMS over SSH on this PC.
  Double-click tools\check_git_ssh.cmd, or let setup_new_pc.bat call it after creating the SSH key.

  Checks, in the order git uses them:
    1. which ssh program git runs: GIT_SSH_COMMAND / GIT_SSH environment variables, core.sshCommand,
       TortoiseGit's SSH client (TortoiseGitPlink ignores ~/.ssh keys -> "No supported authentication
       methods available (server sent: publickey)")
    2. the SSH key files and their fingerprints (compare with GitHub > Settings > SSH and GPG keys)
    3. the login itself (ssh -T git@github.com -> "Hi <account>!"; exit code 1 is normal, docs/12 5.1)
    4. the remote URL (push over SSH needs git@github.com:...)
    5. git fetch (read) and git push --dry-run (write; sends nothing)
    6. user.name / user.email
  Fixes are listed in one dialog and applied only after you click Yes. No administrator rights needed.

.PARAMETER Repo
  The repository to test (default: the one this script is in).

.PARAMETER NoGui
  Console only: no dialogs, and every fix is answered "No" (nothing on the PC is changed).
#>
param([string]$Repo = (Split-Path -Parent $PSScriptRoot), [switch]$NoGui)

$ErrorActionPreference = 'Continue'
$LogFile = Join-Path $env:TEMP 'TOMS_check_git_ssh.log'
Add-Type -AssemblyName System.Windows.Forms

$results = New-Object System.Collections.Generic.List[object]
function Add-Result([string]$state, [string]$name, [string]$detail) {
    $results.Add([pscustomobject]@{ State = $state; Name = $name; Detail = $detail })
    $color = @{ OK = 'Green'; WARN = 'Yellow'; FAIL = 'Red'; FIX = 'Cyan' }[$state]
    Write-Host ("[{0,-4}] {1}" -f $state, $name) -ForegroundColor $color
    if ($detail) { Write-Host "       $detail" }
    Add-Content -Path $LogFile -Value "[$state] $name -- $detail" -Encoding UTF8
}

function Show-Box([string]$text, [string]$title = 'TOMS - git / SSH check', [string]$buttons = 'OK', [string]$icon = 'Information') {
    if ($NoGui) { return $(if ($buttons -eq 'YesNo') { 'No' } else { 'OK' }) }
    $owner = New-Object System.Windows.Forms.Form -Property @{ TopMost = $true; ShowInTaskbar = $false; Opacity = 0 }
    $owner.Show(); $owner.Activate()
    $r = [System.Windows.Forms.MessageBox]::Show($owner, $text, $title,
        [System.Windows.Forms.MessageBoxButtons]::$buttons, [System.Windows.Forms.MessageBoxIcon]::$icon)
    $owner.Close()
    return $r.ToString()
}

# Runs a native command, returns all output (stdout + stderr) as one string; exit code in $LASTEXITCODE.
function Invoke-Native([string]$exe, [string[]]$arguments) {
    $out = & $exe @arguments 2>&1 | ForEach-Object { "$_" }
    return (@($out) -join "`n").Trim()
}

# First word of a command line ("C:/x y/ssh.exe" -v -> C:/x y/ssh.exe), and whether that program exists.
function Get-Program([string]$commandLine) {
    $c = $commandLine.Trim()
    if ($c -match '^"([^"]+)"') { return $Matches[1] }
    if ($c -match "^'([^']+)'") { return $Matches[1] }
    return ($c -split '\s+')[0]
}
function Test-Program([string]$program) {
    if (-not $program) { return $false }
    if ($program -match '[\\/]') { return (Test-Path ($program -replace '/', '\')) }
    if ($program -match '^ssh(\.exe)?$') { return $true }      # git's own shell always finds its bundled ssh
    return [bool](Get-Command $program -ErrorAction SilentlyContinue)
}

Set-Content -Path $LogFile -Value "TOMS git/SSH check $(Get-Date)" -Encoding UTF8
Write-Host "`nTOMS git / SSH check ($Repo)" -ForegroundColor Cyan
Write-Host ('-' * 78)

# ------------------------------------------------------------------ 0. git
$gitCmd = Get-Command git -ErrorAction SilentlyContinue
if (-not $gitCmd) {
    Add-Result FAIL 'Git' 'git is not on PATH. Install Git for Windows (setup_new_pc.bat does it).'
    Show-Box 'Git is not installed. Run setup_new_pc.bat first.' 'TOMS - git / SSH check' 'OK' 'Error' | Out-Null
    exit 1
}
$git = $gitCmd.Source
$gitRoot = Split-Path -Parent (Split-Path -Parent $git)                  # ...\Git\cmd\git.exe -> ...\Git
$isRepo = Test-Path (Join-Path $Repo '.git')
Add-Result OK 'Git' "$(Invoke-Native $git @('--version')), repo: $(if ($isRepo) { $Repo } else { 'not found' })"

# The ssh every git tool should use. Windows' OpenSSH path has no spaces, so it can go into core.sshCommand as is
# (a path with spaces, like C:/Program Files/..., breaks it: git runs the value as a command line).
$sysSsh = Join-Path $env:WINDIR 'System32\OpenSSH\ssh.exe'
if (Test-Path $sysSsh) { $wantCmd = $sysSsh -replace '\\', '/'; $sshExe = $sysSsh }
else { $wantCmd = 'ssh'; $sshExe = Join-Path $gitRoot 'usr\bin\ssh.exe' }
$keygen = if (Test-Path (Join-Path $env:WINDIR 'System32\OpenSSH\ssh-keygen.exe')) { Join-Path $env:WINDIR 'System32\OpenSSH\ssh-keygen.exe' }
          else { Join-Path $gitRoot 'usr\bin\ssh-keygen.exe' }

$fixes = New-Object System.Collections.Generic.List[object]     # @{ Text; Action = scriptblock }

# ------------------------------------------------------------------ 1. which ssh git runs
# GIT_SSH_COMMAND beats core.sshCommand beats GIT_SSH. A bad value makes every git command fail
# ("cannot spawn ...", "unable to fork").
foreach ($name in 'GIT_SSH_COMMAND', 'GIT_SSH') {
    foreach ($scope in 'User', 'Machine') {
        $v = [Environment]::GetEnvironmentVariable($name, $scope)
        if (-not $v) { continue }
        $prog = Get-Program $v
        if (-not (Test-Program $prog) -or $v -match 'plink') {
            if ($scope -eq 'User') {
                Add-Result FIX "$name ($scope variable) is broken" "value '$v' -> will be removed"
                $n = $name
                $fixes.Add(@{ Text = "Remove the broken user variable $name ('$v')"
                              Action = { [Environment]::SetEnvironmentVariable($n, $null, 'User'); Remove-Item "env:$n" -ErrorAction SilentlyContinue }.GetNewClosure() })
            } else {
                Add-Result FAIL "$name ($scope variable) is broken" "value '$v'. Remove it in System Properties > Environment Variables (needs admin)."
            }
        } else {
            Add-Result WARN "$name is set ($scope)" "'$v' overrides git's settings; fine if intended."
        }
    }
}

$cfg = Invoke-Native $git @('config', '--show-origin', '--show-scope', '--get-all', 'core.sshCommand')
$cfgLines = @($cfg -split "`n" | Where-Object { $_ })
$globalOk = $false
foreach ($line in $cfgLines) {
    # format: <scope>\t<origin>\t<value>
    $parts = $line -split "`t", 3
    if ($parts.Count -lt 3) { continue }
    $scope, $origin, $value = $parts
    $prog = Get-Program $value
    $bad = -not (Test-Program $prog) -or $value -match 'plink'
    if ($scope -eq 'global') {
        if ($bad) {
            Add-Result FIX 'core.sshCommand (global) is broken' "'$value' -> will be set to $wantCmd"
        } else { $globalOk = $true; Add-Result OK 'core.sshCommand (global)' $value }
    } elseif ($bad) {
        Add-Result FIX "core.sshCommand ($scope) is broken" "'$value' in $origin -> will be removed"
        $o = $origin -replace '^file:', ''
        $fixes.Add(@{ Text = "Remove the broken core.sshCommand from $o"
                      Action = { & $git config --file $o --unset-all core.sshCommand }.GetNewClosure() })
    }
}
if (-not $globalOk) {
    if (-not ($cfgLines | Where-Object { $_ -match '^global' })) {
        Add-Result FIX 'core.sshCommand (global) not set' "git tools may use another ssh (TortoiseGit: plink) -> will be set to $wantCmd"
    }
    $fixes.Add(@{ Text = "git config --global core.sshCommand $wantCmd   (every git tool, TortoiseGit too, uses this ssh)"
                  Action = { & $git config --global core.sshCommand $wantCmd }.GetNewClosure() })
}

$tgKey = 'HKCU:\Software\TortoiseGit'
$tgSsh = (Get-ItemProperty $tgKey -ErrorAction SilentlyContinue).SSH
if ($tgSsh -and $tgSsh -match 'plink') {
    Add-Result FIX 'TortoiseGit uses plink' "'$tgSsh' cannot use ~/.ssh keys -> will be set to $sshExe"
    $fixes.Add(@{ Text = "TortoiseGit > Settings > Network > SSH client = $sshExe"
                  Action = { Set-ItemProperty $tgKey -Name SSH -Value $sshExe }.GetNewClosure() })
} elseif ($tgSsh) { Add-Result OK 'TortoiseGit SSH client' $tgSsh }

# ------------------------------------------------------------------ 2. key files
$sshDir = Join-Path $env:USERPROFILE '.ssh'
$keys = @('id_ed25519', 'id_ecdsa', 'id_rsa' | ForEach-Object { Join-Path $sshDir $_ } | Where-Object { Test-Path $_ })
if (-not $keys) {
    Add-Result FAIL 'SSH key' "no key in $sshDir. Create one: setup_new_pc.bat (SSH step) or docs/12 section 3.A."
} else {
    foreach ($k in $keys) {
        $fp = if (Test-Path "$k.pub") { Invoke-Native $keygen @('-lf', "$k.pub") } else { '(no .pub file next to it)' }
        Add-Result OK "SSH key $(Split-Path $k -Leaf)" "$fp`n       compare with GitHub > Settings > SSH and GPG keys"
    }
}

# ------------------------------------------------------------------ apply fixes (one dialog)
if ($fixes.Count -gt 0) {
    $text = "These settings stop git from using your SSH key on this PC:`n`n" +
            (($fixes | ForEach-Object { "  * $($_.Text)" }) -join "`n") + "`n`nFix them now?"
    if ((Show-Box $text 'TOMS - git / SSH check' 'YesNo' 'Question') -eq 'Yes') {
        foreach ($f in $fixes) { & $f.Action; Add-Result OK 'fixed' $f.Text }
    } else { Add-Result WARN 'fixes skipped' 'git over SSH will probably keep failing' }
}

# The tests below must see the saved settings, not a variable left in this process.
Remove-Item env:GIT_SSH_COMMAND, env:GIT_SSH -ErrorAction SilentlyContinue
foreach ($n in 'GIT_SSH_COMMAND', 'GIT_SSH') {
    $u = [Environment]::GetEnvironmentVariable($n, 'User'); if ($u) { Set-Item "env:$n" $u }
}

# ------------------------------------------------------------------ 3. login
$account = $null
if ($keys) {
    $reply = Invoke-Native $sshExe @('-T', '-o', 'StrictHostKeyChecking=accept-new', '-o', 'ConnectTimeout=15', 'git@github.com')
    if ($reply -match 'Hi ([^!]+)!') {
        $account = $Matches[1]
        Add-Result OK 'GitHub login' "the key logs in as '$account' (ssh exit code 1 is normal here)"
    } else {
        Add-Result FAIL 'GitHub login' "$reply`n       Add the .pub key on GitHub (Settings > SSH and GPG keys > New SSH key) for the right account."
    }
}

# ------------------------------------------------------------------ 4-5. remote, fetch, push
if ($isRepo) {
    $url = Invoke-Native $git @('-C', $Repo, 'remote', 'get-url', 'origin')
    $owner = if ($url -match 'github\.com[:/]([^/]+)/') { $Matches[1] } else { $null }
    if ($account -and $owner -and $account -ne $owner) {
        Add-Result WARN 'account' "the key is '$account' but the repository belongs to '$owner': push works only if '$account' has write access"
    }
    if ($url -match '^https://github\.com/(.+)$') {
        $sshUrl = "git@github.com:$($Matches[1])"
        if ($account -and (Show-Box ("The remote uses HTTPS:`n   $url`n`nThe SSH login works, so push can use SSH:`n   $sshUrl`n`nSwitch the remote to SSH?") 'TOMS - git / SSH check' 'YesNo' 'Question') -eq 'Yes') {
            & $git -C $Repo remote set-url origin $sshUrl
            $url = $sshUrl
            Add-Result OK 'remote switched to SSH' $sshUrl
        } else {
            Add-Result WARN 'remote uses HTTPS' "$url  (pull works without login; push over HTTPS needs a token, docs/12 3.C)"
        }
    } else { Add-Result OK 'remote' $url }

    $fetch = Invoke-Native $git @('-C', $Repo, 'fetch', 'origin')
    if ($LASTEXITCODE -eq 0) { Add-Result OK 'git fetch (read)' 'works' }
    else { Add-Result FAIL 'git fetch (read)' $fetch }

    if ($url -match '^git@') {
        $push = Invoke-Native $git @('-C', $Repo, 'push', '--dry-run', 'origin', 'HEAD')
        if ($push -match 'denied|Permission|Could not read') { Add-Result FAIL 'git push --dry-run (write)' $push }
        elseif ($push -match 'rejected') { Add-Result WARN 'git push --dry-run (write)' "login and write access OK, but the branch is behind: git pull first`n       $push" }
        elseif ($LASTEXITCODE -eq 0) { Add-Result OK 'git push --dry-run (write)' 'write access works (nothing was sent)' }
        else { Add-Result FAIL 'git push --dry-run (write)' $push }
    }
}

# ------------------------------------------------------------------ 6. identity
$name = Invoke-Native $git @('config', '--global', 'user.name')
$email = Invoke-Native $git @('config', '--global', 'user.email')
if (-not $name -or -not $email) { Add-Result WARN 'commit identity' "user.name='$name' user.email='$email': set both (git config --global ...)" }
elseif ($email -notmatch '@') { Add-Result WARN 'commit identity' "user.email '$email' is not an e-mail address; GitHub cannot link the commits to an account" }
else { Add-Result OK 'commit identity' "$name <$email>  (public in every pushed commit)" }

# ------------------------------------------------------------------ summary
Write-Host ('-' * 78)
$fail = @($results | Where-Object State -eq 'FAIL')
$warn = @($results | Where-Object State -eq 'WARN')
$lines = $results | Where-Object { $_.State -ne 'FIX' } | ForEach-Object { "[{0}] {1}" -f $_.State, $_.Name }
$text = ($lines -join "`n") + "`n`n"
if ($fail) { $text += "Something is still wrong - details in the console and in`n$LogFile" }
else { $text += "git can fetch$(if ($results | Where-Object { $_.Name -like 'git push*' -and $_.State -eq 'OK' }) { ' and push' }) on this PC." }
$text += "`n`nIf one open cmd window still says 'cannot spawn ...', close it:`na 'set GIT_SSH_COMMAND=...' typed there only lives in that window."
Show-Box $text 'TOMS - git / SSH check' 'OK' $(if ($fail) { 'Error' } elseif ($warn) { 'Warning' } else { 'Information' }) | Out-Null
exit $(if ($fail) { 1 } else { 0 })
