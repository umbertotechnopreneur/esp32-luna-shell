# RelayRoot selects the separate proprietary checkout, outside the public tree.
# RelayOperation selects one explicit operation in the private implementation.
# RelayConfig selects its private nonsecret deployment configuration.
# RelayRevision identifies the existing revision for explicit recovery.
# BuildMode selects the source compilation mode.
# Jobs bounds compiler parallelism.
# WaitSeconds bounds Azure convergence waits.
# Throws on a missing private checkout, invalid options or failed operation.
[CmdletBinding()]
param(
    [string]$RelayRoot,
    [ValidateSet('configure','status','build','provision','publish','deploy','expose','close','recover','check')]
    [string]$RelayOperation = 'status',
    [string]$RelayConfig,
    [string]$RelayRevision,
    [ValidateSet('Debug','Release')][string]$BuildMode = 'Release',
    [ValidateRange(0,8)][int]$Jobs = 0,
    [ValidateRange(1,86400)][int]$WaitSeconds = 600
)
$ErrorActionPreference = 'Stop'
$publicRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $RelayRoot) { $RelayRoot = Join-Path (Split-Path $publicRoot -Parent) 'cody-cloud' }
$privateRoot = (Resolve-Path -LiteralPath $RelayRoot -ErrorAction Stop).Path
$relative = [IO.Path]::GetRelativePath($publicRoot, $privateRoot)
if ($relative -eq '.' -or (-not [IO.Path]::IsPathRooted($relative) -and
        $relative -ne '..' -and -not $relative.StartsWith('..' + [IO.Path]::DirectorySeparatorChar))) {
    throw 'The private relay checkout must be outside the public repository.'
}
$implementation = Join-Path $privateRoot 'scripts/commands/relay.ps1'
if (-not (Test-Path -LiteralPath $implementation -PathType Leaf)) {
    throw 'The private relay command is missing. Select the current cody-cloud checkout with -RelayRoot.'
}
$parameters = @{
    Operation = $RelayOperation; ConfigurationPath = $RelayConfig
    Revision = $RelayRevision; BuildMode = $BuildMode
    Jobs = $Jobs; WaitSeconds = $WaitSeconds
}
& $implementation @parameters
