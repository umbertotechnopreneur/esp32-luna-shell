<#
.SYNOPSIS
Compatibility shim for the source-banner workflow.

.DESCRIPTION
Run without parameters to open the shared menu. Explicit parameters preserve
the historical preview/apply/remove CLI through scripts/device.ps1.

.EXAMPLE
pwsh -NoProfile -File .\scripts\add-source-banner.ps1 -NonInteractive

.EXAMPLE
pwsh -NoProfile -File .\scripts\add-source-banner.ps1 -Apply

.EXAMPLE
pwsh -NoProfile -File .\scripts\add-source-banner.ps1 -Remove -Apply
#>

[CmdletBinding()]
param(
    [switch]$Apply,
    [switch]$Remove,
    [switch]$NonInteractive,
    [switch]$NoProgress,
    [switch]$NoBanner,
    [string]$RepositoryRoot = (Split-Path -Parent $PSScriptRoot),
    [string[]]$SourceRoot = @(
        'main',
        'components/LUNASHELL_app',
        'components/LUNASHELL_boot',
        'components/LUNASHELL_logger',
        'components/LUNASHELL_platform'
    )
)

$ErrorActionPreference = 'Stop'
$entrypoint = Join-Path $PSScriptRoot 'device.ps1'
if ($PSBoundParameters.Count -eq 0) {
    & $entrypoint
    return
}

& $entrypoint source-banner -Apply:$Apply -Remove:$Remove `
    -RepositoryRoot $RepositoryRoot -SourceRoot $SourceRoot `
    -NonInteractive -NoProgress:$NoProgress -NoBanner:$NoBanner
