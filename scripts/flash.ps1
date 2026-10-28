#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$BuildDir = 'build',
    [string]$Image = '',
    [string]$Picotool = 'picotool',
    [string]$Serial = '',
    [switch]$NoForce
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

try {
    $repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
    if (-not $Image) {
        $buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
            $BuildDir
        } else {
            Join-Path $repo $BuildDir
        }
        $Image = Join-Path $buildPath 'environment_monitor.elf'
    }

    if (-not (Test-Path -LiteralPath $Image -PathType Leaf)) {
        throw "ELF not found: $Image. Run scripts/build.ps1 first."
    }

    if ($null -eq (Get-Command $Picotool -ErrorAction SilentlyContinue)) {
        throw "picotool not found: $Picotool"
    }

    $args = @('load', '-u', '-v', '-x')
    if (-not $NoForce) { $args += '-f' }
    $args += (Resolve-Path -LiteralPath $Image).Path
    if ($Serial) { $args += @('--ser', $Serial) }

    Write-Host "PICOBOOT flashing: $Image"
    Write-Host "No USB mass-storage drag-and-drop is used."
    & $Picotool @args
    if ($LASTEXITCODE -ne 0) {
        throw "picotool failed (exit code $LASTEXITCODE). If the application cannot reset automatically, enter BOOTSEL manually or use SWD."
    }
    Write-Host '[PASS] Flash verified and reboot requested.'
} catch {
    Write-Error $_.Exception.Message
    exit 1
}
