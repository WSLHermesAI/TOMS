<#
.SYNOPSIS
  The published version: assets/data/version.json = { "number": <how many publishes>, "sha": "<short commit>" }.
  The title screen shows the number (docs/project/07_PUBLISH_GITHUB_PAGES.md section 3b). tools\publish_web.cmd calls this.

.PARAMETER Action
  bump   before a build: no file = number 1; a file = number + 1. sha = the current commit. Remembers the file as it was.
  undo   nothing was published (a dry run, "no" at the question, a failed build): puts the file back as it was.
  adopt  a "nobuild" publish went out: the file takes the number inside the package, if that is higher.

.PARAMETER Dir  Only for its test: where version.json and the note of the old one are.
#>
param([Parameter(Mandatory = $true)][ValidateSet('bump', 'undo', 'adopt')][string]$Action, [string]$Dir = '', [string]$Package = '', [string]$Sha = '')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ($Dir) {
    $file = Join-Path $Dir 'version.json'
    $before = Join-Path $Dir 'version_before.json'
} else {
    $file = Join-Path $root 'assets\data\version.json'
    $before = Join-Path $root 'Build\version_before.json'
}
if (-not $Package) { $Package = Join-Path $root 'Build\dist\TOMS-web\version.json' }
$noFile = 'none'   # the note says: there was no file

function Read-Number($path) {
    $text = Get-Content -Raw -LiteralPath $path
    # A broken file stops the publish: starting again from 1 would quietly lose the count.
    $json = $text | ConvertFrom-Json
    $number = 0
    if (-not [int]::TryParse([string]$json.number, [ref]$number) -or $number -lt 1) { throw "$path has no valid number: $text" }
    return $number
}

function Write-Version($number, $sha) {
    Set-Content -Encoding ASCII -LiteralPath $file ("{`n  `"number`": $number,`n  `"sha`": `"$sha`"`n}")
}

switch ($Action) {
    'bump' {
        if (-not $Sha) {
            $Sha = (& git.exe -C $root rev-parse --short HEAD).Trim()
            if ($LASTEXITCODE) { throw 'git rev-parse failed' }
        }
        $number = 1
        if (Test-Path -LiteralPath $file) {
            $number = (Read-Number $file) + 1
            Copy-Item -LiteralPath $file $before -Force
        } else {
            Set-Content -Encoding ASCII -LiteralPath $before $noFile
        }
        Write-Version $number $Sha
        Write-Host "[toms] version $number ($Sha)"
    }
    'undo' {
        if (-not (Test-Path -LiteralPath $before)) { return }
        if ((Get-Content -Raw -LiteralPath $before).Trim() -eq $noFile) {
            Remove-Item -LiteralPath $file -ErrorAction SilentlyContinue
            Write-Host '[toms] nothing published: no version file, as before'
        } else {
            Copy-Item -LiteralPath $before $file -Force
            Write-Host "[toms] nothing published: back to version $(Read-Number $file)"
        }
        Remove-Item -LiteralPath $before
    }
    'adopt' {
        if (-not (Test-Path -LiteralPath $Package)) { return }
        $inPackage = Read-Number $Package
        if ((Test-Path -LiteralPath $file) -and (Read-Number $file) -ge $inPackage) { return }
        Copy-Item -LiteralPath $Package $file -Force
        Write-Host "[toms] version $inPackage (the package's)"
    }
}
