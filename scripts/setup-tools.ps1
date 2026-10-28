#Requires -Version 5.1
# Explicit, one-time installation of upstream prebuilt Windows x64 host tools.
# Never called by build.ps1; normal builds do not download dependencies.
[CmdletBinding()]
param(
    [string]$InstallRoot = 'C:\Toolchains\pico-tools\2.3.1'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$packages = @(
    @{
        Name = 'picotool'
        Archive = 'picotool-2.3.1-x64-win.zip'
        Sha256 = '68730be0813f8f35be2cca147cf7f1572662d5dcf0f5ba468e02a6dd9e85db2b'
    },
    @{
        Name = 'pioasm'
        Archive = 'pico-sdk-tools-2.3.1-x64-win.zip'
        Sha256 = 'c5d18dae371eb088da61a038ccf56a23fbd19171a319461efebeae1646c06ade'
    }
)

function Find-Package([string]$Directory, [string]$Name) {
    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) { return $null }
    $config = @(Get-ChildItem -LiteralPath $Directory -File -Recurse -Filter "$($Name)Config.cmake" |
                Select-Object -First 1)
    $exe = @(Get-ChildItem -LiteralPath $Directory -File -Recurse -Filter "$Name.exe" |
             Select-Object -First 1)
    if ($config.Count -eq 0 -or $exe.Count -eq 0) { return $null }
    return @{ ConfigDir = $config[0].DirectoryName; Exe = $exe[0].FullName }
}

try {
    if (-not [Environment]::Is64BitOperatingSystem) {
        throw 'These pinned archives require Windows x64.'
    }

    New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
    foreach ($p in $packages) {
        $destination = Join-Path $InstallRoot $p.Name
        $existing = Find-Package $destination $p.Name

        if ($null -ne $existing) {
            Write-Host "[SKIP] $($p.Name) already installed at $destination"
            continue
        }
        if (Test-Path -LiteralPath $destination) {
            throw "Incomplete install at $destination. Inspect/remove it manually before retrying."
        }

        $url = "https://github.com/raspberrypi/pico-sdk-tools/releases/download/v2.3.1-0/$($p.Archive)"
        $zip = Join-Path $InstallRoot $p.Archive
        try {
            Write-Host "[DOWNLOAD ONCE] $url"
            Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
            $actual = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($actual -ne $p.Sha256) {
                throw "SHA256 mismatch for $($p.Archive). Expected $($p.Sha256), got $actual"
            }
            Expand-Archive -LiteralPath $zip -DestinationPath $destination
            $installed = Find-Package $destination $p.Name
            if ($null -eq $installed) {
                throw "The archive does not contain both $($p.Name).exe and $($p.Name)Config.cmake. Inspect $destination."
            }
            Write-Host "[PASS] $($p.Name) package: $($installed.ConfigDir)"
        } finally {
            if (Test-Path -LiteralPath $zip) {
                Remove-Item -LiteralPath $zip -Force
            }
        }
    }

    $resolved = (Resolve-Path -LiteralPath $InstallRoot).Path
    $env:PICO_HOST_TOOLS_ROOT = $resolved
    [Environment]::SetEnvironmentVariable('PICO_HOST_TOOLS_ROOT', $resolved, 'User')
    Write-Host "PICO_HOST_TOOLS_ROOT=$resolved"
    Write-Host '[PASS] Installed tools are reusable across projects. Start a new PowerShell for the saved user environment variable.'
} catch {
    Write-Error $_.Exception.Message
    exit 1
}
