<# : batch part -- this file is both a batch file and a PowerShell script
@echo off
rem setup_new_pc.bat -- set up TOMS on a brand-new Windows PC (docs/11, docs/12).
rem   Double-click it. It works on its own: download just this file onto a new PC and run it.
rem   Installs Git + Visual Studio 2026 (C++ workload, CMake tools, Windows SDK), enables long paths,
rem   clones the repository, optionally creates an SSH key for pushing, then builds and tests.
rem   Every step that needs administrator rights is announced by a dialog first, so you know
rem   when Windows will ask "Do you want to allow this app to make changes to your device?".
setlocal
set "TOMS_SETUP_SELF=%~f0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "iex ([IO.File]::ReadAllText($env:TOMS_SETUP_SELF))"
set RC=%ERRORLEVEL%
echo.
pause
exit /b %RC%
#>

# ================================= PowerShell part =================================
$ErrorActionPreference = 'Continue'
$RepoHttps = 'https://github.com/WSLHermesAI/TOMS.git'
$RepoSsh   = 'git@github.com:WSLHermesAI/TOMS.git'
$LogFile   = Join-Path $env:TEMP 'TOMS_setup.log'
$Self      = $env:TOMS_SETUP_SELF
$SelfDir   = Split-Path -Parent $Self

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName Microsoft.VisualBasic
[System.Windows.Forms.Application]::EnableVisualStyles()

function Log([string]$msg, [string]$color = 'Gray') {
    $line = "[toms-setup] $msg"
    Write-Host $line -ForegroundColor $color
    Add-Content -Path $LogFile -Value ("{0:HH:mm:ss} {1}" -f (Get-Date), $msg) -Encoding UTF8
}

# Message boxes owned by an invisible top-most form, so they never hide behind the console.
function Show-Box([string]$text, [string]$title = 'TOMS setup', [string]$buttons = 'OK', [string]$icon = 'Information') {
    $owner = New-Object System.Windows.Forms.Form -Property @{ TopMost = $true; ShowInTaskbar = $false; Opacity = 0 }
    $owner.Show(); $owner.Activate()
    $r = [System.Windows.Forms.MessageBox]::Show($owner, $text, $title,
        [System.Windows.Forms.MessageBoxButtons]::$buttons, [System.Windows.Forms.MessageBoxIcon]::$icon)
    $owner.Close()
    return $r.ToString()
}

# Announces a UAC prompt before it appears.
function Show-AdminNotice([string]$what) {
    $text = "NEXT STEP NEEDS ADMINISTRATOR PERMISSION`n`n$what`n`n" +
            "After you click OK, Windows will dim the screen and ask:`n" +
            "    'Do you want to allow this app to make changes to your device?'`n`n" +
            "  >> Click  YES  to continue.`n`n" +
            "(If the prompt does not appear, look for a flashing shield icon on the taskbar.)"
    return (Show-Box $text 'TOMS setup - permission needed' 'OKCancel' 'Warning') -eq 'OK'
}

function Update-PathFromRegistry {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}


# ------------------------------------------------------------------ detection
$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$VsComponents = 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                'Microsoft.VisualStudio.Component.VC.CMake.Project'
# The Windows 11 SDK comes with the workload's recommended components; it is checked in the registry below.

function Get-VsInfo {
    # Returns @{ Path = <any VS 2026+>; Complete = <has every component we need> }
    $info = @{ Path = $null; Complete = $false }
    if (-not (Test-Path $VsWhere)) { return $info }
    $info.Path = & $VsWhere -latest -products * -version '[18.0,' -property installationPath | Select-Object -First 1
    if ($info.Path) {
        $full = & $VsWhere -latest -products * -version '[18.0,' -requires $VsComponents[0] $VsComponents[1] -property installationPath
        # Any Windows 10/11 SDK is fine (docs/02 item 4); check the registry rather than one exact component.
        $sdkRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction SilentlyContinue).KitsRoot10
        $hasSdk = $sdkRoot -and @(Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory -ErrorAction SilentlyContinue |
                                  Where-Object { $_.Name -match '^10\.' }).Count -gt 0
        $info.Complete = [bool]$full -and $hasSdk
    }
    return $info
}

function Test-LongPaths {
    (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -ErrorAction SilentlyContinue).LongPathsEnabled -eq 1
}

Set-Content -Path $LogFile -Value "TOMS setup log $(Get-Date)" -Encoding UTF8
Log "log file: $LogFile"

# ------------------------------------------------------------------ 0. welcome
$welcome = "This will set up TOMS on this PC:`n`n" +
           "  1. Install Git for Windows and Visual Studio 2026 Community`n" +
           "     (C++ workload, CMake tools, Windows SDK) - only what is missing`n" +
           "  2. Enable Windows long paths`n" +
           "  3. Clone the TOMS repository`n" +
           "  4. (optional) Create an SSH key so you can push to GitHub`n" +
           "  5. (optional) AI art tools: install ComfyUI + models here, or use a remote server`n" +
           "  6. (optional) Build and test the game`n`n" +
           "Steps 1-2 need administrator permission. A dialog like this one will`n" +
           "warn you each time BEFORE Windows asks, so you know when to click 'Yes'.`n`n" +
           "Installing Visual Studio downloads several GB and can take 30-60 minutes.`n`nContinue?"
if ((Show-Box $welcome 'TOMS setup' 'YesNo' 'Question') -ne 'Yes') { Log 'cancelled by user'; exit 1 }

# ------------------------------------------------------------------ 1. winget
if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Show-Box ("'winget' (App Installer) was not found. It ships with Windows 10/11.`n`n" +
              "Install or update 'App Installer' from the Microsoft Store, then run this file again.`n`n" +
              "The Store page opens after you click OK.") 'TOMS setup - winget missing' 'OK' 'Error' | Out-Null
    Start-Process 'ms-windows-store://pdp/?productid=9NBLGGH4NNS1'
    exit 1
}

# ------------------------------------------------------------------ 2. admin work (one elevated batch)
Update-PathFromRegistry
$needGit = -not (Get-Command git -ErrorAction SilentlyContinue)
$vs = Get-VsInfo
$needLongPaths = -not (Test-LongPaths)
Log ("git present={0}; Visual Studio={1} complete={2}; long paths={3}" -f (-not $needGit), $vs.Path, $vs.Complete, (-not $needLongPaths))

$adminLines = New-Object System.Collections.Generic.List[string]
$todo = New-Object System.Collections.Generic.List[string]
$adminLines.Add('$ErrorActionPreference = "Continue"')
$adminLines.Add("Start-Transcript -Path '$env:TEMP\TOMS_setup_admin.log' -Force | Out-Null")
$winget = 'winget install -e --source winget --accept-source-agreements --accept-package-agreements --disable-interactivity'
if ($needGit) {
    $todo.Add('  * Install Git for Windows')
    $adminLines.Add("Write-Host 'Installing Git for Windows...' -ForegroundColor Cyan")
    $adminLines.Add("$winget --id Git.Git --scope machine")
}
$addArgs = ($VsComponents | ForEach-Object { "--add $_" }) -join ' '
if (-not $vs.Path) {
    $todo.Add('  * Install Visual Studio 2026 Community + Desktop development with C++ (30-60 min)')
    $adminLines.Add("Write-Host 'Installing Visual Studio 2026 Community (this takes a while; a progress window will appear)...' -ForegroundColor Cyan")
    $adminLines.Add("$winget --id Microsoft.VisualStudio.Community --override '--wait --passive --norestart --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended $addArgs'")
} elseif (-not $vs.Complete) {
    $todo.Add("  * Add C++ workload / CMake tools / Windows SDK to the existing Visual Studio")
    $setupExe = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
    $adminLines.Add("Write-Host 'Adding the C++ components to Visual Studio (a progress window will appear)...' -ForegroundColor Cyan")
    $adminLines.Add("& '$setupExe' modify --installPath '$($vs.Path)' --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended $addArgs --passive --norestart | Out-Default")
}
if ($needLongPaths) {
    $todo.Add('  * Enable Windows long paths (registry: LongPathsEnabled = 1)')
    $adminLines.Add("Write-Host 'Enabling long paths...' -ForegroundColor Cyan")
    $adminLines.Add("New-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -Name LongPathsEnabled -Value 1 -PropertyType DWORD -Force | Out-Null")
}
$adminLines.Add('Stop-Transcript | Out-Null')

if ($todo.Count -gt 0) {
    $what = "These items will be installed / changed (all in ONE administrator window):`n`n" + ($todo -join "`n") +
            "`n`nA second black window will show the progress. Do not close it;`nthis setup waits until it finishes."
    if (Show-AdminNotice $what) {
        $adminScript = Join-Path $env:TEMP 'TOMS_setup_admin.ps1'
        Set-Content -Path $adminScript -Value ($adminLines -join "`r`n") -Encoding UTF8
        Log 'starting the elevated installer window'
        try {
            $p = Start-Process powershell -Verb RunAs -Wait -PassThru -ErrorAction Stop `
                 -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$adminScript`""
            Log "elevated window finished (exit $($p.ExitCode)); details: $env:TEMP\TOMS_setup_admin.log"
        } catch {
            Log 'administrator permission was refused' 'Yellow'
            Show-Box ("Windows permission was not granted (you clicked 'No'), so nothing was installed.`n`n" +
                      "Run this file again and click 'Yes' when Windows asks.") 'TOMS setup' 'OK' 'Warning' | Out-Null
            exit 1
        }
    } else { Log 'admin step skipped by user' 'Yellow' }
} else {
    Log 'Git, Visual Studio and long paths are already set up - no administrator permission needed' 'Green'
}

# Re-check after installing (never trust an exit code alone).
Update-PathFromRegistry
$gitCmd = Get-Command git -ErrorAction SilentlyContinue
$vs = Get-VsInfo
$problems = @()
if (-not $gitCmd)      { $problems += 'Git is still not installed.' }
if (-not $vs.Complete) { $problems += 'Visual Studio with the C++ workload, CMake tools and Windows SDK is still incomplete.' }
if ($problems) {
    Log ($problems -join ' ') 'Red'
    Show-Box (($problems -join "`n") + "`n`nSee $LogFile and $env:TEMP\TOMS_setup_admin.log.`n" +
              "A reboot sometimes helps; then run this file again.") 'TOMS setup - install incomplete' 'OK' 'Error' | Out-Null
    if (-not $gitCmd) { exit 1 }   # nothing below works without git
}
$git = $gitCmd.Source

# ------------------------------------------------------------------ 3. git settings
& $git config --global core.longpaths true
Log 'git config --global core.longpaths true'
# Always ask: these end up in every commit (public on GitHub), so never fill them in from the Windows login.
$gName  = (& $git config --global user.name)
$gEmail = (& $git config --global user.email)
$idNote = "This is written into every commit you push, and anyone can read it on GitHub.`n" +
          "It is NOT a login. Tip: GitHub offers a private address like`n" +
          "  <id>+<account>@users.noreply.github.com  (GitHub > Settings > Emails).`n`n"
$newName = [Microsoft.VisualBasic.Interaction]::InputBox($idNote + "Name for git commits (user.name).`n" +
    "Current: $(if ($gName) { $gName } else { '(not set)' })`nLeave empty to keep the current value.", 'TOMS setup - git identity', '')
if ($newName) { $gName = $newName; & $git config --global user.name $gName }
$newEmail = [Microsoft.VisualBasic.Interaction]::InputBox($idNote + "E-mail for git commits (user.email).`n" +
    "Current: $(if ($gEmail) { $gEmail } else { '(not set)' })`nLeave empty to keep the current value.", 'TOMS setup - git identity', '')
if ($newEmail) { $gEmail = $newEmail; & $git config --global user.email $gEmail }
Log "git identity: user.name='$gName' user.email='$gEmail'"

# ------------------------------------------------------------------ 4. clone
$repo = $null
if ((Test-Path (Join-Path $SelfDir '.git')) -and (Test-Path (Join-Path $SelfDir 'tools\check_env.ps1'))) {
    $repo = $SelfDir
    Log "running inside an existing checkout: $repo"
    & $git -C $repo submodule update --init --recursive
} else {
    $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
    $dlg.Description = 'Choose the folder to put TOMS in (a "TOMS" folder is created inside it). A short path such as C:\Work is best.'
    $dlg.SelectedPath = 'C:\'
    $owner = New-Object System.Windows.Forms.Form -Property @{ TopMost = $true; ShowInTaskbar = $false; Opacity = 0 }
    $owner.Show()
    $ok = $dlg.ShowDialog($owner); $owner.Close()
    if ($ok -ne 'OK') { Log 'no folder chosen' 'Yellow'; exit 1 }
    $repo = Join-Path $dlg.SelectedPath 'TOMS'
    if (Test-Path (Join-Path $repo '.git')) {
        Log "$repo already exists - updating it"
        & $git -C $repo pull --ff-only
        & $git -C $repo submodule update --init --recursive
    } else {
        Log "cloning $RepoHttps into $repo (public repository: no login needed)"
        & $git clone --recurse-submodules $RepoHttps $repo
        if ($LASTEXITCODE -ne 0) {
            Show-Box "git clone failed (exit $LASTEXITCODE). Check the internet connection and the console output." 'TOMS setup' 'OK' 'Error' | Out-Null
            exit 1
        }
    }
}
& $git -C $repo config core.longpaths true

# ------------------------------------------------------------------ 5. SSH key (only needed to push)
$sshText = "Do you want to be able to PUSH to GitHub from this PC?`n`n" +
           "Cloning and building do not need this (the repository is public).`n`n" +
           "'Yes' creates an SSH key for this PC (one PC = one key, docs/12 method A),`n" +
           "copies the public key to the clipboard and opens GitHub so you can add it`n" +
           "to the account that owns the repository (WSLHermesAI)."
if ((Show-Box $sshText 'TOMS setup - SSH key' 'YesNo' 'Question') -eq 'Yes') {
    $gitRoot = Split-Path -Parent (Split-Path -Parent $git)          # ...\Git\cmd\git.exe -> ...\Git
    $ssh = Join-Path $gitRoot 'usr\bin\ssh.exe'                      # the ssh that git itself uses
    if (-not (Test-Path $ssh)) { $ssh = 'ssh' }
    $keygen = Join-Path $env:WINDIR 'System32\OpenSSH\ssh-keygen.exe'
    if (-not (Test-Path $keygen)) { $keygen = Join-Path $gitRoot 'usr\bin\ssh-keygen.exe' }
    $sshDir = Join-Path $env:USERPROFILE '.ssh'
    $key = Join-Path $sshDir 'id_ed25519'
    if (-not (Test-Path $sshDir)) { New-Item -ItemType Directory $sshDir | Out-Null }

    # The key's label (comment) is the last word of the .pub line; ask for it, never default to the Windows login.
    # PowerShell 5.1 drops empty arguments, so an empty label is passed as '""'.
    $labelNote = "Label for the SSH key (the text at the end of the public key, shown next to the fingerprint).`n" +
                 "It is only a note to recognise the key; it is not a login.`nLeave empty for no label."
    if (Test-Path $key) {
        Log "an SSH key already exists ($key) - using it"
        $oldLabel = ((Get-Content "$key.pub" -Raw) -split '\s+', 3)[2]
        if ($oldLabel) { $oldLabel = $oldLabel.Trim() }
        $label = [Microsoft.VisualBasic.Interaction]::InputBox("An SSH key already exists.`nCurrent label: $(if ($oldLabel) { $oldLabel } else { '(none)' })`n`n" +
            "Type a new label to replace it (it is only a note; it is not a login).`n" +
            "Leave empty or click Cancel to keep the current label.", 'TOMS setup - SSH key label', '')
        if ($label -and $label -ne $oldLabel) {
            Show-Box "If the key has a passphrase, type it in the BLACK CONSOLE WINDOW after you click OK." 'TOMS setup - SSH key label' | Out-Null
            & $keygen -c -C $label -f $key
            Log "SSH key label changed to '$label'"
        }
    } else {
        $label = [Microsoft.VisualBasic.Interaction]::InputBox($labelNote, 'TOMS setup - SSH key label', '')
        Show-Box ("The key is created in the BLACK CONSOLE WINDOW.`n`n" +
                  "After you click OK, switch to that window. It asks for a passphrase twice:`n" +
                  "type one (recommended, docs/12 section 7) or just press Enter twice for none.") 'TOMS setup - SSH key' | Out-Null
        & $keygen -t ed25519 -C $(if ($label) { $label } else { '""' }) -f $key
        if (-not (Test-Path "$key.pub")) {
            Show-Box 'ssh-keygen did not create a key. See the console output.' 'TOMS setup' 'OK' 'Error' | Out-Null
        }
    }

    if (Test-Path "$key.pub") {
        Get-Content "$key.pub" -Raw | Set-Clipboard
        Log "public key copied to the clipboard: $key.pub"
        Start-Process 'https://github.com/settings/ssh/new'
        $added = Show-Box ("Your PUBLIC key is now on the clipboard, and GitHub opened in your browser.`n`n" +
                           "  1. Sign in as  WSLHermesAI  (the account that owns TOMS)`n" +
                           "  2. Title: anything, e.g. '$env:COMPUTERNAME'`n" +
                           "  3. Key: press Ctrl+V`n" +
                           "  4. Click 'Add SSH key'`n`n" +
                           "Click OK when done (Cancel to skip the test).`n`n" +
                           "Never share the file WITHOUT .pub - that is the private key.") 'TOMS setup - add the key on GitHub' 'OKCancel' 'Information'
        if ($added -eq 'OK') {
            # GitHub has no shell, so ssh -T exits with 1 even on success: read the text (docs/12 section 5.1).
            $reply = (& $ssh -T -o StrictHostKeyChecking=accept-new git@github.com 2>&1 | ForEach-Object { "$_" }) -join "`n"
            Log "ssh -T: $reply"
            if ($reply -match 'Hi ([^!]+)!') {
                $account = $Matches[1]
                & $git -C $repo remote set-url origin $RepoSsh
                Log "SSH works as $account; origin -> $RepoSsh" 'Green'
                $note = if ($account -ne 'WSLHermesAI') { "`n`nWarning: the key belongs to '$account', not WSLHermesAI. Push works only if that account has write access." } else { '' }
                Show-Box "SSH works: GitHub says 'Hi $account!'.`nThe remote now uses SSH, so 'git push' will work.$note" 'TOMS setup - SSH key' | Out-Null
            } else {
                Show-Box ("GitHub did not accept the key yet:`n`n$reply`n`n" +
                          "Check that it was added to the right account, then test later with:`n    ssh -T git@github.com`n" +
                          "The remote stays HTTPS (clone/pull still work).") 'TOMS setup - SSH key' 'OK' 'Warning' | Out-Null
            }
        }
    }
}

# ------------------------------------------------------------------ 5b. AI art tools (optional)
# Asks: install ComfyUI + models here, use a ComfyUI server on another machine, or skip. No admin rights needed.
$aiArt = Join-Path $repo 'tools\setup_ai_art.ps1'
if (Test-Path $aiArt) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $aiArt -FromSetup
    Log "AI art setup finished (exit $LASTEXITCODE); rerun any time: tools\setup_ai_art.cmd"
}

# ------------------------------------------------------------------ 6. environment check + build + test
Log 'running tools\check_env.cmd -NoGui'
& "$repo\tools\check_env.cmd" -NoGui
$envOk = ($LASTEXITCODE -eq 0)

$buildText = "Build the game now?  (tools\build.cmd windows-release, then tools\test.cmd)`n`n" +
             "The first build downloads and compiles bgfx, SDL3, Dear ImGui and glm:`nthis takes several minutes." +
             $(if (-not $envOk) { "`n`nNote: the environment check reported missing REQUIRED items (see the console)." } else { '' })
$built = $false
if ((Show-Box $buildText 'TOMS setup - build' 'YesNo' 'Question') -eq 'Yes') {
    $env:TOMS_NO_POPUPS = '1'
    & "$repo\tools\build.cmd" windows-release
    $built = ($LASTEXITCODE -eq 0) -and (Test-Path "$repo\out\build\windows-release\bin\toms_game.exe")
    Log "build ok = $built" $(if ($built) { 'Green' } else { 'Red' })
    if ($built) {
        & "$repo\tools\test.cmd" windows-release
        $testsOk = ($LASTEXITCODE -eq 0)
        Log "tests ok = $testsOk" $(if ($testsOk) { 'Green' } else { 'Red' })
    }
}

# ------------------------------------------------------------------ 7. summary
$summary = "Setup finished.`n`nRepository: $repo`nLog: $LogFile`n`n" +
           "Environment check: " + $(if ($envOk) { 'OK' } else { 'MISSING ITEMS (see console)' }) + "`n"
if ($built) {
    $summary += "Build: OK`nTests: " + $(if ($testsOk) { 'all passed' } else { 'FAILED (see console)' }) +
                "`n`nStart the game now?"
    if ((Show-Box $summary 'TOMS setup - done' 'YesNo' 'Information') -eq 'Yes') {
        Start-Process "$repo\out\build\windows-release\bin\toms_game.exe" -WorkingDirectory "$repo\out\build\windows-release\bin"
    }
} else {
    $summary += "Build: not run (or failed). Later: tools\build.cmd windows-release`n`n" +
                "Optional extras (editor = Qt, web = Emscripten) are in docs\02_INSTALL_WINDOWS.md."
    Show-Box $summary 'TOMS setup - done' | Out-Null
}
Start-Process explorer.exe $repo
exit 0
