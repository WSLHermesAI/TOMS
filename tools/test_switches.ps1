# test_switches.ps1 -- switch tests on / off for tools\test.cmd (docs/09_TESTS.md). Run it with tools\test_switches.cmd.
#
#   test_switches.cmd              a window: the tests as a tree (group > area > test), a checkbox each; Save
#   test_switches.cmd --list       the same tree as text: [x] on, [ ] off
#   test_switches.cmd [preset]     another build's test list (default windows-release)
#
# The tree comes from the build's own test list (ctest), so build first (tools\build.cmd). Unticked tests are written
# to tests\test_switches.json; tools\test_select.py never runs them (changed-only or `all`).
param([string]$Preset = 'windows-release', [switch]$List, [string]$SelfTest = '')
# -SelfTest <folder>: opens the window, unticks the web group as a click would, saves to <folder>\test_switches.json,
# writes <folder>\test_switches.png and closes (an automated look at the window).

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$switchFile = Join-Path $root 'tests\test_switches.json'
$saveFile = if ($SelfTest) { Join-Path $SelfTest 'test_switches.json' } else { $switchFile }

# ---- the tests: ctest's list (Visual Studio's ctest, as test.cmd uses) ----
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { Write-Host '[toms] Visual Studio not found (vswhere).'; exit 1 }
$vs = & $vswhere -latest -products * -property installationPath
$ctest = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
if (-not (Test-Path (Join-Path $root "Build\$Preset\CTestTestfile.cmake"))) {
    Write-Host "[toms] Build\$Preset has no tests yet: run tools\build.cmd $Preset first."; exit 1
}
Push-Location $root
try { $json = (& $ctest --preset $Preset --show-only=json-v1) -join "`n" } finally { Pop-Location }
$tests = @(($json | ConvertFrom-Json).tests | ForEach-Object {
    $labels = @(); foreach ($p in $_.properties) { if ($p.name -eq 'LABELS') { $labels = @($p.value) } }
    [pscustomobject]@{ Name = $_.name; Labels = $labels }
})

# unit test -> its folder under src/core (tests/CMakeLists.txt's TOMS_UNIT_TESTS), for the "area" level
$unitDir = @{}
$cm = Get-Content (Join-Path $root 'tests\CMakeLists.txt') -Raw
if ($cm -match '(?s)set\(TOMS_UNIT_TESTS(.*?)\)') {
    foreach ($path in ($Matches[1] -split '\s+' | Where-Object { $_ })) {
        $unitDir['unit.' + ($path -split '/')[-1]] = ($path -split '/' | Select-Object -SkipLast 1) -join '/'
    }
}
function Get-Area($t) {
    $group = ($t.Name -split '\.', 2)[0]
    if ($group -eq 'unit') { if ($unitDir.ContainsKey($t.Name)) { return $unitDir[$t.Name] } else { return 'tools' } }
    if ($group -eq 'smoke') {
        if ($t.Name -match 'gltf') { return 'glTF' }
        if ($t.Name -match '_(d3d12|vulkan|opengl|cpu|compute|gpu)$') { return 'backend variants' }
        return 'game'
    }
    return ''
}

# ---- the switches ----
$off = New-Object 'System.Collections.Generic.HashSet[string]'
if (Test-Path $switchFile) {
    try { foreach ($n in ((Get-Content $switchFile -Raw -Encoding UTF8) | ConvertFrom-Json).off) { [void]$off.Add($n) } }
    catch { Write-Host "[toms] $switchFile unreadable: starting with every test on" }
}
function Save-Switches([string[]]$names) {
    $names = @($names | Sort-Object)
    $items = ($names | ForEach-Object { '    "' + $_ + '"' }) -join ",`n"
    $text = "{`n  `"about`": `"Tests switched off: tools\\test_select.py never runs them (changed-only or all). Edit with tools\\test_switches.cmd.`",`n" +
            "  `"off`": [" + $(if ($names.Count) { "`n$items`n  " } else { '' }) + "]`n}`n"
    [IO.File]::WriteAllText($saveFile, $text, (New-Object Text.UTF8Encoding($false)))
}

# group > area > tests, sorted
$tree = [ordered]@{}
foreach ($t in ($tests | Sort-Object Name)) {
    $g = ($t.Name -split '\.', 2)[0]; $a = Get-Area $t
    if (-not $tree.Contains($g)) { $tree[$g] = [ordered]@{} }
    if (-not $tree[$g].Contains($a)) { $tree[$g][$a] = New-Object System.Collections.ArrayList }
    [void]$tree[$g][$a].Add($t)
}

# ---- text mode ----
if ($List) {
    $on = @($tests | Where-Object { -not $off.Contains($_.Name) }).Count
    Write-Host "tests on: $on of $($tests.Count)   (off: tests\test_switches.json)"
    foreach ($g in $tree.Keys) {
        $all = @($tree[$g].Values | ForEach-Object { $_ }); $n = @($all | Where-Object { -not $off.Contains($_.Name) }).Count
        Write-Host ("{0}  ({1}/{2} on)" -f $g, $n, $all.Count)
        foreach ($a in $tree[$g].Keys) {
            $ind = '  '
            if ($a) { Write-Host "  $a"; $ind = '    ' }
            foreach ($t in $tree[$g][$a]) {
                $mark = if ($off.Contains($t.Name)) { '[ ]' } else { '[x]' }
                Write-Host ("{0}{1} {2}   ({3})" -f $ind, $mark, $t.Name, ($t.Labels -join ', '))
            }
        }
    }
    exit 0
}

# ---- the window ----
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
$form = New-Object Windows.Forms.Form
$form.Text = 'TOMS tests: on / off'
$form.Size = New-Object Drawing.Size(560, 720)
$form.StartPosition = 'CenterScreen'

$tv = New-Object Windows.Forms.TreeView
$tv.CheckBoxes = $true
$tv.Dock = 'Fill'
$tv.ShowNodeToolTips = $true
$tv.Font = New-Object Drawing.Font('Segoe UI', 10)

$status = New-Object Windows.Forms.Label
$status.Dock = 'Top'; $status.Height = 26; $status.TextAlign = 'MiddleLeft'; $status.Padding = '6,0,0,0'

$bar = New-Object Windows.Forms.FlowLayoutPanel
$bar.Dock = 'Bottom'; $bar.Height = 40; $bar.FlowDirection = 'RightToLeft'; $bar.Padding = '4,4,4,4'
function New-Button($text, $action) {
    $b = New-Object Windows.Forms.Button; $b.Text = $text; $b.AutoSize = $true; $b.Add_Click($action); [void]$bar.Controls.Add($b); $b
}

$script:busy = $false
function Update-Status {
    $leaves = @(); foreach ($n in $tv.Nodes) { $leaves += Get-Leaves $n }
    $on = @($leaves | Where-Object { $_.Checked }).Count
    $status.Text = "On: $on of $($leaves.Count)    Unticked tests never run (tests\test_switches.json)."
}
function Get-Leaves($node) {
    if ($node.Tag) { return ,$node }
    $r = @(); foreach ($c in $node.Nodes) { $r += Get-Leaves $c }; return $r
}
function Sync-Parent($node) {
    $p = $node.Parent
    while ($p) {
        $all = $true; foreach ($c in $p.Nodes) { if (-not $c.Checked) { $all = $false } }
        $p.Checked = $all; $p = $p.Parent
    }
}
function Set-Children($node, $state) { foreach ($c in $node.Nodes) { $c.Checked = $state; Set-Children $c $state } }

$tv.Add_AfterCheck({
    param($s, $e)
    if ($script:busy -or $e.Action -eq 'Unknown') { return }
    $script:busy = $true
    try { Set-Children $e.Node $e.Node.Checked; Sync-Parent $e.Node; Update-Status } finally { $script:busy = $false }
})

function Build-Tree {
    $script:busy = $true
    $tv.BeginUpdate(); $tv.Nodes.Clear()
    foreach ($g in $tree.Keys) {
        $gn = $tv.Nodes.Add($g)
        foreach ($a in $tree[$g].Keys) {
            $parent = $gn
            if ($a) { $parent = $gn.Nodes.Add($a) }
            foreach ($t in $tree[$g][$a]) {
                $leaf = $parent.Nodes.Add($t.Name)
                $leaf.Tag = $t.Name
                $leaf.ToolTipText = 'labels: ' + ($t.Labels -join ', ')
                $leaf.Checked = -not $off.Contains($t.Name)
            }
        }
    }
    foreach ($n in $tv.Nodes) { $n.Expand() }
    $tv.EndUpdate()
    # (again, after drawing: expanding a node inside BeginUpdate drops its children's checkbox states)
    foreach ($n in $tv.Nodes) {
        foreach ($l in (Get-Leaves $n)) { $l.Checked = -not $off.Contains([string]$l.Tag) }
        foreach ($l in (Get-Leaves $n)) { Sync-Parent $l }
    }
    $script:busy = $false
    Update-Status
}

$saveButton = (New-Button 'Save' {
    $names = @(); foreach ($n in $tv.Nodes) { foreach ($l in (Get-Leaves $n)) { if (-not $l.Checked) { $names += $l.Tag } } }
    Save-Switches $names
    $off.Clear(); foreach ($x in $names) { [void]$off.Add($x) }
    $status.Text = "Saved: $($names.Count) test(s) off -> tests\test_switches.json"
})
[void](New-Button 'All on' { $script:busy = $true; foreach ($n in $tv.Nodes) { $n.Checked = $true; Set-Children $n $true }; $script:busy = $false; Update-Status })
[void](New-Button 'Expand all' { $tv.ExpandAll() })
[void](New-Button 'Collapse' { $tv.CollapseAll() })

$form.Controls.Add($tv)
$form.Controls.Add($status)
$form.Controls.Add($bar)
# Built once the window is shown: a TreeView drops the checkbox states set before its window exists.
$form.Add_Shown({
    Build-Tree
    if ($SelfTest) { $timer.Start() }
})
if ($SelfTest) {
    $timer = New-Object Windows.Forms.Timer
    $timer.Interval = 600
    $timer.Add_Tick({
        $timer.Stop()
        foreach ($n in $tv.Nodes) { if ($n.Text -eq 'web') { $n.Checked = $false; Set-Children $n $false; Sync-Parent $n } }
        Update-Status
        $saveButton.PerformClick()
        $bmp = New-Object Drawing.Bitmap($form.Width, $form.Height)
        $form.DrawToBitmap($bmp, (New-Object Drawing.Rectangle(0, 0, $form.Width, $form.Height)))
        $bmp.Save((Join-Path $SelfTest 'test_switches.png'))
        $form.Close()
    })
}
[void]$form.ShowDialog()
