<#
.SYNOPSIS
Capture a bounded LUNASHELL serial log through scripts/device.ps1.

.DESCRIPTION
This is a thin alias for the supported device.ps1 capture action. It accepts
only parameters that device.ps1 understands and never falls back to a build.
#>

[CmdletBinding()]
param(
    [string]$Port,
    [int]$Baud = 115200,
    [ValidateRange(1, 86400)]
    [int]$DurationSeconds = 30,
    [string]$OutputPath,
    [string]$BuildDir = 'build',
    [int]$WaitSeconds = 30,
    [switch]$ResetBeforeCapture,
    [string]$IdfPath,
    [string]$ToolsPath,
    [string]$PythonExe,
    [int]$Jobs = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Port)) {
    throw 'Specify the serial port explicitly, for example -Port COM12.'
}

$entrypoint = Join-Path $PSScriptRoot 'device.ps1'
$resolvedOutputPath = if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $null
}
elseif ([System.IO.Path]::IsPathRooted($OutputPath)) {
    [System.IO.Path]::GetFullPath($OutputPath)
}
else {
    [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $OutputPath))
}

& $entrypoint capture `
    -Port $Port `
    -Baud $Baud `
    -DurationSeconds $DurationSeconds `
    -OutputPath $resolvedOutputPath `
    -BuildDir $BuildDir `
    -WaitSeconds $WaitSeconds `
    -ResetBeforeCapture:$ResetBeforeCapture `
    -IdfPath $IdfPath `
    -ToolsPath $ToolsPath `
    -PythonExe $PythonExe `
    -Jobs $Jobs
