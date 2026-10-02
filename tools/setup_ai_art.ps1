<#
.SYNOPSIS
  Optional AI-art setup for TOMS: either install ComfyUI + the models on this PC, or point TOMS at a
  ComfyUI server that already runs on another machine. Double-click tools\setup_ai_art.cmd, or let
  setup_new_pc.bat call it.

  Local : reuses AIGamestyle\_pipeline\tools\setup_style_pipeline.bat (clones ComfyUI into
          AIGamestyle\_comfyui\ComfyUI, creates its venv, adds ComfyUI-Manager), then makes PyTorch use
          the GPU, adds the custom nodes the sprite workflow needs, and downloads the chosen models.
  Remote: asks for the server URL, checks that it answers and which of the models it has.
  Both  : save the server in the user environment variables COMFYUI_URL (full URL) and
          COMFYUI_SERVER (host:port, the variable AIGamestyle's Python scripts read).

  Nothing here needs administrator rights.

.PARAMETER FromSetup
  Called by setup_new_pc.bat: the first dialog then also offers "skip".
#>
param([switch]$FromSetup)

$ErrorActionPreference = 'Continue'
$root     = Split-Path -Parent $PSScriptRoot                      # the repository root
$aigs     = Join-Path $root 'AIGamestyle'
$comfyDir = Join-Path $aigs '_comfyui\ComfyUI'
$venvPy   = Join-Path $comfyDir '.venv\Scripts\python.exe'
$LogFile  = Join-Path $env:TEMP 'TOMS_setup_ai_art.log'
$LocalUrl = 'http://127.0.0.1:8188'

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName Microsoft.VisualBasic
[System.Windows.Forms.Application]::EnableVisualStyles()

# Models the TOMS sprite workflow uses (docs: SDXL + pixel-art LoRA + ControlNet Union + IP-Adapter).
# File names match the ones AIGamestyle's workflows already reference, so both projects share one models folder.
$Models = @(
    @{ Name = 'ControlNet Union SDXL ProMax (keeps each sprite''s shape)'; Folder = 'controlnet';  File = 'controlnet-union-sdxl-promax.safetensors'; Size = 2513342408
       Url = 'https://huggingface.co/xinsir/controlnet-union-sdxl-1.0/resolve/main/diffusion_pytorch_model_promax.safetensors' },
    @{ Name = 'Pixel Art XL LoRA (pixel-art style)';                       Folder = 'loras';       File = 'pixel-art-xl.safetensors';                 Size = 170543052
       Url = 'https://huggingface.co/nerijs/pixel-art-xl/resolve/main/pixel-art-xl.safetensors' },
    @{ Name = 'SDXL 1.0 base (main model)';                                Folder = 'checkpoints'; File = 'sd_xl_base_1.0.safetensors';               Size = 6938078334
       Url = 'https://huggingface.co/stabilityai/stable-diffusion-xl-base-1.0/resolve/main/sd_xl_base_1.0.safetensors' },
    @{ Name = 'SDXL VAE fp16 fix (avoids black images)';                   Folder = 'vae';         File = 'sdxl.vae.safetensors';                     Size = 334641162
       Url = 'https://huggingface.co/madebyollin/sdxl-vae-fp16-fix/resolve/main/sdxl.vae.safetensors' },
    @{ Name = 'IP-Adapter Plus SDXL (copies a reference style)';           Folder = 'ipadapter';   File = 'ip-adapter-plus_sdxl_vit-h.safetensors';   Size = 847517512
       Url = 'https://huggingface.co/h94/IP-Adapter/resolve/main/sdxl_models/ip-adapter-plus_sdxl_vit-h.safetensors' },
    @{ Name = 'CLIP Vision ViT-H (needed by IP-Adapter)';                  Folder = 'clip_vision'; File = 'CLIP-ViT-H-14-laion2B-s32B-b79K.safetensors'; Size = 2528373448
       Url = 'https://huggingface.co/h94/IP-Adapter/resolve/main/models/image_encoder/model.safetensors' }
)
$CustomNodes = @(
    @{ Dir = 'comfyui_ipadapter_plus';   Url = 'https://github.com/cubiq/ComfyUI_IPAdapter_plus' },
    @{ Dir = 'comfyui_controlnet_aux';   Url = 'https://github.com/Fannovel16/comfyui_controlnet_aux' },
    @{ Dir = 'comfyui-inspyrenet-rembg'; Url = 'https://github.com/john-mnz/ComfyUI-Inspyrenet-Rembg' }
)

function Log([string]$msg, [string]$color = 'Gray') {
    Write-Host "[toms-ai-art] $msg" -ForegroundColor $color
    Add-Content -Path $LogFile -Value ("{0:HH:mm:ss} {1}" -f (Get-Date), $msg) -Encoding UTF8
}

function New-Owner {
    $o = New-Object System.Windows.Forms.Form -Property @{ TopMost = $true; ShowInTaskbar = $false; Opacity = 0 }
    $o.Show(); $o.Activate(); return $o
}

function Show-Box([string]$text, [string]$title = 'TOMS AI art setup', [string]$buttons = 'OK', [string]$icon = 'Information') {
    $owner = New-Owner
    $r = [System.Windows.Forms.MessageBox]::Show($owner, $text, $title,
        [System.Windows.Forms.MessageBoxButtons]::$buttons, [System.Windows.Forms.MessageBoxIcon]::$icon)
    $owner.Close()
    return $r.ToString()
}

function Format-GB([double]$bytes) { '{0:N1} GB' -f ($bytes / 1GB) }

function Update-PathFromRegistry {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}

function Get-Gpu {
    $smi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
    if (-not $smi) { return $null }
    $line = & $smi.Source --query-gpu=name,memory.total --format=csv,noheader,nounits 2>$null | Select-Object -First 1
    if ($line -match '^(.+),\s*(\d+)') { return @{ Name = $Matches[1].Trim(); VramMB = [int]$Matches[2] } }
    return $null
}

function Save-ServerSetting([string]$url) {
    [Environment]::SetEnvironmentVariable('COMFYUI_URL', $url, 'User')
    $env:COMFYUI_URL = $url
    $uri = [Uri]$url
    if ($uri.Scheme -eq 'http') {
        $hostPort = '{0}:{1}' -f $uri.Host, $uri.Port
        [Environment]::SetEnvironmentVariable('COMFYUI_SERVER', $hostPort, 'User')
        $env:COMFYUI_SERVER = $hostPort
        Log "saved COMFYUI_URL=$url and COMFYUI_SERVER=$hostPort (user environment variables)" 'Green'
    } else {
        Log "saved COMFYUI_URL=$url (https: AIGamestyle's scripts only speak http, so COMFYUI_SERVER is left unchanged)" 'Yellow'
    }
}

# Returns the parsed /system_stats of a ComfyUI server, or $null.
function Test-Server([string]$url, [int]$timeoutSec = 8) {
    try { return Invoke-RestMethod -Uri "$url/system_stats" -TimeoutSec $timeoutSec -ErrorAction Stop } catch { return $null }
}

function Describe-Server($stats) {
    $d = @($stats.devices)[0]
    if (-not $d) { return 'no device info' }
    $vram = if ($d.vram_total) { ' (' + (Format-GB $d.vram_total) + ' VRAM)' } else { '' }
    return "$($d.name)$vram, ComfyUI $($stats.system.comfyui_version)"
}

Set-Content -Path $LogFile -Value "TOMS AI art setup log $(Get-Date)" -Encoding UTF8

# ================================================================== choose: local / remote / skip
$gpu = Get-Gpu
$gpuText = if ($gpu) { "This PC: $($gpu.Name), $([math]::Round($gpu.VramMB / 1024)) GB VRAM" } else { 'This PC: no NVIDIA GPU found' }
$localOk = $gpu -and $gpu.VramMB -ge 8000
Log $gpuText

$form = New-Object System.Windows.Forms.Form -Property @{
    Text = 'TOMS - AI art tools (ComfyUI)'; Width = 600; Height = 400; TopMost = $true
    StartPosition = 'CenterScreen'; FormBorderStyle = 'FixedDialog'; MaximizeBox = $false; MinimizeBox = $false
}
$font = New-Object System.Drawing.Font('Segoe UI', 9.5)
$form.Font = $font
$intro = New-Object System.Windows.Forms.Label -Property @{
    Left = 15; Top = 12; Width = 560; Height = 58
    Text = "TOMS can regenerate its sprites with ComfyUI (free, open source). Where should the images be generated?`n$gpuText"
}
$rbLocal = New-Object System.Windows.Forms.RadioButton -Property @{
    Left = 20; Top = 75; Width = 550; Height = 70
    Text = "On THIS PC - install ComfyUI and download models (about 13 GB of models,`n" +
           "plus 3-5 GB for ComfyUI and PyTorch). Free; needs an NVIDIA GPU with 8+ GB VRAM." +
           $(if ($localOk) { "`n(recommended for this PC)" } else { "`n(not recommended: this PC's GPU is too small or missing)" })
}
$rbRemote = New-Object System.Windows.Forms.RadioButton -Property @{
    Left = 20; Top = 150; Width = 550; Height = 70
    Text = "On ANOTHER machine - use a ComfyUI server that already runs on your network`n" +
           "(or a rented cloud GPU). Nothing big is downloaded here; you enter its address."
}
$rbSkip = New-Object System.Windows.Forms.RadioButton -Property @{
    Left = 20; Top = 225; Width = 550; Height = 45
    Text = "Skip for now (run tools\setup_ai_art.cmd later). The game does not need this."
}
if ($localOk) { $rbLocal.Checked = $true } elseif ($FromSetup) { $rbSkip.Checked = $true } else { $rbRemote.Checked = $true }
$ok = New-Object System.Windows.Forms.Button -Property @{ Text = 'Continue'; Left = 380; Top = 300; Width = 90; DialogResult = 'OK' }
$cancel = New-Object System.Windows.Forms.Button -Property @{ Text = 'Cancel'; Left = 480; Top = 300; Width = 90; DialogResult = 'Cancel' }
$form.Controls.AddRange(@($intro, $rbLocal, $rbRemote, $rbSkip, $ok, $cancel))
$form.AcceptButton = $ok; $form.CancelButton = $cancel
if ($form.ShowDialog() -ne 'OK' -or $rbSkip.Checked) { Log 'skipped'; exit 0 }
$mode = if ($rbLocal.Checked) { 'local' } else { 'remote' }
Log "mode: $mode"

# ================================================================== remote
if ($mode -eq 'remote') {
    $current = [Environment]::GetEnvironmentVariable('COMFYUI_URL', 'User')
    while ($true) {
        $url = [Microsoft.VisualBasic.Interaction]::InputBox(
            "Address of the ComfyUI server, for example:`n" +
            "   http://192.168.1.50:8188        (a PC on your network)`n" +
            "   https://xxxx-8188.proxy.runpod.net   (a rented cloud GPU)`n`n" +
            "On that machine ComfyUI must be started with  --listen 0.0.0.0`n" +
            "and Windows Firewall must allow port 8188.",
            'TOMS - ComfyUI server address', $(if ($current) { $current } else { 'http://' }))
        if (-not $url -or $url -eq 'http://') { Log 'no server entered'; exit 0 }
        $url = $url.Trim().TrimEnd('/')
        if ($url -notmatch '^https?://') { $url = "http://$url" }
        if ($url -notmatch '^https?://[^/:]+(:\d+)?$') {
            Show-Box "'$url' is not an address like http://192.168.1.50:8188" 'TOMS - ComfyUI server' 'OK' 'Warning' | Out-Null
            continue
        }
        Log "testing $url/system_stats"
        $stats = Test-Server $url
        if ($stats) { break }
        $again = Show-Box ("No ComfyUI answered at`n   $url`n`nCheck that:`n" +
                           "  * ComfyUI is running on that machine with  --listen 0.0.0.0`n" +
                           "  * the port is right (default 8188) and the firewall allows it`n" +
                           "  * both machines are on the same network / VPN`n`nTry another address?") 'TOMS - ComfyUI server' 'RetryCancel' 'Warning'
        if ($again -ne 'Retry') { exit 1 }
    }
    $desc = Describe-Server $stats
    Log "server OK: $desc" 'Green'

    # Which of the workflow's models does the server have? (GET /models/<folder> lists file names.)
    $missing = @()
    foreach ($m in $Models) {
        try { $have = @(Invoke-RestMethod -Uri "$url/models/$($m.Folder)" -TimeoutSec 8 -ErrorAction Stop) } catch { $have = $null }
        if ($null -eq $have) { $missing += "  ? $($m.File)  (could not list $($m.Folder))"; continue }
        if (-not ($have | Where-Object { (Split-Path $_ -Leaf) -eq $m.File })) { $missing += "  - $($m.Folder)\$($m.File)" }
    }
    Save-ServerSetting $url
    $msg = "Connected to the ComfyUI server:`n   $url`n   $desc`n`nSaved for TOMS and AIGamestyle (environment variables COMFYUI_URL / COMFYUI_SERVER).`n"
    if ($missing) {
        $msg += "`nThe server is MISSING these models used by the TOMS sprite workflow:`n" + ($missing -join "`n") +
                "`n`nOn the server machine, run tools\setup_ai_art.cmd and choose 'On THIS PC',`nor download them with ComfyUI-Manager > Model Manager."
    } else { $msg += "`nThe server has every model the TOMS sprite workflow needs." }
    $msg += "`n`nNote: ComfyUI has no password. Only use servers on your own network / VPN,`nand stop rented cloud GPUs when you are done (they bill per hour)."
    Show-Box $msg 'TOMS - ComfyUI server' 'OK' $(if ($missing) { 'Warning' } else { 'Information' }) | Out-Null
    exit 0
}

# ================================================================== local
$git = (Get-Command git -ErrorAction SilentlyContinue).Source
if (-not $git) { Show-Box 'Git is required. Run setup_new_pc.bat first.' 'TOMS AI art setup' 'OK' 'Error' | Out-Null; exit 1 }

# ---- 1. AIGamestyle submodule (holds the ComfyUI installer and the pipeline scripts)
$setupBat = Join-Path $aigs '_pipeline\tools\setup_style_pipeline.bat'
if (-not (Test-Path $setupBat)) {
    Log 'fetching the AIGamestyle submodule'
    & $git -C $root submodule update --init AIGamestyle
    if (-not (Test-Path $setupBat)) { Show-Box "Could not fetch the AIGamestyle submodule (see the console)." 'TOMS AI art setup' 'OK' 'Error' | Out-Null; exit 1 }
}

# ---- 2. Python 3.10+ (the WindowsApps 'python' stub only opens the Store, so really run it)
function Test-Python([string]$exe) {
    if (-not $exe) { return $false }
    & $exe -c "import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)" 2>$null
    return ($LASTEXITCODE -eq 0)
}
function Find-Python {
    $c = Get-Command python -ErrorAction SilentlyContinue
    if ($c -and (Test-Python $c.Source)) { return $c.Source }
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        $exe = & $py.Source -3 -c "import sys; print(sys.executable)" 2>$null
        if ($exe -and (Test-Python $exe)) { return $exe }
    }
    $found = Get-ChildItem (Join-Path $env:LOCALAPPDATA 'Programs\Python') -Filter python.exe -Recurse -Depth 1 -ErrorAction SilentlyContinue |
             Sort-Object FullName -Descending | Select-Object -First 1
    if ($found -and (Test-Python $found.FullName)) { return $found.FullName }
    return $null
}
$python = $null
if (-not (Test-Path $venvPy)) {                    # an existing ComfyUI venv already has its own Python
    $python = Find-Python
    if (-not $python) {
        Show-Box ("Python 3 is needed for ComfyUI and is not installed.`n`n" +
                  "It will now be installed for your user only (Python 3.12 via winget).`n" +
                  "This does not need administrator permission; if Windows asks anyway, click 'Yes'.") 'TOMS AI art setup - Python' | Out-Null
        winget install -e --id Python.Python.3.12 --scope user --source winget --accept-source-agreements --accept-package-agreements --disable-interactivity
        Update-PathFromRegistry
        $python = Find-Python
        if (-not $python) { Show-Box 'Python installation failed (see the console).' 'TOMS AI art setup' 'OK' 'Error' | Out-Null; exit 1 }
    }
    Log "python: $python"
    # setup_style_pipeline.bat calls plain 'python': put this one first on PATH for it.
    $pyDir = Split-Path -Parent $python
    $env:Path = "$pyDir;$pyDir\Scripts;$env:Path"
}

# ---- 3. ComfyUI itself (AIGamestyle's installer: clone, venv, requirements, ComfyUI-Manager)
# Only when missing: the installer also runs 'git pull' on ComfyUI, which must not happen to a working install unasked.
if (-not ((Test-Path (Join-Path $comfyDir 'main.py')) -and (Test-Path $venvPy))) {
    Log 'running AIGamestyle\_pipeline\tools\setup_style_pipeline.bat'
    & $setupBat
} else { Log "ComfyUI already installed: $comfyDir" }
if (-not ((Test-Path (Join-Path $comfyDir 'main.py')) -and (Test-Path $venvPy))) {
    Show-Box "Installing ComfyUI failed (see the console and $LogFile)." 'TOMS AI art setup' 'OK' 'Error' | Out-Null
    exit 1
}

# ---- 4. PyTorch with CUDA (ComfyUI's requirements install the CPU-only build from PyPI)
if ($gpu) {
    & $venvPy -c "import torch, sys; sys.exit(0 if torch.cuda.is_available() else 1)" 2>$null
    if ($LASTEXITCODE -ne 0) {
        Log 'installing CUDA PyTorch (about 3 GB)'
        & $venvPy -m pip uninstall -y torch torchvision torchaudio
        # cu128 wheels support RTX 20xx up to RTX 50xx.
        & $venvPy -m pip install --upgrade torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu128
        & $venvPy -c "import torch, sys; sys.exit(0 if torch.cuda.is_available() else 1)" 2>$null
        if ($LASTEXITCODE -ne 0) { Log 'PyTorch still cannot see the GPU: update the NVIDIA driver, then run this again' 'Red' }
        else { Log 'PyTorch uses the GPU' 'Green' }
    } else { Log 'PyTorch already uses the GPU' 'Green' }
}

# ---- 5. custom nodes used by the sprite workflow
foreach ($n in $CustomNodes) {
    $dir = Join-Path $comfyDir "custom_nodes\$($n.Dir)"
    if (-not (Test-Path $dir)) {
        Log "adding custom node $($n.Dir)"
        & $git clone --depth 1 $n.Url $dir
        $req = Join-Path $dir 'requirements.txt'
        if (Test-Path $req) { & $venvPy -m pip install -r $req }
    } else { Log "custom node $($n.Dir) already installed" }
}

# ---- 6+7. models folder (selectable: every PC keeps its models somewhere else) and which models to download
# ComfyUI finds the folder through extra_model_paths.yaml, so the models can live outside the repo and be
# shared with other ComfyUI installs. The box starts at the folder that file already names, if any.
$yaml = Join-Path $comfyDir 'extra_model_paths.yaml'
$comfyModels = Join-Path $comfyDir 'models'
$yamlDir = $null
if (Test-Path $yaml) {
    $m = Select-String -Path $yaml -Pattern '^\s*base_path:\s*(\S.*)$' | Select-Object -First 1
    if ($m) { $yamlDir = $m.Matches[0].Groups[1].Value.Trim().Trim('"').Replace('/', '\').TrimEnd('\') }
}
$startDir = if ($yamlDir) { $yamlDir } else { (Split-Path -Qualifier $root) + '\AI\models' }

$form = New-Object System.Windows.Forms.Form -Property @{
    Text = 'TOMS - AI models'; Width = 680; Height = 470; TopMost = $true; Font = $font
    StartPosition = 'CenterScreen'; FormBorderStyle = 'FixedDialog'; MaximizeBox = $false; MinimizeBox = $false
}
$lblDir = New-Object System.Windows.Forms.Label -Property @{
    Left = 15; Top = 10; Width = 640; Height = 36
    Text = "Models folder - where this PC keeps (or should keep) its AI models. Point it at an existing folder`nto reuse models you already have; models already there are detected and not downloaded again."
}
$txtDir = New-Object System.Windows.Forms.TextBox -Property @{ Left = 15; Top = 50; Width = 530; Text = $startDir }
$btnBrowse = New-Object System.Windows.Forms.Button -Property @{ Text = 'Browse...'; Left = 555; Top = 48; Width = 95 }
$lblFree = New-Object System.Windows.Forms.Label -Property @{ Left = 15; Top = 80; Width = 640; Height = 36 }
$lblInfo = New-Object System.Windows.Forms.Label -Property @{
    Left = 15; Top = 120; Width = 640; Height = 20
    Text = 'Models (free downloads from huggingface.co; read each model card before shipping art made with them):'
}
$list = New-Object System.Windows.Forms.CheckedListBox -Property @{ Left = 15; Top = 142; Width = 635; Height = 200; CheckOnClick = $true }
$total = New-Object System.Windows.Forms.Label -Property @{ Left = 15; Top = 350; Width = 420; Height = 25 }
foreach ($m in $Models) { [void]$list.Items.Add($m.Name) }

# ItemCheck fires before the box changes, so the changing item's new state is passed in.
$updateTotal = {
    param([int]$changed = -1, [bool]$newState = $false)
    $sum = 0
    for ($i = 0; $i -lt $Models.Count; $i++) {
        $on = if ($i -eq $changed) { $newState } else { $list.GetItemChecked($i) }
        if ($on -and -not $Models[$i].Done) { $sum += $Models[$i].Size }
    }
    $total.Text = "To download: $(Format-GB $sum)"
}
# Re-reads the chosen folder: free space, and which models are already in it (same name and size).
$refreshDir = {
    $dir = $txtDir.Text.Trim().TrimEnd('\')
    $valid = $dir -and [IO.Path]::IsPathRooted($dir) -and (Test-Path (Split-Path -Qualifier $dir -ErrorAction SilentlyContinue))
    $ok.Enabled = [bool]$valid
    if (-not $valid) { $lblFree.Text = 'Enter a full path such as D:\AI\models'; return }
    $free = (Get-PSDrive (Split-Path -Qualifier $dir).TrimEnd(':')).Free
    $note = if (-not (Test-Path $dir)) { 'new folder, created when needed' }
            elseif ($dir -eq $yamlDir) { 'the folder ComfyUI uses now' }
            elseif ($dir -eq $comfyModels) { "ComfyUI's own models folder" }
            else { 'ComfyUI will be switched to this folder' }
    $lblFree.Text = "Free on $(Split-Path -Qualifier $dir) $(Format-GB $free)   ($note)"
    for ($i = 0; $i -lt $Models.Count; $i++) {
        $m = $Models[$i]
        $m.Path = Join-Path $dir "$($m.Folder)\$($m.File)"
        $m.Done = (Test-Path $m.Path) -and ((Get-Item $m.Path).Length -eq $m.Size)
        $list.Items[$i] = '{0}  -  {1}{2}' -f $m.Name, (Format-GB $m.Size), $(if ($m.Done) { '   [already in this folder]' } else { '' })
        $list.SetItemChecked($i, -not $m.Done)
    }
    & $updateTotal
}
$btnBrowse.Add_Click({
    $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
    $dlg.Description = 'Choose the AI models folder (it gets subfolders such as checkpoints, loras, controlnet).'
    $dlg.ShowNewFolderButton = $true
    $cur = $txtDir.Text.Trim()
    $dlg.SelectedPath = if ($cur -and (Test-Path $cur)) { $cur } else { (Split-Path -Qualifier $root) + '\' }
    if ($dlg.ShowDialog($form) -eq 'OK') { $txtDir.Text = $dlg.SelectedPath }   # TextChanged refreshes
})
$list.Add_ItemCheck({ & $updateTotal $_.Index ($_.NewValue -eq 'Checked') })
$txtDir.Add_TextChanged({ & $refreshDir })
$ok = New-Object System.Windows.Forms.Button -Property @{ Text = 'Download'; Left = 445; Top = 385; Width = 100; DialogResult = 'OK' }
$cancel = New-Object System.Windows.Forms.Button -Property @{ Text = 'Skip download'; Left = 550; Top = 385; Width = 100; DialogResult = 'Cancel' }
$form.Controls.AddRange(@($lblDir, $txtDir, $btnBrowse, $lblFree, $lblInfo, $list, $total, $ok, $cancel))
$form.AcceptButton = $ok; $form.CancelButton = $cancel
& $refreshDir
$answer = $form.ShowDialog()

# Point ComfyUI at the chosen folder (both buttons: the folder choice counts even without downloading).
$modelsDir = $txtDir.Text.Trim().TrimEnd('\')
if (-not ($modelsDir -and [IO.Path]::IsPathRooted($modelsDir))) { $modelsDir = if ($yamlDir) { $yamlDir } else { $comfyModels } }
if ($modelsDir -ne $yamlDir -and $modelsDir -ne $comfyModels) {
    $folders = 'checkpoints', 'clip', 'clip_vision', 'configs', 'controlnet', 'diffusion_models', 'embeddings', 'ipadapter',
               'loras', 'text_encoders', 'unet', 'upscale_models', 'vae'
    $folders | ForEach-Object { New-Item -ItemType Directory -Force (Join-Path $modelsDir $_) | Out-Null }
    $lines = @('# written by TOMS tools\setup_ai_art.ps1 (previous version: extra_model_paths.yaml.bak)', 'toms_models:',
               "  base_path: $($modelsDir.Replace('\', '/'))", '')
    $lines += $folders | ForEach-Object { "  ${_}: $_" }
    if (Test-Path $yaml) { Copy-Item $yaml "$yaml.bak" -Force }
    Set-Content -Path $yaml -Value $lines -Encoding UTF8
    Log "ComfyUI now loads models from $modelsDir (wrote $yaml)" 'Green'
}
Log "models folder: $modelsDir"
$failed = @()
if ($answer -eq 'OK') {
    $curl = Join-Path $env:WINDIR 'System32\curl.exe'           # built into Windows 10/11; resumes with -C -
    for ($i = 0; $i -lt $Models.Count; $i++) {
        $m = $Models[$i]
        if (-not $list.GetItemChecked($i) -or $m.Done) { continue }
        New-Item -ItemType Directory -Force (Split-Path -Parent $m.Path) | Out-Null
        $part = "$($m.Path).part"
        Log "downloading $($m.File) ($(Format-GB $m.Size)) - you can stop and rerun; it resumes"
        & $curl -L --fail --retry 3 -C - -o $part $m.Url
        if ((Test-Path $part) -and (Get-Item $part).Length -eq $m.Size) {
            Move-Item $part $m.Path -Force
            Log "  OK $($m.Path)" 'Green'
        } else {
            $failed += $m.File
            Log "  FAILED $($m.File) (partial file kept; run this again to resume)" 'Red'
        }
    }
} else { Log 'model download skipped' }

# ---- 8. save the server setting, optionally start ComfyUI and check it answers
Save-ServerSetting $LocalUrl
$startBat = Join-Path $aigs '_pipeline\tools\start_comfyui.bat'
$summary = "ComfyUI is installed in:`n   $comfyDir`nModels folder:`n   $modelsDir`n"
if ($failed) { $summary += "`nThese downloads FAILED (run tools\setup_ai_art.cmd again to resume):`n   " + ($failed -join "`n   ") + "`n" }
$summary += "`nStart ComfyUI any time with:`n   AIGamestyle\_pipeline\tools\start_comfyui.bat`n   (then open $LocalUrl)`n`nStart it now and check that it works?"
if ((Show-Box $summary 'TOMS AI art setup - done' 'YesNo' 'Question') -eq 'Yes') {
    Start-Process -FilePath $startBat -WorkingDirectory (Split-Path $startBat)
    Log 'waiting for ComfyUI to answer (first start can take a few minutes)'
    $stats = $null
    for ($t = 0; $t -lt 60 -and -not $stats; $t++) { Start-Sleep -Seconds 5; $stats = Test-Server $LocalUrl 3 }
    if ($stats) {
        Log "ComfyUI answers: $(Describe-Server $stats)" 'Green'
        Show-Box "ComfyUI is running:`n   $(Describe-Server $stats)`n`nIt opens in the browser at $LocalUrl" 'TOMS AI art setup' | Out-Null
        Start-Process $LocalUrl
    } else {
        Show-Box "ComfyUI did not answer within 5 minutes. Look at its console window for errors." 'TOMS AI art setup' 'OK' 'Warning' | Out-Null
    }
}
exit $(if ($failed) { 1 } else { 0 })
