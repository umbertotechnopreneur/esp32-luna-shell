<#
.SYNOPSIS
Compatibility shim for deterministic SFX generation.

.DESCRIPTION
Run without parameters to open the shared menu. Use -NonInteractive, or pass a
configuration option, to invoke the unified entrypoint's SFX action.
#>

[CmdletBinding()]
param(
    [string]$ConfigPath = (
        Join-Path (Split-Path -Parent $PSScriptRoot) 'assets\sfx\sfx-config.json'
    ),
    [switch]$NonInteractive,
    [switch]$NoProgress,
    [switch]$NoBanner
)

$ErrorActionPreference = 'Stop'
$entrypoint = Join-Path $PSScriptRoot 'device.ps1'
if ($PSBoundParameters.Count -eq 0) {
    & $entrypoint
    return
}

& $entrypoint sfx -ConfigPath $ConfigPath -NonInteractive `
    -NoProgress:$NoProgress -NoBanner:$NoBanner
