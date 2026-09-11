# run_web_debug.ps1 — launched as the "program" of a Visual Studio launch.vs.json
# debug configuration for the toms_web (Emscripten) CMake target. VS builds
# toms_web.html into web-debug/ first, then runs this script instead of trying
# to exec the .html file itself (which isn't a native process VS can launch).
#
# It starts the local HTTP server Emscripten needs (module loading fails over
# file://) if one isn't already listening on the port, then opens the built
# debug page in Chrome (falling back to Edge) so it behaves like "F5 runs the app".
param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot),
    [int]$Port = 8099,
    [string]$Page = "web-debug/toms_web.html"
)

$listening = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
if (-not $listening) {
    Start-Process -FilePath "python" -ArgumentList "-m", "http.server", "$Port" `
        -WorkingDirectory $RepoRoot -WindowStyle Hidden
    Start-Sleep -Seconds 1
}

$browser = "C:\Program Files\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $browser)) {
    $browser = "C:\Program Files (x86)\Google\Chrome\Application\chrome.exe"
}
if (-not (Test-Path $browser)) {
    $browser = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
}
if (-not (Test-Path $browser)) {
    Write-Error "No Chrome or Edge install found in the usual locations. Edit run_web_debug.ps1 to point at your browser."
    exit 1
}

Start-Process -FilePath $browser -ArgumentList "--new-window", "http://localhost:$Port/$Page"
