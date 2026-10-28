#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$BuildDir = 'build',
    [string]$Image = '',
    [string]$Picotool = 'picotool',
    [string]$Serial = '',
    [string]$ToolsRoot = $env:PICO_HOST_TOOLS_ROOT,
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

    $command = Get-Command $Picotool -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        $Picotool = $command.Source
    } elseif ($ToolsRoot) {
        $folder = Join-Path $ToolsRoot 'picotool'
        $exe = @(Get-ChildItem -LiteralPath $folder -Recurse -File -Filter 'picotool.exe' |
                 Select-Object -First 1)
        if ($exe.Count -eq 0) { throw "picotool.exe missing in $folder" }
        $Picotool = $exe[0].FullName
    } else {
        throw 'picotool not found. Set PICO_HOST_TOOLS_ROOT or provide -Picotool.'
    }

    $args = @('load', '-u', '-v', '-x')
    if (-not $NoForce) { $args += '-f' }
    $args += (Resolve-Path -LiteralPath $Image).Path
    if ($Serial) { $args += @('--ser', $Serial) }

    Write-Host "PICOBOOT flashing: $Image"
    Write-Host 'No USB mass-storage drag-and-drop is used.'
    & $Picotool @args
    if ($LASTEXITCODE -ne 0) {
        throw "picotool failed (exit code $LASTEXITCODE). If auto-reset is unavailable, enter BOOTSEL manually or use SWD."
    }
    Write-Host '[PASS] Flash verified and reboot requested.'
} catch {
    Write-Error $_.Exception.Message
    exit 1
}
