<#
.SYNOPSIS
  Downloads ffmpeg into tools\ffmpeg\ (not in git), for the tools that write OGG sound (tools\audio\ogg.py,
  docs/engine/22_AUDIO_GENERATION.md). The game itself does not need it, and it does not ship with the game.

  get_ffmpeg.cmd            double-click: says what happened in a message box
  get_ffmpeg.cmd -NoGui     for scripts: text only, exit 0 when ffmpeg is ready, 1 when not
  get_ffmpeg.cmd -Force     downloads again even when it is there

  The build is BtbN's FFmpeg 9.0 for 64-bit Windows, LGPL: https://github.com/BtbN/FFmpeg-Builds/releases
  It is rebuilt daily under the same name, so it is checked against the SHA-256 list published in the same release.
#>
param(
    [switch]$NoGui,
    [switch]$Force,
    [string]$ReleaseUrl = 'https://github.com/BtbN/FFmpeg-Builds/releases/latest/download',
    [string]$ZipName = 'ffmpeg-n9.0-latest-win64-lgpl-9.0.zip',
    [string]$Sha256 = ''   # checks against this instead of the release's list (for a test, or a build you trust)
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest's progress bar makes it very slow
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$dest = Join-Path $PSScriptRoot 'ffmpeg'
$exe = Join-Path $dest 'ffmpeg.exe'

function Finish([bool]$ok, [string]$text) {
    Write-Host ''
    Write-Host $text -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })
    if (-not $NoGui) {
        Add-Type -AssemblyName System.Windows.Forms
        $icon = if ($ok) { 'Information' } else { 'Error' }
        [void][System.Windows.Forms.MessageBox]::Show($text, 'TOMS: ffmpeg', 'OK', $icon)
    }
    exit $(if ($ok) { 0 } else { 1 })
}

# ffmpeg's version line when this one runs and can write OGG Vorbis; empty when not.
function Test-Ffmpeg([string]$path) {
    if (-not (Test-Path $path)) { return '' }
    try {
        $version = (& $path -hide_banner -version 2>$null | Select-Object -First 1)
        $encoders = (& $path -hide_banner -encoders 2>$null) -join "`n"
    } catch { return '' }
    if ($encoders -notmatch 'libvorbis') { return '' }
    return "$version"
}

function Download([string]$url, [string]$file) {
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue   # Windows 10 and later: shows its progress
    if ($curl) {
        & $curl.Source -L --fail --retry 2 --progress-bar -o $file $url
        if ($LASTEXITCODE -ne 0) { throw "download failed (curl $LASTEXITCODE): $url" }
    } else {
        Invoke-WebRequest -Uri $url -OutFile $file -UseBasicParsing
    }
}

if (-not $Force) {
    $have = Test-Ffmpeg $exe
    if ($have) { Finish $true "ffmpeg is already here:`n$exe`n$have" }
}

$work = Join-Path ([IO.Path]::GetTempPath()) ('toms_ffmpeg_' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $work | Out-Null
try {
    $zip = Join-Path $work 'ffmpeg.zip'
    $expected = $Sha256
    if (-not $expected) {
        $shaFile = Join-Path $work 'checksums.sha256'
        Download "$ReleaseUrl/checksums.sha256" $shaFile
        $line = Get-Content $shaFile | Where-Object { $_ -match [regex]::Escape($ZipName) } | Select-Object -First 1
        if (-not $line) { throw "the release's SHA-256 list does not name $ZipName. Nothing was installed." }
        $expected = ($line.Trim() -split '\s+')[0]
    }
    Write-Host "Downloading ffmpeg (about 170 MB) ..."
    Write-Host "$ReleaseUrl/$ZipName"
    Download "$ReleaseUrl/$ZipName" $zip

    Write-Host 'Checking the download ...'
    $actual = (Get-FileHash -Algorithm SHA256 $zip).Hash
    if ($actual -ne $expected) { throw "the download is damaged or not the expected file (SHA-256 $actual, expected $expected). Nothing was installed." }

    Write-Host 'Unpacking ...'
    $unpacked = Join-Path $work 'unpacked'
    Expand-Archive -Path $zip -DestinationPath $unpacked
    $found = Get-ChildItem $unpacked -Recurse -Filter ffmpeg.exe | Select-Object -First 1
    if (-not $found) { throw 'the download has no ffmpeg.exe in it. Nothing was installed.' }
    $version = Test-Ffmpeg $found.FullName
    if (-not $version) { throw 'the downloaded ffmpeg does not run here, or cannot write OGG Vorbis. Nothing was installed.' }

    New-Item -ItemType Directory -Force $dest | Out-Null
    Copy-Item $found.FullName $exe -Force
    $license = Get-ChildItem (Split-Path (Split-Path $found.FullName)) -Filter 'LICENSE*' | Select-Object -First 1
    if ($license) { Copy-Item $license.FullName (Join-Path $dest 'LICENSE.txt') -Force }

    $final = Test-Ffmpeg $exe
    if (-not $final) { throw "ffmpeg was copied to $exe but does not run there." }
    Finish $true "ffmpeg is ready:`n$exe`n$final"
} catch {
    Finish $false "ffmpeg was not installed.`n$($_.Exception.Message)`n`nCheck the internet connection and run tools\get_ffmpeg.cmd again, or set the environment variable TOMS_FFMPEG to an ffmpeg.exe you already have."
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
