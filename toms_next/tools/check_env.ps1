<#
.SYNOPSIS
  Checks everything toms_next needs on Windows and shows a popup with download links for
  anything missing. Run it before opening the folder in Visual Studio (or double-click
  tools\check_env.cmd).

.PARAMETER NoGui
  Console output only (no popup). Use this in CI.

.OUTPUTS
  Exit code 0 = all required items present (optional ones may be missing), 1 = something required is missing.
#>
param([switch]$NoGui)

$ErrorActionPreference = 'Continue'
$root   = Split-Path -Parent $PSScriptRoot            # toms_next/
$legacy = Split-Path -Parent $root                    # TOMS/
$results = New-Object System.Collections.Generic.List[object]

function Add-Result([string]$name, [bool]$ok, [bool]$required, [string]$detail, [string]$fix, [string]$link) {
    $results.Add([pscustomobject]@{ Name = $name; Ok = $ok; Required = $required; Detail = $detail; Fix = $fix; Link = $link })
}

function Get-VersionFromText([string]$text) {
    if ($text -match '(\d+)\.(\d+)(\.(\d+))?') { return [version]("{0}.{1}.{2}" -f $Matches[1], $Matches[2], $(if ($Matches[4]) { $Matches[4] } else { 0 })) }
    return $null
}

# ---------------------------------------------------------------------------------------------
# 1. Windows 64-bit
# ---------------------------------------------------------------------------------------------
$os = [Environment]::OSVersion.Version
Add-Result "Windows 10/11 64-bit" ([Environment]::Is64BitOperatingSystem -and $os.Major -ge 10) $true `
    ("Windows {0}, 64-bit={1}" -f $os, [Environment]::Is64BitOperatingSystem) `
    "Use 64-bit Windows 10 or 11." "https://www.microsoft.com/windows"

# ---------------------------------------------------------------------------------------------
# 2-3. Visual Studio + C++ workload + CMake tools
# ---------------------------------------------------------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsDir = $null; $vsName = $null
if (Test-Path $vswhere) {
    $vsDir  = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $vsName = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property displayName
    $vsVer  = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
}
$vsOk = [bool]$vsDir -and ((Get-VersionFromText $vsVer) -ge [version]'17.0.0')
Add-Result "Visual Studio 2022+ with 'Desktop development with C++'" $vsOk $true `
    $(if ($vsDir) { "$vsName $vsVer" } else { "not found" }) `
    "Install Visual Studio 2022 or newer (Community is free) and tick the workload 'Desktop development with C++'." `
    "https://visualstudio.microsoft.com/downloads/"

$cmakeTools = $null
if (Test-Path $vswhere) {
    $cmakeTools = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
}
Add-Result "Visual Studio component 'C++ CMake tools for Windows'" ([bool]$cmakeTools) $true `
    $(if ($cmakeTools) { "installed" } else { "missing" }) `
    "Visual Studio Installer > Modify > Individual components > 'C++ CMake tools for Windows' (it provides CMake + Ninja and 'Open Folder' support)." `
    "https://learn.microsoft.com/cpp/build/cmake-projects-in-visual-studio"

# ---------------------------------------------------------------------------------------------
# 4. Windows SDK
# ---------------------------------------------------------------------------------------------
$sdkRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction SilentlyContinue).KitsRoot10
$sdkVers = @()
if ($sdkRoot -and (Test-Path (Join-Path $sdkRoot 'Include'))) {
    $sdkVers = Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory | Where-Object { $_.Name -match '^10\.' } | ForEach-Object { $_.Name }
}
Add-Result "Windows 10/11 SDK" ($sdkVers.Count -gt 0) $true `
    $(if ($sdkVers) { ($sdkVers -join ', ') } else { "not found" }) `
    "Visual Studio Installer > Individual components > 'Windows 11 SDK' (any 10.0.2xxxx)." `
    "https://developer.microsoft.com/windows/downloads/windows-sdk/"

# ---------------------------------------------------------------------------------------------
# 5-6. CMake >= 3.24 and Ninja (Visual Studio's own copies are fine)
# ---------------------------------------------------------------------------------------------
$cmakeExe = $null
if ($vsDir) {
    $c = Join-Path $vsDir 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (Test-Path $c) { $cmakeExe = $c }
}
if (-not $cmakeExe) { $cmd = Get-Command cmake -ErrorAction SilentlyContinue; if ($cmd) { $cmakeExe = $cmd.Source } }
$cmakeVer = $null
if ($cmakeExe) { $cmakeVer = Get-VersionFromText ((& $cmakeExe --version | Select-Object -First 1) -join '') }
Add-Result "CMake 3.24+" ($cmakeVer -and $cmakeVer -ge [version]'3.24.0') $true `
    $(if ($cmakeExe) { "$cmakeVer ($cmakeExe)" } else { "not found" }) `
    "Comes with Visual Studio's 'C++ CMake tools for Windows'; or install CMake from cmake.org." `
    "https://cmake.org/download/"

$ninjaExe = $null
if ($vsDir) {
    $n = Join-Path $vsDir 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
    if (Test-Path $n) { $ninjaExe = $n }
}
if (-not $ninjaExe) { $cmd = Get-Command ninja -ErrorAction SilentlyContinue; if ($cmd) { $ninjaExe = $cmd.Source } }
Add-Result "Ninja" ([bool]$ninjaExe) $true `
    $(if ($ninjaExe) { $ninjaExe } else { "not found" }) `
    "Comes with Visual Studio's 'C++ CMake tools for Windows'; or download ninja-win.zip." `
    "https://github.com/ninja-build/ninja/releases"

# ---------------------------------------------------------------------------------------------
# 7. Git (FetchContent clones bgfx, SDL3, ImGui, glm on the first configure)
# ---------------------------------------------------------------------------------------------
$git = Get-Command git -ErrorAction SilentlyContinue
Add-Result "Git" ([bool]$git) $true `
    $(if ($git) { (& git --version) } else { "not on PATH" }) `
    "Install Git for Windows (keep 'Git from the command line and also from 3rd-party software')." `
    "https://git-scm.com/download/win"

# ---------------------------------------------------------------------------------------------
# 8. Network (only needed until the first configure has downloaded the dependencies)
# ---------------------------------------------------------------------------------------------
$depsCached = @(Get-ChildItem (Join-Path $root 'out\build') -Directory -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName '_deps\bgfx-src') }).Count -gt 0
$net = $false
try {
    $req = [System.Net.WebRequest]::Create('https://github.com')
    $req.Method = 'HEAD'; $req.Timeout = 5000
    $resp = $req.GetResponse(); $resp.Close(); $net = $true
} catch { $net = $false }
Add-Result "Internet access to github.com" ($net -or $depsCached) (-not $depsCached) `
    $(if ($net) { "reachable" } elseif ($depsCached) { "offline, but dependencies are already downloaded" } else { "unreachable" }) `
    "The first configure downloads bgfx, SDL3, Dear ImGui and glm from GitHub. Check proxy/firewall settings." `
    "https://github.com"

# ---------------------------------------------------------------------------------------------
# 9. Long paths (bgfx's third-party tree is deep)
# ---------------------------------------------------------------------------------------------
$lp = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -ErrorAction SilentlyContinue).LongPathsEnabled
$deep = ($root.Length -gt 60)
Add-Result "Long path support" (($lp -eq 1) -or -not $deep) $false `
    ("LongPathsEnabled={0}, project path length={1}" -f $lp, $root.Length) `
    "The project path is long; enable Win32 long paths (admin PowerShell: New-ItemProperty -Path HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem -Name LongPathsEnabled -Value 1 -PropertyType DWORD -Force) and run: git config --global core.longpaths true. Or move the checkout closer to the drive root." `
    "https://learn.microsoft.com/windows/win32/fileio/maximum-file-path-limitation"

# ---------------------------------------------------------------------------------------------
# 10. The original TOMS project next to toms_next
# ---------------------------------------------------------------------------------------------
$needed = 'src\game\core\game.h', 'src\engine\render_iface.h', 'assets\sprites', 'assets\fonts', 'data\stages',
          'external\json\json.hpp', 'external\stb\stb_image.h', 'external\miniaudio\miniaudio.h', 'editor\src\mainwindow.cpp'
$missing = @($needed | Where-Object { -not (Test-Path (Join-Path $legacy $_)) })
Add-Result "TOMS game code, assets and data (parent folder)" ($missing.Count -eq 0) $true `
    $(if ($missing.Count -eq 0) { $legacy } else { "missing: " + ($missing -join ', ') }) `
    "toms_next must stay inside the TOMS checkout (it compiles ../src and reads ../assets, ../data)." `
    "https://github.com/WSLHermesAI/TOMS"

# ---------------------------------------------------------------------------------------------
# 11. CJK font (gitignored, so a fresh clone does not have it)
# ---------------------------------------------------------------------------------------------
$font = Join-Path $legacy 'assets\wqy-zenhei.ttc'
$msjh = Join-Path $env:WINDIR 'Fonts\msjh.ttc'
$fontOk = Test-Path $font
Add-Result "CJK font assets\wqy-zenhei.ttc" $fontOk (-not (Test-Path $msjh)) `
    $(if ($fontOk) { "present" } elseif (Test-Path $msjh) { "missing -> the game falls back to Microsoft JhengHei (msjh.ttc)" } else { "missing, and no Windows fallback font" }) `
    "Download WenQuanYi Zen Hei (wqy-zenhei-*.tar.gz), extract wqy-zenhei.ttc and copy it to TOMS\assets\." `
    "https://sourceforge.net/projects/wqy/files/wqy-zenhei/"

# ---------------------------------------------------------------------------------------------
# 12. D3D shader compiler DLL (shaderc -> Direct3D shaders)
# ---------------------------------------------------------------------------------------------
$d3dc = Join-Path $env:WINDIR 'System32\d3dcompiler_47.dll'
Add-Result "d3dcompiler_47.dll" (Test-Path $d3dc) $true `
    $(if (Test-Path $d3dc) { "present" } else { "missing" }) `
    "Part of Windows and the Windows SDK. Install the Windows SDK or run Windows Update." `
    "https://developer.microsoft.com/windows/downloads/windows-sdk/"

# ---------------------------------------------------------------------------------------------
# 13. Disk space
# ---------------------------------------------------------------------------------------------
$drive = (Get-Item $root).PSDrive
$freeGb = [math]::Round($drive.Free / 1GB, 1)
Add-Result "Free disk space (>= 5 GB on $($drive.Name):)" ($freeGb -ge 5) $true "$freeGb GB free" `
    "A full debug + release build with bgfx tools takes about 3-4 GB." "https://support.microsoft.com/windows/free-up-drive-space-in-windows"

# ---------------------------------------------------------------------------------------------
# 14. Qt 6 MSVC 64-bit kit (optional: only toms_editor needs it)
# ---------------------------------------------------------------------------------------------
$qtCandidates = New-Object System.Collections.Generic.List[string]
foreach ($e in 'QT_ROOT_DIR', 'QTDIR') {
    $v = [Environment]::GetEnvironmentVariable($e)
    if ($v) { $qtCandidates.Add($v) }
}
$q6 = [Environment]::GetEnvironmentVariable('Qt6_DIR')
if ($q6) { $qtCandidates.Add((Resolve-Path (Join-Path $q6 '..\..\..') -ErrorAction SilentlyContinue).Path) }
foreach ($base in 'C:\Qt', 'D:\Qt', (Join-Path $env:USERPROFILE 'Qt')) {
    if (Test-Path $base) {
        Get-ChildItem $base -Directory -Filter '6.*' -ErrorAction SilentlyContinue | ForEach-Object {
            Get-ChildItem $_.FullName -Directory -ErrorAction SilentlyContinue | ForEach-Object { $qtCandidates.Add($_.FullName) }
        }
    }
}
$qtKit = $null; $qtMingw = $null
foreach ($k in ($qtCandidates | Where-Object { $_ } | Select-Object -Unique | Sort-Object -Descending)) {
    $hasCore = Test-Path (Join-Path $k 'bin\Qt6Core.dll')
    $hasCmake = Test-Path (Join-Path $k 'lib\cmake\Qt6\Qt6Config.cmake')
    if ($hasCore -and $hasCmake) {
        if ($k -match 'msvc' -and $k -match '64') { $qtKit = $k; break }
        elseif (-not $qtMingw) { $qtMingw = $k }
    }
}
$qtDetail = "not found"
if ($qtKit) { $qtDetail = $qtKit } elseif ($qtMingw) { $qtDetail = "only a non-MSVC kit found: $qtMingw (MinGW cannot link with MSVC)" }
Add-Result "Qt 6.5+ (MSVC 2022 64-bit kit) - editor only" ([bool]$qtKit) $false $qtDetail `
    "Qt Online Installer > Qt 6.8 (LTS) > tick 'MSVC 2022 64-bit'. Install to C:\Qt (auto-detected) or set QTDIR to the kit folder. Without Qt only toms_game is built." `
    "https://www.qt.io/download-qt-installer-oss"

# ---------------------------------------------------------------------------------------------
# 15. Optional GPU extras
# ---------------------------------------------------------------------------------------------
$vk = Test-Path (Join-Path $env:WINDIR 'System32\vulkan-1.dll')
Add-Result "Vulkan runtime (for --renderer=vulkan)" $vk $false `
    $(if ($vk) { "present" } else { "missing (Direct3D 11/12 still work)" }) `
    "Update the GPU driver (NVIDIA/AMD/Intel drivers include the Vulkan runtime)." `
    "https://www.vulkan.org/tools#download-these-essential-development-tools"
$rdoc = (Test-Path 'C:\Program Files\RenderDoc\renderdoc.dll')
Add-Result "RenderDoc (GPU frame debugger)" $rdoc $false `
    $(if ($rdoc) { "installed" } else { "not installed (optional)" }) `
    "Optional: capture and inspect bgfx frames." "https://renderdoc.org/"

# ---------------------------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------------------------
Write-Host ""
Write-Host "toms_next environment check ($root)" -ForegroundColor Cyan
Write-Host ("-" * 78)
foreach ($r in $results) {
    if ($r.Ok) { $tag = "[ OK ]"; $color = 'Green' }
    elseif ($r.Required) { $tag = "[MISS]"; $color = 'Red' }
    else { $tag = "[WARN]"; $color = 'Yellow' }
    Write-Host ("{0} {1}" -f $tag, $r.Name) -ForegroundColor $color
    Write-Host ("       {0}" -f $r.Detail)
    if (-not $r.Ok) {
        Write-Host ("       fix : {0}" -f $r.Fix)
        Write-Host ("       link: {0}" -f $r.Link)
    }
}
Write-Host ("-" * 78)

$missReq = @($results | Where-Object { -not $_.Ok -and $_.Required })
$missOpt = @($results | Where-Object { -not $_.Ok -and -not $_.Required })
if ($missReq.Count -eq 0 -and $missOpt.Count -eq 0) {
    Write-Host "Everything is installed. Open the toms_next folder in Visual Studio (File > Open > Folder)." -ForegroundColor Green
} elseif ($missReq.Count -eq 0) {
    Write-Host "Required tools OK. $($missOpt.Count) optional item(s) missing (see above)." -ForegroundColor Yellow
} else {
    Write-Host "$($missReq.Count) required item(s) missing. Install them, then run this check again." -ForegroundColor Red
}

if (-not $NoGui -and ($missReq.Count + $missOpt.Count) -gt 0) {
    $sb = New-Object System.Text.StringBuilder
    if ($missReq.Count) {
        [void]$sb.AppendLine("REQUIRED - the build cannot work without these:")
        foreach ($r in $missReq) { [void]$sb.AppendLine("  * $($r.Name)`n      $($r.Fix)`n      $($r.Link)") }
        [void]$sb.AppendLine("")
    }
    if ($missOpt.Count) {
        [void]$sb.AppendLine("OPTIONAL:")
        foreach ($r in $missOpt) { [void]$sb.AppendLine("  * $($r.Name)`n      $($r.Fix)`n      $($r.Link)") }
    }
    [void]$sb.AppendLine("")
    [void]$sb.AppendLine("Details: toms_next\docs\02_INSTALL_WINDOWS.md")
    $title = if ($missReq.Count) { "TOMS: required tools are missing" } else { "TOMS: optional tools are missing" }
    $links = @($missReq + $missOpt | ForEach-Object { $_.Link } | Where-Object { $_ -match '^https?://' })
    & (Join-Path $PSScriptRoot 'show_message.ps1') -Title $title -Message $sb.ToString() -Links $links
}

if ($missReq.Count -gt 0) { exit 1 } else { exit 0 }
