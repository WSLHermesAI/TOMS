<#
.SYNOPSIS
  Packages a finished build into a folder (and a zip) you can hand out.
    -Target windows : dist\TOMS-windows\  toms_game.exe + assets\ + the MSVC runtime DLLs.
                      Copy the folder anywhere and double-click toms_game.exe.
    -Target web     : dist\TOMS-web\      index.html + toms_game.js/.wasm/.data (+ server MIME config).
                      Copy the folder's files to any web server.
  Called by build_windows.bat / build_web.bat after the build.
#>
param([Parameter(Mandatory = $true)][ValidateSet('windows', 'web')][string]$Target)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force $dist | Out-Null

function Reset-Dir([string]$path) {
    if (Test-Path $path) { Remove-Item -LiteralPath $path -Recurse -Force }
    New-Item -ItemType Directory -Force $path | Out-Null
}
function Zip-Dir([string]$dir, [string]$zip) {
    if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
    Compress-Archive -Path (Join-Path $dir '*') -DestinationPath $zip
}

if ($Target -eq 'windows') {
    $bin = Join-Path $root 'out\build\windows-shipping\bin'
    $exe = Join-Path $bin 'toms_game.exe'
    if (-not (Test-Path $exe)) { throw "No $exe. Build first: tools\build.cmd windows-shipping" }
    $out = Join-Path $dist 'TOMS-windows'
    Reset-Dir $out

    Copy-Item $exe $out
    # The game reads <exe dir>\assets\media and its sibling assets\data.
    New-Item -ItemType Directory -Force (Join-Path $out 'assets') | Out-Null
    Copy-Item (Join-Path $root 'assets\media') (Join-Path $out 'assets\media') -Recurse
    Copy-Item (Join-Path $root 'assets\data')  (Join-Path $out 'assets\data')  -Recurse

    # The MSVC C++ runtime (msvcp140, vcruntime140, ...) next to the exe ("app-local"), so the game
    # also starts on PCs without the Visual C++ Redistributable.
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $crt = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC') -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        ForEach-Object { Get-ChildItem (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue } |
        Select-Object -First 1
    if ($crt) {
        Copy-Item (Join-Path $crt.FullName '*.dll') $out
        Write-Host "[toms] MSVC runtime from $($crt.FullName)"
    } else {
        Write-Warning "MSVC runtime DLLs not found under $vs\VC\Redist; players may need the 'Visual C++ Redistributable (x64)'."
    }

    $font = Test-Path (Join-Path $out 'assets\media\wqy-zenhei.ttc')
    @"
Tower of the Sorcerer (TOMS) - Windows

Run:   double-click toms_game.exe
Needs: 64-bit Windows 10/11 with a Direct3D 11 capable GPU (any PC from the last ten years).
Saves: the "save" folder next to toms_game.exe.  Log: toms.log next to toms_game.exe.
Keys:  arrows/WASD move, Enter interact/attack, I inventory, B store, Tab stage select, Esc menu.
Keep the "assets" folder next to toms_game.exe.
$(if (-not $font) { "`nFont: wqy-zenhei.ttc is not included, so Windows' Microsoft JhengHei font is used.`n" })
"@ | Set-Content -Encoding UTF8 (Join-Path $out 'README.txt')

    Zip-Dir $out (Join-Path $dist 'TOMS-windows.zip')
}
else {
    $bin = Join-Path $root 'build-web-release-windows\bin'
    foreach ($f in 'toms_game.html', 'toms_game.js', 'toms_game.wasm', 'toms_game.data') {
        if (-not (Test-Path (Join-Path $bin $f))) { throw "No $bin\$f. Build first: tools\build_web.cmd release" }
    }
    $out = Join-Path $dist 'TOMS-web'
    Reset-Dir $out

    # index.html is the page itself, so the folder URL opens the game. It loads toms_game.js, which
    # loads toms_game.wasm and toms_game.data from the same folder.
    Copy-Item (Join-Path $bin 'toms_game.html') (Join-Path $out 'index.html')
    foreach ($f in 'toms_game.js', 'toms_game.wasm', 'toms_game.data') { Copy-Item (Join-Path $bin $f) $out }

    # Servers must send .wasm as application/wasm. Most do already; these cover Apache and IIS.
    @"
AddType application/wasm .wasm
AddType application/octet-stream .data
"@ | Set-Content -Encoding ASCII (Join-Path $out '.htaccess')
    @"
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <system.webServer>
    <staticContent>
      <remove fileExtension=".wasm" />
      <mimeMap fileExtension=".wasm" mimeType="application/wasm" />
      <remove fileExtension=".data" />
      <mimeMap fileExtension=".data" mimeType="application/octet-stream" />
    </staticContent>
  </system.webServer>
</configuration>
"@ | Set-Content -Encoding UTF8 (Join-Path $out 'web.config')
    @"
Tower of the Sorcerer (TOMS) - web version

Put every file of this folder into one folder on a web server (any static host: nginx, Apache,
IIS, GitHub Pages, Netlify, an S3 bucket, ...) and open that folder's URL (index.html).

- It must be served over http(s). Opening index.html from the disk (file://) does not work.
- .wasm must be served as "application/wasm" (most servers do; .htaccess / web.config set it for
  Apache / IIS).
- After uploading a new version, a hard reload (Ctrl+F5) avoids a cached old .data file.
- Saves are stored in the browser (IndexedDB) of each player.
- Local test: tools\serve_web.cmd in the source tree, or  python -m http.server 8099  in this folder.
"@ | Set-Content -Encoding UTF8 (Join-Path $out 'README.txt')

    Zip-Dir $out (Join-Path $dist 'TOMS-web.zip')
}

$size = (Get-ChildItem $out -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ("[toms] packaged: {0}  ({1:N1} MB)" -f $out, ($size / 1MB))
Write-Host ("[toms] zip:      {0}" -f (Join-Path $dist ("TOMS-$Target.zip")))
