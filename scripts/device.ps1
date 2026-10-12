# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested VibeWare branding.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

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
        'build','clean','fullclean','flash','reset','monitor','capture','sd-log','erase',
        'reconfigure','set-target','update-lock','size','combo','ports','discovery','port-status','recover')]
    [string]$Action = 'menu',
    [string]$Port,
    [ValidateRange(1,4000000)][int]$Baud = 115200,
    [string]$BuildDir,
    [ValidateSet('full','connected','ui-minimal','display-touch','headless','leg-edition','cody-companion')]
    [string]$Profile = 'cody-companion',
    [ValidateSet('Debug','Release')]
    [string]$BuildMode = 'Debug',
    [ValidateSet('none','distance','wake-word','tts-sd','tts-flash')][string]$Feature = 'none',
    [switch]$ActivityClassifier,
    [switch]$ActivityCapture,
    [string]$BoardConfig,
    [ValidateRange(1,86400)][int]$WaitSeconds = 30,
    [ValidateRange(1,86400)][int]$DurationSeconds = 30,
    [string]$OutputPath,
    # Select the current or previous persisted SD log for the host-only USB download.
    [ValidateSet('current','previous')][string]$StoredLog = 'current',
    [ValidateSet('default','text','csv','json2','tree','raw')][string]$SizeOutputFormat = 'default',
    [string]$SizeOutputFile,
    [string]$LogPath,
    # Opt in to ESP-IDF resetting the device when monitor/combo attaches the monitor.
    [switch]$ResetBeforeMonitor,
    [switch]$ResetBeforeCapture,
    [string]$IdfPath,
    [string]$ToolsPath,
    [string]$PythonExe,
    [ValidateRange(0,8)][int]$Jobs = 0,
    [ValidateSet('rig','greeting','boot-sleep','boot-waking','boot-ready','boot-vibeware')][string]$Screen = 'rig',
    [ValidateSet(0,90,180,270)][int]$Rotation = 0,
    [ValidateRange(0,2147483647)][int]$AtMs = 1000,
    [switch]$SafeGuide,
    [switch]$CloudMaterialStudy,
    [switch]$CloudPngMeshStudy,
    [switch]$CloudJellyStudy,
    [switch]$MeshOverlay,
    [switch]$Ccp,
    [switch]$ActivityPreview,
    [string]$PythonExecutable,
    [string]$Skin,
    [string]$Check = 'cody-alpha',
    [string]$Generator = 'cody-rig',
    [string[]]$ToolArguments = @()
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'common/Errors.ps1')

$operationLock = $null
$resolvedLog = $null
$operationFailure = $null
$failureStage = 'loading command helpers'
$dispatch = @{}
$savedEnvironment = @{}
$cleanupMessages = @()
try {
    . (Join-Path $PSScriptRoot 'common/Artifacts.ps1')
    . (Join-Path $PSScriptRoot 'common/Commands.ps1')
    . (Join-Path $PSScriptRoot 'common/Console.ps1')
    . (Join-Path $PSScriptRoot 'common/Help.ps1')
    . (Join-Path $PSScriptRoot 'common/SerialPorts.ps1')

    $failureStage = 'validating command options'
    $contract = Get-DeviceActionParameters
    Assert-DeviceActionParameters -RequestedAction $Action -BoundParameters $PSBoundParameters -Contract $contract
    if ($Action -in @('help','list')) {
        $failureStage = 'showing command help'
        Show-DeviceHelp
    } elseif ($Action -eq 'menu') {
        $failureStage = 'running interactive menu'
        . (Join-Path $PSScriptRoot 'commands/menu.ps1')
        Show-DeviceMenu -InitialSettings $PSBoundParameters
    } else {
        # Resolve every public output before cleanup, toolchain initialization or device I/O.
        $failureStage = 'resolving output paths'
        foreach ($key in $PSBoundParameters.Keys) {
            $dispatch[$key] = $PSBoundParameters[$key]
        }
        $dispatch.Action = $Action
        $resolvedLog = Resolve-ArtifactPath $LogPath "artifacts/logs/$Action.log"
        if ($BuildDir) {
            $dispatch.BuildDir = Resolve-ArtifactPath $BuildDir ''
        }
        if ($OutputPath -or $Action -in @('capture','sd-log')) {
            $defaultOutput = if ($Action -eq 'sd-log') {
                $dispatch.StoredLog = $StoredLog.ToLowerInvariant()
                "artifacts/serial/sd-log-$($dispatch.StoredLog).log"
            } else {
                'artifacts/serial/capture.log'
            }
            $dispatch.OutputPath = Resolve-ArtifactPath $OutputPath $defaultOutput
            if ($dispatch.OutputPath -eq $resolvedLog) {
                throw 'OutputPath and LogPath must be different files.'
            }
        }
        if ($SizeOutputFile -or $Action -eq 'size') {
            $extension = if ($SizeOutputFormat -in @('json2','raw')) {
                'json'
            } elseif ($SizeOutputFormat -eq 'csv') {
                'csv'
            } else {
                'txt'
            }
            $dispatch.SizeOutputFile = Resolve-ArtifactPath $SizeOutputFile "artifacts/reports/size.$extension"
            if ($dispatch.SizeOutputFile -eq $resolvedLog) {
                throw 'SizeOutputFile and LogPath must be different files.'
            }
        }
        if ($Action -in @('check','generate')) {
            $selectedTool = if ($Action -eq 'check') {
                Find-DeviceTool 'checks' $Check
            } else {
                Find-DeviceTool 'generators' $Generator
            }
            if ($PythonExecutable -and $selectedTool.Extension -ne '.py') {
                throw '-PythonExecutable selects Python tools only. Pass PowerShell tool options through -ToolArguments.'
            }
        }

        $failureStage = 'selecting serial port'
        if ($Action -in @('flash','reset','monitor','capture','sd-log','erase','combo','port-status','recover')) {
            if ($PSBoundParameters.ContainsKey('Port')) {
                $dispatch.Port = Normalize-PortName -RequestedPort $Port
            } else {
                $dispatch.Port = Select-SerialPort -RequestedAction $Action
            }
        }

        # Serialize output writers too: a build must not remove an active simulator/check log.
        $failureStage = 'acquiring artifact operation lock'
        $operationLock = Open-DeviceArtifactLock
        if ($Action -in @('build','flash','combo','recover','clean-artifacts')) {
            $failureStage = 'cleaning transient artifacts'
            $protected = @()
            if ($dispatch.ContainsKey('BuildDir')) {
                $protected += $dispatch.BuildDir
            }
            $cleanupMessages = @(Clear-DeviceArtifacts -ProtectedPaths $protected 6>&1)
        }
        $failureStage = 'preparing command outputs'
        foreach ($destination in @($resolvedLog,$dispatch.OutputPath,$dispatch.SizeOutputFile)) {
            if ($destination) {
                $null = New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force
            }
        }
        $temporaryRoot = Resolve-ArtifactPath '' 'artifacts/tmp'
        $null = New-Item -ItemType Directory -Path $temporaryRoot -Force
        $failureStage = 'preparing host environment'
        $environment = @{
            PYTHONDONTWRITEBYTECODE = '1'; PYTHONUTF8 = '1'; PYTHONIOENCODING = 'utf-8'
            TEMP = $temporaryRoot; TMP = $temporaryRoot
            PIP_CACHE_DIR = (Join-Path $ProjectRoot 'artifacts/python/pip-cache')
        }
        foreach ($name in $environment.Keys) {
            $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name,'Process')
            [Environment]::SetEnvironmentVariable($name,$environment[$name],'Process')
        }
        $failureStage = 'running command'
        Push-Location $ProjectRoot
        try {
            # One logger owns the complete operation; internal commands never relaunch it.
            & {
                foreach ($message in $cleanupMessages) {
                    Write-Output ([string]$message)
                }
                Invoke-DeviceCommand -RequestedAction $Action -Options $dispatch
            } *>&1 |
                Tee-Object -FilePath $resolvedLog
        } finally {
            Pop-Location
        }
        Write-Host "Log: $resolvedLog"
    }
} catch {
    $operationFailure = $_
    $loggingUnavailable = ''
    if ($null -eq $operationLock -and $failureStage -ne 'acquiring artifact operation lock') {
        try {
            # Preflight can fail before the command logger. Acquire ownership solely for diagnostics.
            $operationLock = Open-DeviceArtifactLock
        } catch {
            $loggingUnavailable = $_.Exception.Message
        }
    }
    $logConflictsWithOutput = $null -ne $dispatch -and (
        ($dispatch.OutputPath -and $dispatch.OutputPath -eq $resolvedLog) -or
        ($dispatch.SizeOutputFile -and $dispatch.SizeOutputFile -eq $resolvedLog))
    if ($null -ne $operationLock -and ([string]::IsNullOrWhiteSpace($resolvedLog) -or $logConflictsWithOutput)) {
        try {
            # A rejected or conflicting LogPath must not become an unsafe destination or change an output file.
            $failureLogName = "$Action-failure-$([Guid]::NewGuid().ToString('N')).log"
            $resolvedLog = Resolve-ArtifactPath '' "artifacts/logs/$failureLogName"
        } catch {
            $loggingUnavailable = $_.Exception.Message
        }
    }
    if ($failureStage -eq 'acquiring artifact operation lock') {
        $loggingUnavailable = 'The artifact operation lock was not acquired; another operation log will not be changed.'
    }
    $failureOptions = @{
        FailureRecord = $operationFailure
        Operation = $Action
        Stage = $failureStage
        LogPath = $resolvedLog
        LogDetails = ($null -ne $operationLock -and -not [string]::IsNullOrWhiteSpace($resolvedLog))
        LogUnavailableReason = $loggingUnavailable
    }
    Write-DeviceFailure @failureOptions
} finally {
    foreach ($name in $savedEnvironment.Keys) {
        try {
            [Environment]::SetEnvironmentVariable($name,$savedEnvironment[$name],'Process')
        } catch {
            $cleanupFailure = $_
            if ($null -eq $operationFailure) {
                $operationFailure = $cleanupFailure
            }
            Write-DeviceFailure -FailureRecord $cleanupFailure -Operation $Action `
                -Stage "restoring environment variable $name" -LogPath $resolvedLog `
                -LogDetails:($null -ne $operationLock -and -not [string]::IsNullOrWhiteSpace($resolvedLog))
        }
    }
    if ($null -ne $operationLock) {
        try {
            $operationLock.Dispose()
        } catch {
            $cleanupFailure = $_
            if ($null -eq $operationFailure) {
                $operationFailure = $cleanupFailure
            }
            Write-DeviceFailure -FailureRecord $cleanupFailure -Operation $Action `
                -Stage 'releasing artifact operation lock' `
                -LogUnavailableReason 'The artifact lock could not be released; diagnostic log ownership is uncertain.'
        }
    }
}

if ($null -ne $operationFailure) {
    exit 1
}
exit 0
