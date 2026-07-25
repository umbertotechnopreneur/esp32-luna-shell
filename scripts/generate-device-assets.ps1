<#
.SYNOPSIS
Compatibility shim for QSee.ai device-asset generation.

.DESCRIPTION
Run without parameters to open the shared menu. Use -NonInteractive, or pass an
asset option, to invoke the unified entrypoint's all-assets action.
#>

[CmdletBinding()]
param(
    [switch]$NonInteractive,
    [switch]$NoProgress,
    [switch]$NoBanner,
    [string]$ConfigPath,
    [string]$BootSource,
    [string]$BootOutputDirectory,
    [string]$AssetPython
)

$ErrorActionPreference = 'Stop'
$entrypoint = Join-Path $PSScriptRoot 'device.ps1'
if ($PSBoundParameters.Count -eq 0) {
    & $entrypoint
    return
}

$forward = @{
    Action = 'assets'
    NonInteractive = $true
    NoProgress = $NoProgress
    NoBanner = $NoBanner
}
foreach ($name in @('ConfigPath', 'BootSource', 'BootOutputDirectory', 'AssetPython')) {
    if ($PSBoundParameters.ContainsKey($name)) {
        $forward[$name] = $PSBoundParameters[$name]
    }
}

& $entrypoint @forward
