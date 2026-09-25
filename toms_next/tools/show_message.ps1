<#
.SYNOPSIS
  Shows a Windows message box listing missing prerequisites, and offers to open the download pages.
  Called by cmake/TomsPrerequisites.cmake during configure, and by check_env.ps1.
#>
param(
    [Parameter(Mandatory = $true)][string]$Title,
    [string]$Message = "",
    [string]$MessageFile = "",
    [string]$LinkFile = "",
    [string[]]$Links = @()
)

if ($MessageFile -and (Test-Path $MessageFile)) { $Message = Get-Content -Raw -Encoding UTF8 $MessageFile }
if ($LinkFile -and (Test-Path $LinkFile)) {
    $Links += Get-Content -Encoding UTF8 $LinkFile | Where-Object { $_ -match '^https?://' }
}
$Links = $Links | Select-Object -Unique

try {
    Add-Type -AssemblyName System.Windows.Forms -ErrorAction Stop
} catch {
    Write-Host "[$Title]`n$Message"
    exit 0
}

if ($Links.Count -gt 0) {
    $text = $Message + "`n`nOpen the download page(s) in your browser now?"
    $answer = [System.Windows.Forms.MessageBox]::Show($text, $Title,
        [System.Windows.Forms.MessageBoxButtons]::YesNo,
        [System.Windows.Forms.MessageBoxIcon]::Warning)
    if ($answer -eq [System.Windows.Forms.DialogResult]::Yes) {
        foreach ($l in $Links) { Start-Process $l }
    }
} else {
    [void][System.Windows.Forms.MessageBox]::Show($Message, $Title,
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Warning)
}
exit 0
