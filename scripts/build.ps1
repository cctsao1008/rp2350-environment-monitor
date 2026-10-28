#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('check', 'configure', 'build')]
    [string]$Mode = 'build',
    [string]$BuildDir = 'build',
    [string]$PicotoolDir = '',
    [string]$PioasmDir = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Tool([string]$Name) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        throw "Missing host tool '$Name'. Install it once and add it to PATH."
    }
    Write-Host ("[PASS] {0}: {1}" -f $Name, $command.Source)
}

function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "'$Executable' failed (exit code $LASTEXITCODE)."
    }
}

try {
    $repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
    $sdk = Join-Path $repo 'lib/pico-sdk'
    $sdkInit = Join-Path $sdk 'pico_sdk_init.cmake'
    $buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
        $BuildDir
    } else {
        Join-Path $repo $BuildDir
    }

    Write-Host "RP2350 Environment Monitor - $Mode"
    Write-Host "Repository: $repo"

    foreach ($tool in @('git', 'cmake', 'ninja', 'arm-none-eabi-gcc',
                        'picotool', 'pioasm')) {
        Assert-Tool $tool
    }

    if (-not (Test-Path -LiteralPath $sdkInit -PathType Leaf)) {
        throw "SDK submodule is missing. Run: git submodule update --init --recursive"
    }

    $status = (& git -C $repo submodule status -- lib/pico-sdk | Out-String).TrimEnd()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($status)) {
        throw 'Could not read the SDK submodule status.'
    }
    if ($status -notmatch '^ ([0-9a-f]{40}) ') {
        throw "SDK submodule is uninitialized or differs from its pinned commit: $status"
    }

    Write-Host "[PASS] SDK submodule at pinned commit: $($Matches[1])"

    if (-not $PicotoolDir) { $PicotoolDir = $env:picotool_DIR }
    if (-not $PioasmDir) { $PioasmDir = $env:pioasm_DIR }

    foreach ($pair in @(@('picotool', $PicotoolDir),
                         @('pioasm', $PioasmDir))) {
        if ($pair[1]) {
            $configFile = Join-Path $pair[1] "$($pair[0])Config.cmake"
            if (-not (Test-Path -LiteralPath $configFile -PathType Leaf)) {
                throw "Missing $configFile; point -$($pair[0])Dir at the CMake package directory."
            }
            Write-Host "[PASS] $($pair[0]) CMake package: $($pair[1])"
        }
    }

    if ($Mode -eq 'check') {
        Write-Host "[PASS] CLI prerequisites. CMake package compatibility is checked during configure."
        exit 0
    }

    $cacheFile = Join-Path $buildPath 'CMakeCache.txt'
    if ($Mode -eq 'configure' -or -not (Test-Path $cacheFile)) {
        $args = @('-S', $repo, '-B', $buildPath, '-G', 'Ninja',
                  "-DPICO_SDK_PATH=$sdk",
                  '-DPICO_BOARD=waveshare_rp2350_pizero',
                  '-DPICO_PLATFORM=rp2350-arm-s',
                  '-DCMAKE_BUILD_TYPE=Release',
                  '-DFETCHCONTENT_FULLY_DISCONNECTED=ON')
        if ($PicotoolDir) { $args += "-Dpicotool_DIR=$PicotoolDir" }
        if ($PioasmDir) { $args += "-Dpioasm_DIR=$PioasmDir" }
        Invoke-Checked 'cmake' $args
    }

    if ($Mode -eq 'build') {
        Invoke-Checked 'cmake' @('--build', $buildPath, '--parallel')
        Write-Host "[PASS] Firmware ELF: $(Join-Path $buildPath 'environment_monitor.elf')"
    }
} catch {
    Write-Error $_.Exception.Message
    exit 1
}
