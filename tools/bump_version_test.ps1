# bump_version_test.ps1 -- tools\bump_version.ps1 in an empty folder: create, increase, undo, adopt, a broken file.
$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'bump_version.ps1'
$dir = Join-Path $env:TEMP ("toms-version-test-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $dir | Out-Null
$file = Join-Path $dir 'version.json'
$failed = 0
function Check($ok, $what) { if ($ok) { Write-Host "  ok   $what" } else { Write-Host "  FAIL $what"; $script:failed++ } }
function Version() { Get-Content -Raw $file | ConvertFrom-Json }
function Run($action, $sha = 'abc1234', $package = '') { & $script -Action $action -Dir $dir -Sha $sha -Package $package | Out-Null }
try {
    Run bump
    Check ((Version).number -eq 1 -and (Version).sha -eq 'abc1234') 'no file: created with number 1 and the sha'
    Run undo
    Check (-not (Test-Path $file)) 'undo of the first: no file again'

    Run bump
    Run bump 'def5678'
    Check ((Version).number -eq 2 -and (Version).sha -eq 'def5678') 'a file: number + 1, the new sha'
    Run bump 'fff0000'
    Run undo
    Check ((Version).number -eq 2 -and (Version).sha -eq 'def5678') 'undo: the number and sha as before'
    Run undo
    Check ((Version).number -eq 2) 'a second undo changes nothing'

    $package = Join-Path $dir 'package.json'
    Set-Content -Encoding ASCII $package '{ "number": 7, "sha": "aaa1111" }'
    Run adopt 'x' $package
    Check ((Version).number -eq 7 -and (Version).sha -eq 'aaa1111') 'adopt: takes the higher number of the package'
    Set-Content -Encoding ASCII $package '{ "number": 3, "sha": "bbb2222" }'
    Run adopt 'x' $package
    Check ((Version).number -eq 7) 'adopt: keeps a higher number of its own'

    Set-Content -Encoding ASCII $file 'not json'
    $stopped = $false
    try { Run bump } catch { $stopped = $true }
    Check ($stopped -and (Get-Content -Raw $file).Trim() -eq 'not json') 'a broken file stops it and stays as it is'
}
finally { Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue }
if ($failed) { Write-Host "bump_version_test: $failed failed"; exit 1 }
Write-Host 'bump_version_test: all passed'
exit 0
