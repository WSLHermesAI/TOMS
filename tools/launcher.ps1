# launcher.ps1 -- one window for every build, deploy, test and setup action. Start it with TOMS_Launcher.bat.
#
#   Left: the actions by group. Right: what the selected one does, where its result goes, the command it runs.
#   Run (or a double-click) starts the action in its own console window (tools\launcher_run.cmd); the list at the
#   bottom shows each run and whether it finished OK. Ticked actions run as a chain: one after another in the order
#   of the list (builds of one folder must not overlap), and a failure skips the rest. The actions are tools\*.cmd and setup_new_pc.bat: this window
#   only starts them, so they keep working on their own too.
#   Language: English or Traditional Chinese (the box at the top right), remembered in %LOCALAPPDATA%\TOMS\launcher.json;
#   the first time it follows the Windows display language.
#
# This file holds Chinese text: keep it UTF-8 *with* a BOM (Windows PowerShell 5.1 reads a file without one as ANSI).
param([string]$SelfTest = '', [ValidateSet('', 'en', 'zh')][string]$Lang = '')
# -SelfTest <folder>: opens the window, selects an action, writes <folder>\launcher_<lang>.png and closes (an
# automated look at the window; nothing runs). -Lang en|zh: start in that language.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
# The console is hidden (TOMS_Launcher.bat): anything that goes wrong is said in a message box.
trap {
    [void][Windows.Forms.MessageBox]::Show("The TOMS launcher stopped:`n$($_.Exception.Message)", 'TOMS launcher', 'OK', 'Error')
    exit 1
}

# Every text is a pair: @(English, Traditional Chinese).
# An action: N name, Cmd the command line (from the repo root), T what it does, Result where the result goes, and:
#   Pause = $false   the command pauses on its own (the console would ask twice)
#   C                a question asked before it starts (pushes, long runs)
#   Window = $true   the command opens its own window (its console starts minimized)
$groups = @(
    @{ N = @('Windows', 'Windows 桌面版'); Actions = @(
        @{ N = @('Build the Windows game (release)', '建置 Windows 遊戲（正式版）'); Cmd = 'tools\release_windows.cmd nopause'
           T = @('Builds the shipping Windows game (Release, no editor, no Qt, no console window) and packages it with its assets and the MSVC runtime DLLs. Copy the folder anywhere and double-click toms_game.exe.',
                 '建置出貨用的 Windows 遊戲（Release、不含編輯器、不含 Qt、沒有主控台視窗），並和素材、MSVC runtime DLL 一起打包。整個資料夾可以複製到任何地方，雙擊 toms_game.exe 即可遊玩。')
           Result = 'Build\dist\TOMS-windows' }
        @{ N = @('Build all editors', '建置所有編輯器'); Cmd = 'tools\release_editors.cmd nopause'
           T = @('Builds windows-release, then copies every editor (toms, anim, atlas, particle, font, glTF, stage/event, UI, battle), atlaspack, fontpack, the Qt DLLs and the C++ runtime into Editor\bin, and rewrites Editor\BUILD_INFO.md. Start the editors with the launchers in Editor\.',
                 '建置 windows-release，再把所有編輯器（toms、動畫、圖集、粒子、字型、glTF、關卡/事件、UI、戰鬥）、atlaspack、fontpack、Qt DLL 和 C++ runtime 複製到 Editor\bin，並重寫 Editor\BUILD_INFO.md。用 Editor\ 裡的啟動檔開啟編輯器。')
           Result = 'Editor\bin' }
        @{ N = @('Copy the last editor build (no compile)', '複製上次建置的編輯器（不編譯）'); Cmd = 'tools\release_editors.cmd nobuild nopause'
           T = @('Copies the editors of the last windows-release build into Editor\bin without compiling. Use it after you built in Visual Studio.',
                 '不編譯，直接把上次 windows-release 建置的編輯器複製到 Editor\bin。在 Visual Studio 建置完之後使用。')
           Result = 'Editor\bin' }
        @{ N = @('Check the editors in Editor\bin', '檢查 Editor\bin 的編輯器'); Cmd = 'python tools\package_editors.py --check'
           T = @('Only lists what Editor\bin lacks, has extra or has out of date compared with the last build. Changes nothing.',
                 '只列出 Editor\bin 和上次建置相比缺少、多出或過期的檔案。不會修改任何東西。')
           Result = 'Editor\bin' }
        @{ N = @('Development build (windows-release)', '開發用建置（windows-release）'); Cmd = 'tools\build.cmd windows-release'
           T = @('Builds the development preset: the game with its console, every editor and the tests. The same build Visual Studio makes; nothing is copied anywhere.',
                 '建置開發用的 preset：帶主控台的遊戲、所有編輯器和測試。和 Visual Studio 建置的一樣；不會複製到任何地方。')
           Result = 'Build\windows-release\bin' }
    ) }
    @{ N = @('Web', '網頁版'); Actions = @(
        @{ N = @('Build the web game', '建置網頁版遊戲'); Cmd = 'tools\release_web.cmd nopause'
           T = @('Builds the web game (single- and multithreaded) and packages it for a web server. Needs emsdk (D:\Work\emsdk).',
                 '建置網頁版遊戲（單執行緒和多執行緒），並打包成可放上網頁伺服器的檔案。需要 emsdk（D:\Work\emsdk）。')
           Result = 'Build\dist\TOMS-web' }
        @{ N = @('Try the web game locally', '在本機試玩網頁版'); Cmd = 'tools\serve_web.cmd dist'
           T = @('Serves the package of "Build the web game" on a local web server and opens the browser. Press Ctrl+C in its console to stop the server.',
                 '用本機網頁伺服器提供「建置網頁版遊戲」的打包結果，並開啟瀏覽器。在它的主控台按 Ctrl+C 停止伺服器。')
           Result = 'Build\dist\TOMS-web' }
        @{ N = @('Publish the web game: dry run', '發佈網頁版：試跑'); Cmd = 'tools\publish_web.cmd dryrun'; Pause = $false
           T = @('Builds the web game and prepares GitHub Pages, then shows what would change. Pushes nothing.',
                 '建置網頁版遊戲並準備 GitHub Pages，然後顯示會改變什麼。不會 push 任何東西。')
           Result = 'Build\dist\TOMS-web' }
        @{ N = @('Publish the web game to GitHub Pages', '發佈網頁版到 GitHub Pages'); Cmd = 'tools\publish_web.cmd'; Pause = $false
           C = @("Build the web game and publish it to https://wslhermesai.github.io/TOMS/ ?`n`nIt still asks once more in its console before pushing.",
                 "建置網頁版遊戲並發佈到 https://wslhermesai.github.io/TOMS/ ？`n`npush 之前它還會在主控台再問一次。")
           T = @('Builds the web game and publishes it to the gh-pages branch (https://wslhermesai.github.io/TOMS/). Asks in its console before pushing. Details: docs\project\07_PUBLISH_GITHUB_PAGES.md.',
                 '建置網頁版遊戲並發佈到 gh-pages 分支（https://wslhermesai.github.io/TOMS/）。push 之前會在主控台詢問。詳見 docs\project\zh_TW\07_PUBLISH_GITHUB_PAGES.md。')
           Result = 'Build\dist\TOMS-web' }
        @{ N = @('Publish the existing web build (no rebuild)', '發佈現有的網頁版（不重新建置）'); Cmd = 'tools\publish_web.cmd nobuild'; Pause = $false
           C = @('Publish the existing Build\dist\TOMS-web to GitHub Pages without rebuilding?',
                 '不重新建置，直接把現有的 Build\dist\TOMS-web 發佈到 GitHub Pages？')
           T = @('Publishes the package already in Build\dist\TOMS-web to GitHub Pages, without building. Asks in its console before pushing.',
                 '不建置，直接把 Build\dist\TOMS-web 裡現有的打包發佈到 GitHub Pages。push 之前會在主控台詢問。')
           Result = 'Build\dist\TOMS-web' }
        @{ N = @('Build the glTF viewer for the browser', '建置網頁版 glTF 檢視器'); Cmd = 'tools\build_gltf_web.cmd release'
           T = @('Builds gltf_viewer.html (WebGL2) with the test models. Then use "Try the glTF viewer in the browser".',
                 '建置 gltf_viewer.html（WebGL2）和測試模型。之後用「在瀏覽器試用 glTF 檢視器」開啟。')
           Result = 'Build\web-release-windows\bin' }
        @{ N = @('Try the glTF viewer in the browser', '在瀏覽器試用 glTF 檢視器'); Cmd = 'tools\serve_web.cmd gltf'
           T = @('Serves the web glTF viewer and opens the browser. Ctrl+C in its console stops the server.',
                 '提供網頁版 glTF 檢視器並開啟瀏覽器。在它的主控台按 Ctrl+C 停止伺服器。')
           Result = 'Build\web-release-windows\bin' }
        @{ N = @('Build the web atlas tool', '建置網頁版圖集工具'); Cmd = 'tools\build_atlas_web.cmd release'
           T = @('Builds the browser version of the atlas tool (index.html, atlas_web.js/.wasm, atlaspack.mjs).',
                 '建置瀏覽器版的圖集工具（index.html、atlas_web.js/.wasm、atlaspack.mjs）。')
           Result = 'Build\atlas-web\site' }
    ) }
    @{ N = @('Android', 'Android'); Actions = @(
        @{ N = @('Build the APK for the emulator', '建置模擬器用的 APK'); Cmd = 'tools\build_android.cmd x86_64-debug'
           T = @('Builds the Android APK for the Android Studio emulator (x86_64, debug). Details: docs\project\06_BUILD_ANDROID.md.',
                 '建置給 Android Studio 模擬器用的 APK（x86_64、debug）。詳見 docs\project\06_BUILD_ANDROID.md。')
           Result = 'android\app\build\outputs\apk' }
        @{ N = @('Build the APK for a phone (release)', '建置手機用的 APK（正式版）'); Cmd = 'tools\build_android.cmd release'
           T = @('Builds the release APK for arm64 phones.', '建置 arm64 手機用的正式版 APK。')
           Result = 'android\app\build\outputs\apk' }
        @{ N = @('Install and run on the emulator / phone', '安裝並在模擬器/手機上執行'); Cmd = 'tools\run_android.cmd'
           T = @('Installs the emulator APK on the running emulator or the connected phone, starts it and shows the game log. Start the emulator first (Android Studio, Device Manager). Ctrl+C stops the log.',
                 '把模擬器用的 APK 安裝到執行中的模擬器或連接的手機，啟動遊戲並顯示遊戲記錄。請先啟動模擬器（Android Studio 的 Device Manager）。按 Ctrl+C 停止記錄。')
           Result = 'android\app\build\outputs\apk' }
    ) }
    @{ N = @('Tests', '測試'); Actions = @(
        @{ N = @('Test what changed', '測試有改動的部分'); Cmd = 'tools\test.cmd'
           T = @('Runs only the tests the uncommitted changes can break (tools\test_select.py). Build first (Development build).',
                 '只執行尚未 commit 的改動可能影響到的測試（tools\test_select.py）。請先建置（開發用建置）。')
           Result = 'Build\windows-release' }
        @{ N = @('Which tests would run (list only)', '會跑哪些測試（只列出）'); Cmd = 'tools\test.cmd --list'
           T = @('Prints which tests the uncommitted changes select, and why. Runs nothing.',
                 '列出尚未 commit 的改動會選到哪些測試，以及原因。不執行任何測試。')
           Result = '' }
        @{ N = @('Test all (desktop and web)', '全部測試（桌面版和網頁版）'); Cmd = 'tools\test.cmd all'
           C = @('Run every test (desktop and web)? It takes a long time.', '執行所有測試（桌面版和網頁版）？會花很長的時間。')
           T = @('Runs every test of windows-release, web included. Do it before a release or a publish.',
                 '執行 windows-release 的所有測試，包含網頁版。在正式出版或發佈之前執行。')
           Result = 'Build\windows-release' }
        @{ N = @('Switch tests on / off', '開啟 / 關閉個別測試'); Cmd = 'tools\test_switches.cmd'; Pause = $false; Window = $true
           T = @('Opens a window with every test as a tree and a checkbox each. Save writes tests\test_switches.json; switched-off tests never run.',
                 '開啟一個視窗，把所有測試列成樹狀、每個都有勾選框。存檔會寫入 tests\test_switches.json；關掉的測試永遠不會執行。')
           Result = 'tests' }
    ) }
    @{ N = @('Setup and checks', '安裝與檢查'); Actions = @(
        @{ N = @('Check prerequisites', '檢查必要工具'); Cmd = 'tools\check_env.cmd'; Pause = $false
           T = @('Checks every prerequisite of TOMS (Visual Studio, CMake, Qt, emsdk, ...) and pops up download links for anything missing.',
                 '檢查 TOMS 需要的所有工具（Visual Studio、CMake、Qt、emsdk…），缺少的會跳出下載連結。')
           Result = '' }
        @{ N = @('Check git over SSH', '檢查 git 的 SSH 連線'); Cmd = 'tools\check_git_ssh.cmd'; Pause = $false
           T = @('Checks that git can fetch and push TOMS over SSH on this PC and offers to fix what is wrong. Details: docs\project\10_SSH_KEY_SETUP.md.',
                 '檢查這台電腦上的 git 能否透過 SSH fetch 和 push TOMS，有問題會提議修正。詳見 docs\project\zh_TW\10_SSH_KEY_SETUP.md。')
           Result = '' }
        @{ N = @('Set up the AI art tools (ComfyUI)', '安裝 AI 美術工具（ComfyUI）'); Cmd = 'tools\setup_ai_art.cmd'; Pause = $false
           T = @('Installs ComfyUI and its models on this PC, or points TOMS at a ComfyUI server on another machine. Optional.',
                 '在這台電腦安裝 ComfyUI 和模型，或讓 TOMS 使用另一台電腦上的 ComfyUI 伺服器。非必要。')
           Result = '' }
        @{ N = @('Set up a brand-new PC', '設定全新的電腦'); Cmd = 'setup_new_pc.bat'; Pause = $false
           C = @('This is for a brand-new PC: it installs Git and Visual Studio, clones the repository, builds and tests. Run it here anyway?',
                 '這是給全新電腦用的：會安裝 Git 和 Visual Studio、clone repository、建置並測試。仍要在這台電腦執行嗎？')
           T = @('For a new PC only: installs Git + Visual Studio 2026, enables long paths, clones the repository, optionally creates an SSH key, then builds and tests. Details: docs\project\09_NEW_MACHINE_SETUP.md.',
                 '只用於新電腦：安裝 Git 和 Visual Studio 2026、開啟長路徑、clone repository、可選擇建立 SSH 金鑰，然後建置並測試。詳見 docs\project\zh_TW\09_NEW_MACHINE_SETUP.md。')
           Result = '' }
    ) }
)

$S = @{
    Title     = @('TOMS launcher: build, deploy, test', 'TOMS 啟動器：建置、發佈、測試')
    Run       = @('Run', '執行')
    RunTicked = @('Run ticked ({0})', '執行勾選的（{0}）')
    Untick    = @('Untick all', '全部取消勾選')
    Waiting   = @('waiting', '等待中')
    Skipped   = @('skipped: an earlier action failed', '略過：前面的動作失敗了')
    Open      = @('Open result folder', '開啟結果資料夾')
    OpenRepo  = @('Open TOMS folder', '開啟 TOMS 資料夾')
    Hint      = @('Tick several actions to run them one after another, in the order of the list; a failure stops the rest. Each runs in its own console window. Double-click an action to run just that one.',
                  '勾選多個動作，會依列表順序一個接一個執行；有一個失敗就略過後面的。每個動作都在自己的主控台視窗執行。雙擊動作只執行那一個。')
    Pick      = @('Pick an action on the left', '請從左邊選一個動作')
    PickText  = @('Windows: the game and the editors. Web: the browser game and GitHub Pages. Android: the APK. Tests and setup checks below.',
                  'Windows 桌面版：遊戲和編輯器。網頁版：瀏覽器遊戲和 GitHub Pages。Android：APK。下面還有測試和安裝檢查。')
    Result    = @('Result: ', '結果：')
    ColRan    = @('Ran', '已執行')
    ColStart  = @('Started', '開始時間')
    ColStatus = @('Status', '狀態')
    Running   = @('running...', '執行中…')
    Ok        = @('finished OK at ', '完成：')
    Failed    = @('FAILED (exit code {0}), see its console', '失敗（結束碼 {0}），請看它的主控台')
    Busy      = @("Still running:`n{0}`n`nTwo builds of the same folder at once can break each other. Start '{1}' anyway?",
                  "還在執行：`n{0}`n`n同時建置同一個資料夾可能會互相破壞。仍要開始「{1}」嗎？")
    CloseBusy = @('{0} action(s) still running in their own console. They keep running after this window closes. Close the launcher?',
                  '還有 {0} 個動作在自己的主控台執行中，關掉這個視窗後它們會繼續執行。要關閉啟動器嗎？')
    StartFail = @("Could not start '{0}':`n{1}", "無法開始「{0}」：`n{1}")
}

# The language: -Lang, else the saved choice, else the Windows display language.
$settingsFile = Join-Path $env:LOCALAPPDATA 'TOMS\launcher.json'
if (-not $Lang) {
    try { $Lang = (Get-Content -Raw -Encoding UTF8 $settingsFile | ConvertFrom-Json).lang } catch { }
}
if ($Lang -notin 'en', 'zh') {
    $Lang = if ((Get-UICulture).Name -match '^zh-(TW|HK|MO|Hant)') { 'zh' } else { 'en' }
}
$L = if ($Lang -eq 'zh') { 1 } else { 0 }
function Save-Language {
    if ($SelfTest) { return }
    try {
        New-Item -ItemType Directory -Force (Split-Path $settingsFile) | Out-Null
        @{ lang = $(if ($L -eq 1) { 'zh' } else { 'en' }) } | ConvertTo-Json | Set-Content -Encoding UTF8 $settingsFile
    } catch { }   # only a convenience: the next start asks Windows again
}

$ui = New-Object Drawing.Font('Microsoft JhengHei UI', 10)
$bold = New-Object Drawing.Font('Microsoft JhengHei UI', 10, [Drawing.FontStyle]::Bold)
$form = New-Object Windows.Forms.Form
$form.Size = New-Object Drawing.Size(1100, 680)
$form.MinimumSize = New-Object Drawing.Size(760, 480)
$form.StartPosition = 'CenterScreen'
$form.Font = $ui

# Top: the language box.
$bar = New-Object Windows.Forms.FlowLayoutPanel
$bar.Dock = 'Top'; $bar.Height = 38
$bar.FlowDirection = 'RightToLeft'
$bar.Padding = '0,5,8,0'
$langBox = New-Object Windows.Forms.ComboBox
$langBox.DropDownStyle = 'DropDownList'
$langBox.Width = 120
[void]$langBox.Items.AddRange(@('English', '繁體中文'))
$langLabel = New-Object Windows.Forms.Label
$langLabel.AutoSize = $true; $langLabel.Margin = '0,6,4,0'
$bar.Controls.AddRange(@($langBox, $langLabel))

$split = New-Object Windows.Forms.SplitContainer
$split.Dock = 'Fill'
$split.Orientation = 'Horizontal'
$top = New-Object Windows.Forms.SplitContainer
$top.Dock = 'Fill'

# Left: the actions. The tree's font is the bold one and the actions get the regular one: a NodeFont bolder than the
# tree's is clipped.
$tree = New-Object Windows.Forms.TreeView
$tree.Dock = 'Fill'
$tree.HideSelection = $false
$tree.FullRowSelect = $true
$tree.ShowLines = $false
$tree.ItemHeight = 24
$tree.Font = $bold
$tree.CheckBoxes = $true   # a ticked action: $action.Ticked (kept when the tree is rebuilt for another language)
$top.Panel1.Controls.Add($tree)

# Right: the selected action.
$info = New-Object Windows.Forms.TableLayoutPanel
$info.Dock = 'Fill'
$info.Padding = '14,10,14,10'
$info.ColumnCount = 1
$info.RowCount = 6
foreach ($h in 'AutoSize', 'Percent', 'AutoSize', 'AutoSize', 'AutoSize', 'Absolute') {
    [void]$info.RowStyles.Add((New-Object Windows.Forms.RowStyle($h, 100)))
}
$info.RowStyles[5].Height = 48   # the hint: two wrapped lines
# The one column is as wide as the pane (an AutoSize column grows to its widest label and runs off the window).
[void]$info.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('Percent', 100)))
function New-Label([Drawing.Font]$font, [Drawing.Color]$color) {
    $l = New-Object Windows.Forms.Label
    $l.Font = $font; $l.ForeColor = $color
    $l.Dock = 'Fill'; $l.AutoSize = $true; $l.Margin = '0,4,0,4'
    $l
}
$title = New-Label (New-Object Drawing.Font('Microsoft JhengHei UI', 14, [Drawing.FontStyle]::Bold)) ([Drawing.SystemColors]::ControlText)
$desc = New-Label $ui ([Drawing.SystemColors]::ControlText)
$desc.AutoSize = $false   # fills its row and wraps (an AutoSize label stays one line)
$result = New-Label $ui ([Drawing.SystemColors]::ControlText)
$cmdLabel = New-Label (New-Object Drawing.Font('Consolas', 9.5)) ([Drawing.Color]::DimGray)
$buttons = New-Object Windows.Forms.FlowLayoutPanel
$buttons.Dock = 'Fill'; $buttons.AutoSize = $true; $buttons.Margin = '0,10,0,0'
$run = New-Object Windows.Forms.Button
$run.Width = 120; $run.Height = 40; $run.Font = $bold
$runTicked = New-Object Windows.Forms.Button
$runTicked.Width = 170; $runTicked.Height = 40; $runTicked.Font = $bold
$untick = New-Object Windows.Forms.Button
$untick.Width = 130; $untick.Height = 40
$open = New-Object Windows.Forms.Button
$open.Width = 150; $open.Height = 40
$openRepo = New-Object Windows.Forms.Button
$openRepo.Width = 150; $openRepo.Height = 40
$buttons.Controls.AddRange(@($run, $runTicked, $untick, $open, $openRepo))
$hint = New-Label (New-Object Drawing.Font('Microsoft JhengHei UI', 9)) ([Drawing.Color]::DimGray)
$hint.AutoSize = $false   # wraps
$info.Controls.Add($title, 0, 0)
$info.Controls.Add($desc, 0, 1)
$info.Controls.Add($result, 0, 2)
$info.Controls.Add($cmdLabel, 0, 3)
$info.Controls.Add($buttons, 0, 4)
$info.Controls.Add($hint, 0, 5)
$top.Panel2.Controls.Add($info)
$split.Panel1.Controls.Add($top)

# Bottom: what ran.
$runs = New-Object Windows.Forms.ListView
$runs.Dock = 'Fill'
$runs.View = 'Details'
$runs.FullRowSelect = $true
$runs.HeaderStyle = 'Nonclickable'
[void]$runs.Columns.Add('', 360)
[void]$runs.Columns.Add('', 90)
[void]$runs.Columns.Add('', 320)
$split.Panel2.Controls.Add($runs)
$form.Controls.Add($split)
$form.Controls.Add($bar)
$split.BringToFront()   # docks after the bar: fills what the bar leaves

function Show-Action($a) {
    if (-not $a) {
        $title.Text = $S.Pick[$L]
        $desc.Text = $S.PickText[$L]
        $result.Text = ''; $cmdLabel.Text = ''
        $run.Enabled = $false; $open.Enabled = $false
        return
    }
    $title.Text = $a.N[$L]
    $desc.Text = $a.T[$L]
    $result.Text = if ($a.Result) { $S.Result[$L] + $a.Result } else { '' }
    $cmdLabel.Text = '> ' + $a.Cmd
    $run.Enabled = $true
    $open.Enabled = [bool]$a.Result -and (Test-Path (Join-Path $root $a.Result))
}

# Every text in the current language; the tree is rebuilt and keeps its selection.
function Set-Texts {
    $form.Text = $S.Title[$L]
    $langLabel.Text = 'Language / 語言'
    $run.Text = $S.Run[$L]; $untick.Text = $S.Untick[$L]; $open.Text = $S.Open[$L]; $openRepo.Text = $S.OpenRepo[$L]
    $hint.Text = $S.Hint[$L]
    $runs.Columns[0].Text = $S.ColRan[$L]; $runs.Columns[1].Text = $S.ColStart[$L]; $runs.Columns[2].Text = $S.ColStatus[$L]
    $selected = if ($tree.SelectedNode) { $tree.SelectedNode.Tag } else { $null }
    $script:checking = $true
    $tree.BeginUpdate()
    $tree.Nodes.Clear()
    $pick = $null
    foreach ($g in $groups) {
        $gn = $tree.Nodes.Add($g.N[$L])
        foreach ($a in $g.Actions) {
            $n = $gn.Nodes.Add($a.N[$L])
            $n.NodeFont = $ui
            $n.Tag = $a
            $n.Checked = [bool]$a.Ticked
            if ($a -eq $selected) { $pick = $n }
        }
        $gn.Checked = @($g.Actions | Where-Object { -not $_.Ticked }).Count -eq 0
    }
    $tree.ExpandAll()
    $tree.EndUpdate()
    $script:checking = $false
    if ($pick) { $tree.SelectedNode = $pick } else { Show-Action $null }
    Set-TickedCount
    foreach ($j in $jobs) { Set-JobText $j }
}

# The ticked actions, in the order of the list.
function Get-Ticked { foreach ($g in $groups) { foreach ($a in $g.Actions) { if ($a.Ticked) { $a } } } }
function Set-TickedCount {
    $n = @(Get-Ticked).Count
    $runTicked.Text = $S.RunTicked[$L] -f $n
    $runTicked.Enabled = $n -gt 0
    $untick.Enabled = $n -gt 0
}
# A tick on a group ticks all of its actions; a tick on an action updates its group's box.
# (Handlers name the sender $src: PowerShell names ignore case, so a $s would hide the texts in $S.)
$checking = $false
$tree.Add_AfterCheck({
    param($src, $e)
    if ($script:checking) { return }
    $script:checking = $true
    if ($e.Node.Tag) {
        $e.Node.Tag.Ticked = $e.Node.Checked
        $all = $true
        foreach ($c in $e.Node.Parent.Nodes) { if (-not $c.Checked) { $all = $false } }
        $e.Node.Parent.Checked = $all
    } else {
        foreach ($c in $e.Node.Nodes) { $c.Checked = $e.Node.Checked; $c.Tag.Ticked = $e.Node.Checked }
    }
    $script:checking = $false
    Set-TickedCount
})

# A job: one run of an action. A chain: the jobs of one Run, started one after another; a failure skips the rest.
$jobs = New-Object Collections.ArrayList
function Set-JobText($j) {
    $j.Item.Text = $j.Action.N[$L]
    $j.Item.SubItems[1].Text = $j.Started
    $j.Item.SubItems[2].Text = switch ($j.State) {
        'waiting' { $S.Waiting[$L] }
        'running' { $S.Running[$L] }
        'skipped' { $S.Skipped[$L] }
        default   { if ($j.Code -eq 0) { $S.Ok[$L] + $j.Ended } else { $S.Failed[$L] -f $j.Code } }
    }
    $j.Item.ForeColor = switch ($j.State) {
        'done'    { if ($j.Code -eq 0) { [Drawing.Color]::DarkGreen } else { [Drawing.Color]::Firebrick } }
        'skipped' { [Drawing.Color]::Gray }
        default   { [Drawing.SystemColors]::WindowText }
    }
}
function Skip-Rest($chain) {
    foreach ($k in $chain) { if ($k.State -eq 'waiting') { $k.State = 'skipped'; Set-JobText $k } }
}
function Start-Job($j) {
    # tools\launcher_run.cmd reads these from its environment (inherited at start).
    $env:TOMS_RUN_TITLE = $j.Action.N[$L]
    $env:TOMS_RUN_CMD = $j.Action.Cmd
    $env:TOMS_RUN_PAUSE = $j.Pause
    $runner = Join-Path $PSScriptRoot 'launcher_run.cmd'
    $j.Started = (Get-Date).ToString('HH:mm:ss')
    try {
        $style = if ($j.Action.Window) { 'Minimized' } else { 'Normal' }
        $j.Process = Start-Process cmd.exe -ArgumentList "/c `"`"$runner`"`"" -WorkingDirectory $root -WindowStyle $style -PassThru
        [void]$j.Process.Handle   # keep the handle so ExitCode is readable once it ends
        $j.State = 'running'
        Set-JobText $j
    } catch {
        $j.State = 'done'; $j.Code = -1
        Set-JobText $j
        Skip-Rest $j.Chain
        [void][Windows.Forms.MessageBox]::Show($form, ($S.StartFail[$L] -f $j.Action.N[$L], $_.Exception.Message), $S.Title[$L], 'OK', 'Error')
    }
}
function Start-Chain($actions) {
    $actions = @($actions)
    $busy = @($jobs | Where-Object { $_.State -in 'waiting', 'running' })
    if ($busy.Count -gt 0) {
        $names = ($busy | ForEach-Object { '  ' + $_.Action.N[$L] }) -join "`n"
        $these = ($actions | ForEach-Object { $_.N[$L] }) -join ', '
        if ([Windows.Forms.MessageBox]::Show($form, ($S.Busy[$L] -f $names, $these), $S.Title[$L], 'YesNo', 'Warning') -ne 'Yes') { return }
    }
    # Every question first, so the chain then runs without waiting on this window.
    $actions = @($actions | Where-Object { -not $_.C -or [Windows.Forms.MessageBox]::Show($form, $_.C[$L], $_.N[$L], 'YesNo', 'Question') -eq 'Yes' })
    if ($actions.Count -eq 0) { return }
    $chain = New-Object Collections.ArrayList
    for ($i = 0; $i -lt $actions.Count; $i++) {
        $a = $actions[$i]
        # In a chain the console closes by itself on success, so the next action starts; it waits on a failure, and
        # the last one waits as a single run does. A command that pauses on its own is never paused again.
        $pause = if ($a.Pause -eq $false) { '0' } elseif ($i -eq $actions.Count - 1) { '1' } else { 'fail' }
        $item = New-Object Windows.Forms.ListViewItem('')
        [void]$item.SubItems.Add(''); [void]$item.SubItems.Add('')
        $runs.Items.Insert($i, $item)   # the newest chain on top, in its own order
        $j = @{ Action = $a; Process = $null; Item = $item; State = 'waiting'; Code = 0; Started = ''; Ended = ''; Pause = $pause; Chain = $chain }
        [void]$chain.Add($j); [void]$jobs.Add($j)
        Set-JobText $j
    }
    Start-Job $chain[0]
}

$timer = New-Object Windows.Forms.Timer
$timer.Interval = 1000
$timer.Add_Tick({
    foreach ($j in @($jobs)) {
        if ($j.State -ne 'running' -or -not $j.Process.HasExited) { continue }
        $j.State = 'done'
        $j.Code = $j.Process.ExitCode
        $j.Ended = (Get-Date).ToString('HH:mm:ss')
        Set-JobText $j
        if ($j.Code -ne 0) { Skip-Rest $j.Chain }
        else {
            $next = $j.Chain | Where-Object { $_.State -eq 'waiting' } | Select-Object -First 1
            if ($next) { Start-Job $next }
        }
        if ($tree.SelectedNode -and $tree.SelectedNode.Tag) { Show-Action $tree.SelectedNode.Tag }   # a result folder may exist now
    }
})
$timer.Start()

$tree.Add_AfterSelect({ Show-Action $tree.SelectedNode.Tag })
$tree.Add_NodeMouseDoubleClick({
    param($src, $e)
    # A double-click on the box only ticks it twice (it never runs anything).
    if ($e.Node.Tag -and $tree.HitTest($e.Location).Location -ne 'StateImage') { Start-Chain $e.Node.Tag }
})
$run.Add_Click({ if ($tree.SelectedNode.Tag) { Start-Chain $tree.SelectedNode.Tag } })
$runTicked.Add_Click({ $t = @(Get-Ticked); if ($t.Count) { Start-Chain $t } })
$untick.Add_Click({
    foreach ($g in $groups) { foreach ($a in $g.Actions) { $a.Ticked = $false } }
    Set-Texts
})
$open.Add_Click({ $a = $tree.SelectedNode.Tag; if ($a -and $a.Result) { Start-Process explorer.exe (Join-Path $root $a.Result) } })
$openRepo.Add_Click({ Start-Process explorer.exe $root })
$langBox.Add_SelectedIndexChanged({
    if ($langBox.SelectedIndex -eq $L) { return }
    $script:L = $langBox.SelectedIndex
    Save-Language
    Set-Texts
})
$form.Add_Shown({
    $split.SplitterDistance = [int]($split.Height - 150)
    $top.SplitterDistance = 340
    $form.Activate()
})
$form.Add_FormClosing({
    param($src, $e)
    $busy = @($jobs | Where-Object { $_.State -in 'waiting', 'running' })
    if ($busy.Count -gt 0 -and -not $SelfTest) {
        if ([Windows.Forms.MessageBox]::Show($form, ($S.CloseBusy[$L] -f $busy.Count), $S.Title[$L], 'YesNo', 'Question') -ne 'Yes') { $e.Cancel = $true }
    }
})

$langBox.SelectedIndex = $L
Set-Texts
if ($SelfTest) {
    # Ticks as clicks make them (the same AfterCheck handler): the Web group, then two actions; a chain in every
    # state (nothing runs).
    $tree.Nodes[1].Checked = $true
    $tree.Nodes[1].Checked = $false
    $tree.Nodes[0].Nodes[0].Checked = $true
    $tree.Nodes[0].Nodes[1].Checked = $true
    if (@(Get-Ticked).Count -ne 2) { throw "self-test: $(@(Get-Ticked).Count) actions ticked, expected 2" }
    $tree.SelectedNode = $tree.Nodes[0].Nodes[1]
    $states = @(@('done', '12:00:00', 0), @('done', '12:03:10', 2), @('skipped', '', 0))
    for ($i = 0; $i -lt 3; $i++) {
        $item = New-Object Windows.Forms.ListViewItem('')
        [void]$item.SubItems.Add(''); [void]$item.SubItems.Add('')
        [void]$runs.Items.Add($item)
        $j = @{ Action = $groups[0].Actions[$i]; Process = $null; Item = $item; State = $states[$i][0]; Code = $states[$i][2]
                Started = $states[$i][1]; Ended = '12:03:10'; Pause = '1'; Chain = $null }
        [void]$jobs.Add($j)
        Set-JobText $j
    }
    $shot = New-Object Windows.Forms.Timer
    $shot.Interval = 800
    $shot.Add_Tick({
        $shot.Stop()
        $bmp = New-Object Drawing.Bitmap($form.Width, $form.Height)
        $form.DrawToBitmap($bmp, (New-Object Drawing.Rectangle(0, 0, $form.Width, $form.Height)))
        $bmp.Save((Join-Path $SelfTest ('launcher_' + $(if ($L -eq 1) { 'zh' } else { 'en' }) + '.png')))
        $form.Close()
    })
    $shot.Start()
}
[void]$form.ShowDialog()
