<#
.SYNOPSIS
Compatibility shim for the script console.

.DESCRIPTION
Run without parameters to open the shared menu. Pass any parameter, or
-NonInteractive alone, to run the historical serial-capture operation.

.PARAMETER StopStaleMonitors
Explicitly allow the capture workflow to close matching stale monitor processes.
#>

[CmdletBinding()]
param(
    [string]$Port = 'COM12',
    [int]$Baud = 115200,
    [int]$DurationSeconds = 30,
    [string]$OutputPath,
    [string]$BuildDir = 'build',
    [int]$WaitSeconds = 30,
    [switch]$StopStaleMonitors,
    [switch]$NonInteractive,
    [switch]$NoBanner
)

$ErrorActionPreference = 'Stop'
$entrypoint = Join-Path $PSScriptRoot 'device.ps1'
if ($PSBoundParameters.Count -eq 0) {
    & $entrypoint
    return
}

$callerDirectory = (Get-Location).Path
$forward = @{
    Action = 'capture'
    NonInteractive = $true
}
foreach ($name in @(
    'Port', 'Baud', 'DurationSeconds', 'BuildDir', 'WaitSeconds',
    'StopStaleMonitors', 'NoBanner'
)) {
    if ($PSBoundParameters.ContainsKey($name)) {
        $forward[$name] = $PSBoundParameters[$name]
    }
}
if (
    $PSBoundParameters.ContainsKey('OutputPath') -and
    -not [string]::IsNullOrWhiteSpace($OutputPath)
) {
    $forward.OutputPath = if ([System.IO.Path]::IsPathRooted($OutputPath)) {
        [System.IO.Path]::GetFullPath($OutputPath)
    }
    else {
        [System.IO.Path]::GetFullPath((Join-Path $callerDirectory $OutputPath))
    }
}

& $entrypoint @forward
