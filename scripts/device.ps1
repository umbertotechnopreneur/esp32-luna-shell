<#
.SYNOPSIS
Cody Companion console: simulator, firmware, assets and explicit checks.
.DESCRIPTION
No arguments opens the menu without loading ESP-IDF or probing serial ports.
Use 'help' or 'list' to discover the CLI. Runtime output belongs in artifacts/.
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('menu','help','list','simulator','simulator-luna','boot-screens',
        'check','generate','banner','package-simulator','clean-artifacts',
        'build','clean','fullclean','flash','reset','monitor','capture','erase',
        'reconfigure','set-target','update-lock','size','combo','ports','discovery','port-status','recover')]
    [string]$Action = 'menu',
    [string]$Port = 'COM8',
    [ValidateRange(1,4000000)][int]$Baud = 115200,
    [string]$BuildDir,
    [ValidateSet('full','connected','ui-minimal','display-touch','headless','leg-edition','cody-companion')]
    [string]$Profile = 'cody-companion',
    [ValidateSet('Debug','Release')]
    [string]$BuildMode = 'Debug',
    [ValidateSet('none','distance','wake-word')][string]$Feature = 'none',
    [string]$BoardConfig,
    [ValidateRange(1,86400)][int]$WaitSeconds = 30,
    [ValidateRange(1,86400)][int]$DurationSeconds = 30,
    [string]$OutputPath,
    [ValidateSet('default','text','csv','json2','tree','raw')][string]$SizeOutputFormat = 'default',
    [string]$SizeOutputFile,
    [string]$LogPath,
    [switch]$ResetBeforeCapture,
    [string]$IdfPath,
    [string]$ToolsPath,
    [string]$PythonExe,
    [ValidateRange(0,8)][int]$Jobs = 0,
    [ValidateSet('rig','greeting','boot-sleep','boot-waking','boot-ready')][string]$Screen = 'rig',
    [ValidateSet(0,90,180,270)][int]$Rotation = 0,
    [ValidateRange(0,2147483647)][int]$AtMs = 1000,
    [switch]$SafeGuide,
    [switch]$CloudMaterialStudy,
    [switch]$CloudPngMeshStudy,
    [switch]$CloudJellyStudy,
    [switch]$MeshOverlay,
    [string]$PythonExecutable,
    [string]$Skin,
    [string]$Check = 'cody-poc',
    [string]$Generator = 'cody-rig',
    [string[]]$ToolArguments = @()
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'common/Artifacts.ps1')
. (Join-Path $PSScriptRoot 'common/Commands.ps1')
. (Join-Path $PSScriptRoot 'common/Console.ps1')
. (Join-Path $PSScriptRoot 'common/Help.ps1')
$contract = Get-DeviceActionParameters
Assert-DeviceActionParameters -RequestedAction $Action -BoundParameters $PSBoundParameters -Contract $contract

if ($Action -in @('help','list')) {
    Show-DeviceHelp
    return
}
if ($Action -eq 'menu') {
    . (Join-Path $PSScriptRoot 'commands/menu.ps1')
    Show-DeviceMenu -InitialSettings $PSBoundParameters
    return
}

# Resolve every public output before cleanup, toolchain initialization or I/O.
$dispatch = @{}
foreach ($key in $PSBoundParameters.Keys) { $dispatch[$key] = $PSBoundParameters[$key] }
$dispatch.Action = $Action
$resolvedLog = Resolve-ArtifactPath $LogPath "artifacts/logs/$Action.log"
if ($BuildDir) { $dispatch.BuildDir = Resolve-ArtifactPath $BuildDir '' }
if ($OutputPath -or $Action -eq 'capture') {
    $dispatch.OutputPath = Resolve-ArtifactPath $OutputPath 'artifacts/serial/capture.log'
    if ($dispatch.OutputPath -eq $resolvedLog) { throw 'OutputPath and LogPath must be different files.' }
}
if ($SizeOutputFile -or $Action -eq 'size') {
    $extension = if ($SizeOutputFormat -in @('json2','raw')) { 'json' } elseif ($SizeOutputFormat -eq 'csv') { 'csv' } else { 'txt' }
    $dispatch.SizeOutputFile = Resolve-ArtifactPath $SizeOutputFile "artifacts/reports/size.$extension"
    if ($dispatch.SizeOutputFile -eq $resolvedLog) { throw 'SizeOutputFile and LogPath must be different files.' }
}
if ($Action -in @('check','generate')) {
    $selectedTool = if ($Action -eq 'check') { Find-DeviceTool 'checks' $Check } else { Find-DeviceTool 'generators' $Generator }
    if ($PythonExecutable -and $selectedTool.Extension -ne '.py') {
        throw '-PythonExecutable selects Python tools only. Pass PowerShell tool options through -ToolArguments.'
    }
}

$operationLock = $null
$savedEnvironment = @{}
$cleanupMessages = @()
$failureLogged = $false
try {
    # Serialize output writers too: a build must not remove an active simulator/check log.
    $operationLock = Open-DeviceArtifactLock
    if ($Action -in @('build','flash','combo','recover','clean-artifacts')) {
        $protected = @()
        if ($dispatch.ContainsKey('BuildDir')) { $protected += $dispatch.BuildDir }
        $cleanupMessages = @(Clear-DeviceArtifacts -ProtectedPaths $protected 6>&1)
    }
    foreach ($destination in @($resolvedLog,$dispatch.OutputPath,$dispatch.SizeOutputFile)) {
        if ($destination) { $null = New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force }
    }
    $temporaryRoot = Resolve-ArtifactPath '' 'artifacts/tmp'
    $null = New-Item -ItemType Directory -Path $temporaryRoot -Force
    $environment = @{
        PYTHONDONTWRITEBYTECODE = '1'; PYTHONUTF8 = '1'; PYTHONIOENCODING = 'utf-8'
        TEMP = $temporaryRoot; TMP = $temporaryRoot
        PIP_CACHE_DIR = (Join-Path $ProjectRoot 'artifacts/python/pip-cache')
    }
    foreach ($name in $environment.Keys) {
        $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name,'Process')
        [Environment]::SetEnvironmentVariable($name,$environment[$name],'Process')
    }
    Push-Location $ProjectRoot
    try {
        # One logger owns the complete operation; internal commands never relaunch it.
        & {
            foreach ($message in $cleanupMessages) { Write-Output ([string]$message) }
            Invoke-DeviceCommand -RequestedAction $Action -Options $dispatch
        } *>&1 |
            Tee-Object -FilePath $resolvedLog
    } catch {
        Add-Content -LiteralPath $resolvedLog -Value "FAILED: $($_.Exception.Message)"
        $failureLogged = $true
        throw
    } finally {
        Pop-Location
    }
    Write-Host "Log: $resolvedLog"
} catch {
    # Cleanup failures precede the command logger. Preserve their real error and record it if safe.
    $originalFailure = $_
    if (-not $failureLogged -and $null -ne $operationLock) {
        try {
            $failureLog = Resolve-ArtifactPath $resolvedLog ''
            $null = New-Item -ItemType Directory -Path (Split-Path -Parent $failureLog) -Force
            Add-Content -LiteralPath $failureLog -Value "FAILED before command dispatch: $($originalFailure.Exception.Message)"
        } catch {
            Write-Warning "Could not record the operation failure: $($_.Exception.Message)"
        }
    }
    throw $originalFailure
} finally {
    foreach ($name in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name,$savedEnvironment[$name],'Process')
    }
    if ($null -ne $operationLock) { $operationLock.Dispose() }
}
