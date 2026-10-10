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
Generate every canonical LUNASHELL device asset.

.DESCRIPTION
Preflights the deterministic boot-art and SFX generators, then commits the
complete canonical asset package as one recoverable operation. If either
generator fails, every pre-existing package file is restored. This script does
not initialize ESP-IDF, build firmware, or access the physical device.
#>

[CmdletBinding()]
param(
    [string]$ConfigPath = (
        Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'assets\sfx\sfx-config.json'
    ),
    [string]$BootSource = (
        Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) `
            'assets\branding\LUNASHELL\source\LUNASHELL-ai-logo.pdf'
    ),
    [string]$BootOutputDirectory = (
        Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) `
            'assets\branding\LUNASHELL\generated'
    ),
    [string]$AssetPython,
    [switch]$NoProgress,
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$script:deviceAssetConfigStream = $null
$script:deviceAssetPackageState = 'unchanged'
$script:preserveDeviceAssetLock = $false
$assetLockEnvironmentName = 'LUNASHELL_ASSET_LOCK_TOKEN'
$assetLockDirectoryName = '.device-asset-generator.lock'
$pathComparison = if ($IsWindows) {
    [StringComparison]::OrdinalIgnoreCase
}
else {
    [StringComparison]::Ordinal
}

function Resolve-PythonCommand {
    param([string]$RequestedCommand)

    if (-not [string]::IsNullOrWhiteSpace($RequestedCommand)) {
        $command = Get-Command $RequestedCommand -CommandType Application `
            -ErrorAction Stop | Select-Object -First 1
        return $command.Source
    }

    foreach ($candidate in @('python', 'python3')) {
        $command = Get-Command $candidate -CommandType Application `
            -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -ne $command) {
            return $command.Source
        }
    }

    throw 'Python was not found. Pass its executable path with -AssetPython.'
}

function Assert-NoReparseTree {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Label
    )

    $rootItem = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    $items = @($rootItem)
    if ($rootItem.PSIsContainer) {
        $items += @(Get-ChildItem -LiteralPath $Path -Recurse -Force)
    }
    $reparsePoint = $items |
        Where-Object {
            ($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0
        } |
        Select-Object -First 1
    if ($null -ne $reparsePoint) {
        throw "$Label contains an unsupported reparse point: $($reparsePoint.FullName)"
    }
}

function Get-Sha256Hex {
    param([Parameter(Mandatory)][byte[]]$Bytes)

    return [Convert]::ToHexString(
        [Security.Cryptography.SHA256]::HashData($Bytes)
    ).ToLowerInvariant()
}

function Open-DeviceAssetConfigSnapshot {
    param([Parameter(Mandatory)][string]$Path)

    $stream = [System.IO.File]::Open(
        $Path,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::Read
    )
    try {
        $memory = [System.IO.MemoryStream]::new()
        try {
            $stream.CopyTo($memory)
            $bytes = $memory.ToArray()
        }
        finally {
            $memory.Dispose()
        }
        $encoding = [System.Text.UTF8Encoding]::new($false, $true)
        $text = $encoding.GetString($bytes)
        if ($text.Length -gt 0 -and $text[0] -eq [char]0xFEFF) {
            $text = $text.Substring(1)
        }
        return [pscustomobject]@{
            Bytes = $bytes
            Sha256 = Get-Sha256Hex -Bytes $bytes
            Config = ($text | ConvertFrom-Json -ErrorAction Stop)
            Stream = $stream
        }
    }
    catch {
        $stream.Dispose()
        throw
    }
}

function Assert-DeviceAssetConfigUnchanged {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$ExpectedSha256,
        [Parameter(Mandatory)][string]$Phase
    )

    $currentSha256 = Get-Sha256Hex -Bytes (
        [System.IO.File]::ReadAllBytes($Path)
    )
    if ($currentSha256 -cne $ExpectedSha256) {
        throw (
            "SFX configuration changed during the device-asset plan ($Phase): " +
            "expected=$ExpectedSha256 actual=$currentSha256 path=$Path"
        )
    }
}

function Read-AssetLockOwner {
    param([Parameter(Mandatory)][string]$OwnerPath)

    $ownerItem = Get-Item -LiteralPath $OwnerPath -Force -ErrorAction Stop
    if ($ownerItem.PSIsContainer -or
        ($ownerItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Asset-package lock owner metadata is not a regular file: $OwnerPath"
    }
    return Get-Content -Raw -LiteralPath $OwnerPath | ConvertFrom-Json -ErrorAction Stop
}

function Enter-AssetPackageLock {
    param(
        [Parameter(Mandatory)][string]$ProjectRoot,
        [Parameter(Mandatory)][string]$Owner
    )

    $assetsRoot = Join-Path $ProjectRoot 'assets'
    $assetsItem = Get-Item -LiteralPath $assetsRoot -Force -ErrorAction Stop
    if (-not $assetsItem.PSIsContainer -or
        ($assetsItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Canonical assets root is missing or is a reparse point: $assetsRoot"
    }
    $lockRoot = Join-Path $assetsRoot $assetLockDirectoryName
    $ownerPath = Join-Path $lockRoot 'owner.json'
    $inheritedToken = [Environment]::GetEnvironmentVariable(
        $assetLockEnvironmentName,
        [EnvironmentVariableTarget]::Process
    )
    if (-not [string]::IsNullOrWhiteSpace($inheritedToken)) {
        $lockItem = Get-Item -LiteralPath $lockRoot -Force -ErrorAction Stop
        if (-not $lockItem.PSIsContainer -or
            ($lockItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Inherited asset-package lock is not a regular directory: $lockRoot"
        }
        $metadata = Read-AssetLockOwner -OwnerPath $ownerPath
        if ([string]$metadata.token -cne $inheritedToken) {
            throw "Inherited asset-package lock token does not match $ownerPath."
        }
        return [pscustomobject]@{
            Root = $lockRoot
            OwnerPath = $ownerPath
            Token = $inheritedToken
            Owned = $false
        }
    }

    try {
        New-Item -ItemType Directory -Path $lockRoot -ErrorAction Stop | Out-Null
    }
    catch {
        throw (
            "Asset-package lock is already present or cannot be acquired at " +
            "$lockRoot. A stale lock is intentionally retained for inspection. " +
            "Details: $($_.Exception.Message)"
        )
    }
    $token = [Guid]::NewGuid().ToString('N')
    try {
        $metadata = [ordered]@{
            schema_version = 1
            token = $token
            pid = $PID
            owner = $Owner
            created_utc = [DateTime]::UtcNow.ToString('O')
        }
        [System.IO.File]::WriteAllText(
            $ownerPath,
            (($metadata | ConvertTo-Json -Depth 3) + [Environment]::NewLine),
            [System.Text.UTF8Encoding]::new($false)
        )
    }
    catch {
        throw (
            "Asset-package lock metadata could not be written; the fail-closed " +
            "lock remains visible at ${lockRoot}: $($_.Exception.Message)"
        )
    }
    return [pscustomobject]@{
        Root = $lockRoot
        OwnerPath = $ownerPath
        Token = $token
        Owned = $true
    }
}

function Exit-AssetPackageLock {
    param([Parameter(Mandatory)][object]$LockState)

    if (-not $LockState.Owned) {
        return
    }
    $metadata = Read-AssetLockOwner -OwnerPath ([string]$LockState.OwnerPath)
    if ([string]$metadata.token -cne [string]$LockState.Token) {
        throw "Asset-package lock ownership changed at $($LockState.Root); lock retained."
    }
    [System.IO.File]::Delete([string]$LockState.OwnerPath)
    [System.IO.Directory]::Delete([string]$LockState.Root, $false)
}

function Get-PackageDestinations {
    param(
        [Parameter(Mandatory)][string]$BootRoot,
        [Parameter(Mandatory)][string]$SfxRoot,
        [Parameter(Mandatory)][object]$Config
    )

    $destinations = [System.Collections.Generic.List[string]]::new()
    foreach ($leaf in @(
        'LUNASHELL-boot-466x466.png',
        'LUNASHELL-boot-466x466.rgb565le.bin',
        'round-466-safe-area-guide.png',
        'boot-assets.manifest.json'
    )) {
        $destinations.Add((Join-Path $BootRoot $leaf))
    }

    $generatedRoot = [System.IO.Path]::GetFullPath(
        (Join-Path $SfxRoot 'generated')
    )
    $generatedPrefix = $generatedRoot.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    ) + [System.IO.Path]::DirectorySeparatorChar
    foreach ($asset in @($Config.assets)) {
        $output = [System.IO.Path]::GetFullPath(
            (Join-Path $SfxRoot ([string]$asset.output))
        )
        if (-not $output.StartsWith($generatedPrefix, $pathComparison)) {
            throw "SFX package output escapes $generatedRoot`: $output"
        }
        $destinations.Add($output)
    }
    $destinations.Add((Join-Path $generatedRoot 'sfx.manifest.json'))

    $unique = [System.Collections.Generic.HashSet[string]]::new(
        $(if ($IsWindows) {
            [StringComparer]::OrdinalIgnoreCase
        }
        else {
            [StringComparer]::Ordinal
        })
    )
    foreach ($destination in $destinations) {
        if (-not $unique.Add($destination)) {
            throw "Duplicate canonical asset destination: $destination"
        }
    }
    return @($destinations)
}

function Backup-PackageFiles {
    param(
        [Parameter(Mandatory)][string[]]$Destinations,
        [Parameter(Mandatory)][string]$BackupRoot
    )

    [System.IO.Directory]::CreateDirectory($BackupRoot) | Out-Null
    $states = [System.Collections.Generic.List[object]]::new()
    for ($index = 0; $index -lt $Destinations.Count; $index++) {
        $destination = $Destinations[$index]
        if ([System.IO.Directory]::Exists($destination)) {
            throw "Canonical asset destination is a directory: $destination"
        }
        $exists = [System.IO.File]::Exists($destination)
        $backup = Join-Path $BackupRoot ('{0:D4}.bak' -f $index)
        $hash = $null
        $attributes = $null
        $lastWriteTimeUtc = $null
        if ($exists) {
            $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $destination).Hash
            $attributes = [System.IO.File]::GetAttributes($destination)
            $lastWriteTimeUtc = [System.IO.File]::GetLastWriteTimeUtc($destination)
            [System.IO.File]::Copy($destination, $backup, $false)
            $backupHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $backup).Hash
            if ($backupHash -cne $hash) {
                throw "Asset backup verification failed: $destination"
            }
        }
        $states.Add([pscustomobject]@{
            Destination = $destination
            Backup = $backup
            Existed = $exists
            Sha256 = $hash
            Attributes = $attributes
            LastWriteTimeUtc = $lastWriteTimeUtc
        })
    }
    return @($states)
}

function Restore-PackageFiles {
    param([Parameter(Mandatory)][object[]]$States)

    $rollbackErrors = [System.Collections.Generic.List[string]]::new()
    foreach ($state in $States) {
        try {
            if ([System.IO.File]::Exists([string]$state.Destination)) {
                [System.IO.File]::SetAttributes(
                    [string]$state.Destination,
                    [System.IO.FileAttributes]::Normal
                )
                [System.IO.File]::Delete([string]$state.Destination)
            }
            if ($state.Existed) {
                if (-not [System.IO.File]::Exists([string]$state.Backup)) {
                    throw "Verified backup is missing: $($state.Backup)"
                }
                [System.IO.File]::Copy(
                    [string]$state.Backup,
                    [string]$state.Destination,
                    $false
                )
                $restoredHash = (
                    Get-FileHash -Algorithm SHA256 `
                        -LiteralPath ([string]$state.Destination)
                ).Hash
                if ($restoredHash -cne [string]$state.Sha256) {
                    throw 'Restored content failed SHA-256 verification.'
                }
                [System.IO.File]::SetLastWriteTimeUtc(
                    [string]$state.Destination,
                    [datetime]$state.LastWriteTimeUtc
                )
                [System.IO.File]::SetAttributes(
                    [string]$state.Destination,
                    [System.IO.FileAttributes]$state.Attributes
                )
            }
        }
        catch {
            $rollbackErrors.Add(
                "$($state.Destination): $($_.Exception.Message)"
            )
        }
    }
    return @($rollbackErrors)
}

$bootGenerator = Join-Path $PSScriptRoot 'generate_boot_assets.py'
$sfxGenerator = Join-Path $PSScriptRoot 'generate-sfx.ps1'
foreach ($requiredScript in @($bootGenerator, $sfxGenerator)) {
    if (-not (Test-Path -LiteralPath $requiredScript -PathType Leaf)) {
        throw "Required asset generator not found: $requiredScript"
    }
}
if (-not (Test-Path -LiteralPath $BootSource -PathType Leaf)) {
    throw "Boot artwork source not found: $BootSource"
}
if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) {
    throw "SFX configuration not found: $ConfigPath"
}

$bootSourceRootPath = Join-Path $projectRoot 'assets\branding\LUNASHELL\source'
$bootGeneratedRootPath = Join-Path $projectRoot 'assets\branding\LUNASHELL\generated'
Assert-NoReparseTree -Path $bootSourceRootPath -Label 'Canonical boot source tree'
Assert-NoReparseTree -Path $bootGeneratedRootPath -Label 'Canonical boot generated tree'
$bootSourceRoot = (
    Resolve-Path -LiteralPath $bootSourceRootPath -ErrorAction Stop
).Path
$bootGeneratedRoot = (
    Resolve-Path -LiteralPath $bootGeneratedRootPath -ErrorAction Stop
).Path
$resolvedBootSource = (Resolve-Path -LiteralPath $BootSource -ErrorAction Stop).Path
$resolvedBootOutput = [System.IO.Path]::GetFullPath($BootOutputDirectory)
$resolvedConfig = (Resolve-Path -LiteralPath $ConfigPath -ErrorAction Stop).Path
$sfxRootPath = Join-Path $projectRoot 'assets\sfx'
Assert-NoReparseTree -Path $sfxRootPath -Label 'Canonical SFX tree'
$sfxRoot = (Resolve-Path -LiteralPath $sfxRootPath -ErrorAction Stop).Path
$sourcePrefix = $bootSourceRoot.TrimEnd(
    [System.IO.Path]::DirectorySeparatorChar,
    [System.IO.Path]::AltDirectorySeparatorChar
) + `
    [System.IO.Path]::DirectorySeparatorChar
if (-not $resolvedBootSource.StartsWith(
    $sourcePrefix,
    $pathComparison
)) {
    throw "Boot artwork must stay below $bootSourceRoot`: $resolvedBootSource"
}
if (-not $resolvedBootOutput.Equals(
    $bootGeneratedRoot,
    $pathComparison
)) {
    throw "Boot output must be the canonical directory $bootGeneratedRoot`: $resolvedBootOutput"
}
$sfxPrefix = $sfxRoot.TrimEnd(
    [System.IO.Path]::DirectorySeparatorChar,
    [System.IO.Path]::AltDirectorySeparatorChar
) + [System.IO.Path]::DirectorySeparatorChar
if (-not $resolvedConfig.StartsWith($sfxPrefix, $pathComparison)) {
    throw "SFX configuration must stay below $sfxRoot`: $resolvedConfig"
}

$pythonCommand = Resolve-PythonCommand -RequestedCommand $AssetPython
$bootArguments = @(
    $bootGenerator,
    '--source', $resolvedBootSource,
    '--output-dir', $resolvedBootOutput
)

$assetLock = $null
$workflowFailure = $null
$cleanupFailures = [System.Collections.Generic.List[string]]::new()
$assetLockRetained = $false
$previousAssetLockToken = [Environment]::GetEnvironmentVariable(
    $assetLockEnvironmentName,
    [EnvironmentVariableTarget]::Process
)
$assetLockEnvironmentChanged = $false
$script:deviceAssetWorkflowCompleted = $false

try {
    $assetLock = Enter-AssetPackageLock -ProjectRoot $projectRoot `
        -Owner 'complete-device-assets'
    [Environment]::SetEnvironmentVariable(
        $assetLockEnvironmentName,
        [string]$assetLock.Token,
        [EnvironmentVariableTarget]::Process
    )
    $assetLockEnvironmentChanged = $true

    & {
    $configSnapshot = Open-DeviceAssetConfigSnapshot -Path $resolvedConfig
    $script:deviceAssetConfigStream = $configSnapshot.Stream
    $configSha256 = [string]$configSnapshot.Sha256
    $packageDestinations = @(
        Get-PackageDestinations -BootRoot $bootGeneratedRoot -SfxRoot $sfxRoot `
            -Config $configSnapshot.Config
    )

    Write-Host 'Validating the complete device-asset plan before canonical writes.'
    & $pythonCommand @bootArguments '--validate-only'
    if ($LASTEXITCODE -ne 0) {
        throw "Boot asset preflight failed (exit code $LASTEXITCODE)."
    }
    & $sfxGenerator -ConfigPath $resolvedConfig -NoProgress -ValidateOnly `
        -ExpectedConfigSha256 $configSha256
    Assert-DeviceAssetConfigUnchanged -Path $resolvedConfig `
        -ExpectedSha256 $configSha256 -Phase 'after preflight'

    if ($ValidateOnly) {
        $script:deviceAssetWorkflowCompleted = $true
        Write-Host (
            "Complete device-asset preflight: PASS (config SHA-256 " +
            "$configSha256; no canonical output writes)."
        )
        return
    }

    $backupRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
        'LUNASHELL-device-assets-backup-' + [Guid]::NewGuid().ToString('N')
    )
    $packageStates = @()
    $backupCompleted = $false
    $packageCommitted = $false
    $rollbackIncomplete = $false
    $primaryFailure = $null

    try {
        Assert-DeviceAssetConfigUnchanged -Path $resolvedConfig `
            -ExpectedSha256 $configSha256 -Phase 'before package snapshot'
        $packageStates = @(
            Backup-PackageFiles -Destinations $packageDestinations `
                -BackupRoot $backupRoot
        )
        $backupCompleted = $true
        $script:deviceAssetPackageState = 'snapshotted'

        Write-Host 'Generating deterministic boot assets.'
        & $pythonCommand @bootArguments
        if ($LASTEXITCODE -ne 0) {
            throw "Boot asset generation failed (exit code $LASTEXITCODE)."
        }

        Write-Host 'Generating deterministic sound assets.'
        & $sfxGenerator -ConfigPath $resolvedConfig -NoProgress:$NoProgress `
            -ExpectedConfigSha256 $configSha256
        Assert-DeviceAssetConfigUnchanged -Path $resolvedConfig `
            -ExpectedSha256 $configSha256 -Phase 'after package commit'
        $packageCommitted = $true
        $script:deviceAssetPackageState = 'committed'
    }
    catch {
        $primaryFailure = $_
        $rollbackErrors = @()
        if ($backupCompleted) {
            $rollbackErrors = @(
                Restore-PackageFiles -States $packageStates
            )
            if ($rollbackErrors.Count -gt 0) {
                $rollbackIncomplete = $true
                $script:deviceAssetPackageState = 'rollback incomplete'
                $script:preserveDeviceAssetLock = $true
            }
            else {
                $script:deviceAssetPackageState = 'restored'
            }
        }
        else {
            $script:deviceAssetPackageState = 'unchanged'
        }

        $failureMessage = (
            "Device-asset generation failed ($($primaryFailure.Exception.Message)); " +
            "package state=$($script:deviceAssetPackageState)"
        )
        if ($rollbackIncomplete) {
            $failureMessage += (
                "; package rollback incomplete: $($rollbackErrors -join '; '); " +
                "verified backups retained at $backupRoot"
            )
        }
        throw [System.InvalidOperationException]::new(
            $failureMessage,
            $primaryFailure.Exception
        )
    }
    finally {
        if (-not $rollbackIncomplete -and
            [System.IO.Directory]::Exists($backupRoot)) {
            try {
                Remove-Item -LiteralPath $backupRoot -Recurse -Force
            }
            catch {
                $cleanupFailures.Add(
                    "backup cleanup failed at ${backupRoot}: $($_.Exception.Message)"
                )
            }
        }
    }

    if (-not $packageCommitted) {
        throw (
            'Device-asset generation ended without a committed package; ' +
            "package state=$($script:deviceAssetPackageState)"
        )
    }
    $script:deviceAssetWorkflowCompleted = $true
    }
}
catch {
    $workflowFailure = $_
}
finally {
    if ($null -ne $script:deviceAssetConfigStream) {
        try {
            $script:deviceAssetConfigStream.Dispose()
        }
        catch {
            $cleanupFailures.Add(
                "config snapshot release failed: $($_.Exception.Message)"
            )
        }
        $script:deviceAssetConfigStream = $null
    }
    if ($assetLockEnvironmentChanged) {
        try {
            if ($null -eq $previousAssetLockToken) {
                Remove-Item -LiteralPath "Env:$assetLockEnvironmentName" `
                    -ErrorAction Stop
            }
            else {
                [Environment]::SetEnvironmentVariable(
                    $assetLockEnvironmentName,
                    $previousAssetLockToken,
                    [EnvironmentVariableTarget]::Process
                )
            }
        }
        catch {
            $cleanupFailures.Add(
                "asset-lock environment restore failed: $($_.Exception.Message)"
            )
        }
    }
    if ($null -ne $assetLock -and $assetLock.Owned) {
        if ($script:preserveDeviceAssetLock) {
            $assetLockRetained = $true
        }
        else {
            try {
                Exit-AssetPackageLock -LockState $assetLock
            }
            catch {
                $cleanupFailures.Add(
                    "asset-package lock cleanup failed at $($assetLock.Root): " +
                    $_.Exception.Message
                )
            }
        }
    }
}

if ($null -ne $workflowFailure) {
    $message = (
        "Device-asset workflow failed: $($workflowFailure.Exception.Message); " +
        "package state=$($script:deviceAssetPackageState)"
    )
    if ($assetLockRetained) {
        $message += "; fail-closed asset-package lock retained at $($assetLock.Root)"
    }
    if ($cleanupFailures.Count -gt 0) {
        $message += "; cleanup failure(s): $($cleanupFailures -join '; ')"
    }
    throw [System.InvalidOperationException]::new(
        $message,
        $workflowFailure.Exception
    )
}
if ($cleanupFailures.Count -gt 0) {
    throw (
        "Device-asset workflow completed with package state=" +
        "$($script:deviceAssetPackageState), but cleanup failed: " +
        ($cleanupFailures -join '; ')
    )
}
if (-not $script:deviceAssetWorkflowCompleted) {
    throw 'Device-asset workflow ended without a completed result.'
}

if (-not $ValidateOnly) {
    Write-Host 'All canonical device assets were committed successfully.'
}
