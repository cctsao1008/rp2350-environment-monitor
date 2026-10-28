#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('check', 'configure', 'build')]
    [string]$Mode = 'build',
    [string]$BuildDir = 'build',
    [string]$PicotoolDir = '',
    [string]$PioasmDir = '',
    [string]$ToolsRoot = $env:PICO_HOST_TOOLS_ROOT
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Command([string]$Name) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        throw "Missing host tool '$Name'. Install it once and add it to PATH."
    }
    Write-Host ("[PASS] {0}: {1}" -f $Name, $command.Source)
}

function Find-InstalledPackage([string]$Name, [string]$Root, [string]$SpecifiedDir) {
    if ($SpecifiedDir) {
        $config = Join-Path $SpecifiedDir "$($Name)Config.cmake"
        if (-not (Test-Path -LiteralPath $config -PathType Leaf)) {
            throw "CMake package config not found: $config"
        }
        $cmd = Get-Command $Name -ErrorAction SilentlyContinue
        $exe = if ($null -eq $cmd) { $null } else { $cmd.Source }
        if (-not $exe) {
            $searchRoot = Split-Path -Parent $SpecifiedDir
            $found = @(Get-ChildItem -LiteralPath $searchRoot -File -Recurse -Filter "$Name.exe" |
                       Select-Object -First 1)
            if ($found.Count -gt 0) { $exe = $found[0].FullName }
        }
        if (-not $exe) { throw "$Name.exe not found beside or below $SpecifiedDir" }
        return @{ ConfigDir = $SpecifiedDir; Exe = $exe }
    }

    if ($Root) {
        $packageRoot = Join-Path $Root $Name
        if (-not (Test-Path -LiteralPath $packageRoot -PathType Container)) {
            throw "Missing $Name in tools root: $packageRoot. Run scripts/setup-tools.ps1 explicitly."
        }
        $configs = @(Get-ChildItem -LiteralPath $packageRoot -File -Recurse -Filter "$($Name)Config.cmake" |
                     Select-Object -First 1)
        $exes = @(Get-ChildItem -LiteralPath $packageRoot -File -Recurse -Filter "$Name.exe" |
                  Select-Object -First 1)
        if ($configs.Count -eq 0 -or $exes.Count -eq 0) {
            throw "Incomplete $Name installation at $packageRoot (missing exe or CMake config)."
        }
        return @{ ConfigDir = $configs[0].DirectoryName; Exe = $exes[0].FullName }
    }

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        throw "Missing $Name. Run scripts/setup-tools.ps1 once, or set PICO_HOST_TOOLS_ROOT to your existing tools directory."
    }
    Write-Host "[PASS] $Name on PATH: $($command.Source); CMake package will be validated during configure."
    return @{ ConfigDir = ''; Exe = $command.Source }
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

    foreach ($tool in @('git', 'cmake', 'ninja', 'arm-none-eabi-gcc')) {
        Assert-Command $tool
    }

    if (-not (Test-Path -LiteralPath $sdkInit -PathType Leaf)) {
        throw "SDK submodule is missing. Run: git submodule update --init --recursive"
    }

    $status = (& git -C $repo submodule status -- lib/pico-sdk | Out-String).TrimEnd()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($status)) {
        throw 'Could not read the SDK submodule status.'
    }
    if ($status -notmatch '^ ([0-9a-f]{40}) ') {
        throw "SDK submodule uninitialized or differs from pinned commit: $status"
    }
    Write-Host "[PASS] SDK submodule: $($Matches[1])"

    if (-not $PicotoolDir) { $PicotoolDir = $env:picotool_DIR }
    if (-not $PioasmDir) { $PioasmDir = $env:pioasm_DIR }

    $picotool = Find-InstalledPackage 'picotool' $ToolsRoot $PicotoolDir
    $pioasm = Find-InstalledPackage 'pioasm' $ToolsRoot $PioasmDir

    # Config-package users and offline host builds need the tool EXE directories
    # even when neither tool is installed globally on PATH.
    $env:PATH = "$(Split-Path -Parent $picotool.Exe);$(Split-Path -Parent $pioasm.Exe);$env:PATH"
    Write-Host "[PASS] picotool: $($picotool.Exe)"
    Write-Host "[PASS] pioasm: $($pioasm.Exe)"

    if ($Mode -eq 'check') {
        Write-Host '[PASS] Host tool files and SDK present. CMake package versions are validated by configure.'
        exit 0
    }

    $cacheFile = Join-Path $buildPath 'CMakeCache.txt'
    if ($Mode -eq 'configure' -or -not (Test-Path -LiteralPath $cacheFile)) {
        $args = @('-S', $repo, '-B', $buildPath, '-G', 'Ninja',
                  "-DPICO_SDK_PATH=$sdk",
                  '-DPICO_BOARD=waveshare_rp2350_pizero',
                  '-DPICO_PLATFORM=rp2350-arm-s',
                  '-DCMAKE_BUILD_TYPE=Release',
                  '-DFETCHCONTENT_FULLY_DISCONNECTED=ON')
        if ($picotool.ConfigDir) { $args += "-Dpicotool_DIR=$($picotool.ConfigDir)" }
        if ($pioasm.ConfigDir) { $args += "-Dpioasm_DIR=$($pioasm.ConfigDir)" }
        Invoke-Checked 'cmake' $args
    }

    if ($Mode -eq 'build') {
        Invoke-Checked 'cmake' @('--build', $buildPath, '--parallel')
        $elf = Join-Path $buildPath 'environment_monitor.elf'
        if (-not (Test-Path -LiteralPath $elf -PathType Leaf)) {
            throw "Build finished but ELF is missing: $elf"
        }
        Write-Host "[PASS] Firmware ELF: $elf"
    }
} catch {
    Write-Error $_.Exception.Message
    exit 1
}
