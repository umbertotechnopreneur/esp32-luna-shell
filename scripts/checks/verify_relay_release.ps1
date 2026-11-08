# RelayRoot identifies the separate private checkout containing the release safety checks.
# Throws if the checks are unavailable or any assertion fails.
param([string]$RelayRoot)
$ErrorActionPreference = 'Stop'
$publicRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $RelayRoot) { $RelayRoot = Join-Path (Split-Path $publicRoot -Parent) 'cody-cloud' }
$suite = Join-Path $RelayRoot 'scripts/checks/verify-release.ps1'
if (-not (Test-Path -LiteralPath $suite)) { throw 'Select the current private relay checkout with -RelayRoot.' }
& $suite -PublicRoot $publicRoot
