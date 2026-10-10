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
Generate the canonical LUNASHELL sound effects deterministically.

.DESCRIPTION
Reads assets/sfx/sfx-config.json, validates the complete repository-confined
plan, invokes ffmpeg into a staging directory, verifies every result with
ffprobe and the firmware's fixed 44-byte WAV contract, then commits outputs and
the SHA-256 manifest with rollback. This standalone script never invokes an
ESP-IDF build or a device operation.
#>

[CmdletBinding()]
param(
    [string]$ConfigPath = (
        Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'assets\sfx\sfx-config.json'
    ),
    [switch]$NoProgress,
    [switch]$ValidateOnly,
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$ExpectedConfigSha256
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:preserveSfxStaging = $false
$script:sfxConfigStream = $null
$assetLockEnvironmentName = 'LUNASHELL_ASSET_LOCK_TOKEN'
$assetLockDirectoryName = '.device-asset-generator.lock'

function Format-InvariantDecimal {
    param([Parameter(Mandatory)][double]$Value)

    return $Value.ToString(
        '0.###',
        [Globalization.CultureInfo]::InvariantCulture
    )
}

function Test-FiniteNumber {
    param([Parameter(Mandatory)][double]$Value)

    return -not [double]::IsNaN($Value) -and
        -not [double]::IsInfinity($Value)
}

function Read-LittleEndianUInt16 {
    param(
        [Parameter(Mandatory)][byte[]]$Bytes,
        [Parameter(Mandatory)][int]$Offset
    )

    return [uint16](
        [uint16]$Bytes[$Offset] -bor
        ([uint16]$Bytes[$Offset + 1] -shl 8)
    )
}

function Read-LittleEndianUInt32 {
    param(
        [Parameter(Mandatory)][byte[]]$Bytes,
        [Parameter(Mandatory)][int]$Offset
    )

    return [uint32](
        [uint32]$Bytes[$Offset] -bor
        ([uint32]$Bytes[$Offset + 1] -shl 8) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 16) -bor
        ([uint32]$Bytes[$Offset + 3] -shl 24)
    )
}

function Assert-CanonicalFirmwareWav {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$AssetId
    )

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 44) {
        throw "Generated WAV is shorter than the firmware header for $AssetId."
    }
    $ascii = [System.Text.Encoding]::ASCII
    $riffSize = Read-LittleEndianUInt32 -Bytes $bytes -Offset 4
    $fmtSize = Read-LittleEndianUInt32 -Bytes $bytes -Offset 16
    $audioFormat = Read-LittleEndianUInt16 -Bytes $bytes -Offset 20
    $channels = Read-LittleEndianUInt16 -Bytes $bytes -Offset 22
    $sampleRate = Read-LittleEndianUInt32 -Bytes $bytes -Offset 24
    $byteRate = Read-LittleEndianUInt32 -Bytes $bytes -Offset 28
    $blockAlign = Read-LittleEndianUInt16 -Bytes $bytes -Offset 32
    $bitsPerSample = Read-LittleEndianUInt16 -Bytes $bytes -Offset 34
    $dataSize = Read-LittleEndianUInt32 -Bytes $bytes -Offset 40
    if ($ascii.GetString($bytes, 0, 4) -cne 'RIFF' -or
        $ascii.GetString($bytes, 8, 4) -cne 'WAVE' -or
        $ascii.GetString($bytes, 12, 4) -cne 'fmt ' -or
        $ascii.GetString($bytes, 36, 4) -cne 'data' -or
        $riffSize -ne [uint32]($bytes.Length - 8) -or
        $fmtSize -ne 16U -or $audioFormat -ne 1U -or
        $channels -ne 1U -or $sampleRate -ne 22050U -or
        $byteRate -ne 44100U -or $blockAlign -ne 2U -or
        $bitsPerSample -ne 16U -or
        $dataSize -ne [uint32]($bytes.Length - 44) -or
        ($dataSize % 2U) -ne 0U) {
        throw (
            "Generated WAV violates the firmware's canonical 44-byte " +
            "PCM16/mono/22050 contract for $AssetId."
        )
    }
}

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter(Mandatory)][string[]]$ArgumentList,
        [Parameter(Mandatory)][string]$FailureMessage
    )

    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "$FailureMessage (exit code $LASTEXITCODE)"
    }
}

function Resolve-ToolPath {
    param([Parameter(Mandatory)][string]$Name)

    $command = Get-Command $Name -CommandType Application -ErrorAction Stop |
        Select-Object -First 1
    return $command.Source
}

function Get-ToolVersionLine {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter(Mandatory)][string]$Name
    )

    $versionOutput = @(& $FilePath '-version')
    if ($LASTEXITCODE -ne 0 -or $versionOutput.Count -eq 0) {
        throw "$Name version query failed."
    }
    return [string]$versionOutput[0]
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

function Open-SfxConfigSnapshot {
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
        $config = $text | ConvertFrom-Json -ErrorAction Stop
        return [pscustomobject]@{
            Bytes = $bytes
            Sha256 = Get-Sha256Hex -Bytes $bytes
            Config = $config
            Stream = $stream
        }
    }
    catch {
        $stream.Dispose()
        throw
    }
}

function Assert-SfxConfigUnchanged {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$ExpectedSha256,
        [Parameter(Mandatory)][string]$Phase
    )

    $currentBytes = [System.IO.File]::ReadAllBytes($Path)
    $currentSha256 = Get-Sha256Hex -Bytes $currentBytes
    if ($currentSha256 -cne $ExpectedSha256) {
        throw (
            "SFX configuration changed during the immutable plan ($Phase): " +
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

function Resolve-ChildPath {
    param(
        [Parameter(Mandatory)][string]$Root,
        [Parameter(Mandatory)][string]$RelativePath,
        [Parameter(Mandatory)][string]$Label
    )

    if ([string]::IsNullOrWhiteSpace($RelativePath) -or
        [System.IO.Path]::IsPathRooted($RelativePath)) {
        throw "$Label must be a non-empty path relative to the SFX directory: $RelativePath"
    }

    $rootPrefix = $Root.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    ) + [System.IO.Path]::DirectorySeparatorChar
    $resolved = [System.IO.Path]::GetFullPath((Join-Path $Root $RelativePath))
    if (-not $resolved.StartsWith(
        $rootPrefix,
        [StringComparison]::OrdinalIgnoreCase
    )) {
        throw "$Label escapes the SFX directory: $RelativePath"
    }
    return $resolved
}

function Commit-StagedFiles {
    param(
        [Parameter(Mandatory)][object[]]$Entries,
        [Parameter(Mandatory)][string]$BackupRoot,
        [Parameter(Mandatory)][string]$StagingRoot,
        [scriptblock]$PostCommitValidation
    )

    [System.IO.Directory]::CreateDirectory($BackupRoot) | Out-Null
    $states = [System.Collections.Generic.List[object]]::new()
    try {
        $index = 0
        foreach ($entry in $Entries) {
            if (-not [System.IO.File]::Exists([string]$entry.Staged)) {
                throw "Staged asset is missing: $($entry.Staged)"
            }
            $destinationDirectory = Split-Path -Parent ([string]$entry.Destination)
            [System.IO.Directory]::CreateDirectory($destinationDirectory) |
                Out-Null
            $state = [pscustomobject]@{
                Destination = [string]$entry.Destination
                Backup = Join-Path $BackupRoot ('{0:D4}.bak' -f $index)
                BackupMoved = $false
                NewMoved = $false
            }
            $states.Add($state)
            if ([System.IO.File]::Exists($state.Destination)) {
                # Rename the old path out of the way. This never writes through
                # a pre-existing NTFS hardlink to a preserved source file.
                [System.IO.File]::Move($state.Destination, $state.Backup)
                $state.BackupMoved = $true
            }
            [System.IO.File]::Move(
                [string]$entry.Staged,
                $state.Destination
            )
            $state.NewMoved = $true
            $index++
        }
        if ($null -ne $PostCommitValidation) {
            & $PostCommitValidation
        }
    }
    catch {
        $commitMessage = $_.Exception.Message
        $rollbackErrors = [System.Collections.Generic.List[string]]::new()
        for ($index = $states.Count - 1; $index -ge 0; $index--) {
            $state = $states[$index]
            try {
                if ($state.NewMoved -and
                    [System.IO.File]::Exists($state.Destination)) {
                    [System.IO.File]::Delete($state.Destination)
                }
                if ($state.BackupMoved -and
                    [System.IO.File]::Exists($state.Backup)) {
                    [System.IO.File]::Move($state.Backup, $state.Destination)
                }
            }
            catch {
                $rollbackErrors.Add(
                    "$($state.Destination): $($_.Exception.Message)"
                )
            }
        }
        if ($rollbackErrors.Count -gt 0) {
            $script:preserveSfxStaging = $true
            throw (
                "SFX commit failed ($commitMessage); rollback incomplete: " +
                "$($rollbackErrors -join '; '). Recovery files retained at " +
                $StagingRoot
            )
        }
        throw "SFX commit failed; previous canonical files restored: $commitMessage"
    }
}

$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$assetLock = $null
$workflowFailure = $null
$assetLockRetained = $false
$cleanupFailures = [System.Collections.Generic.List[string]]::new()
$previousAssetLockToken = [Environment]::GetEnvironmentVariable(
    $assetLockEnvironmentName,
    [EnvironmentVariableTarget]::Process
)
$assetLockEnvironmentChanged = $false

try {
    $assetLock = Enter-AssetPackageLock -ProjectRoot $projectRoot -Owner 'sfx'
    [Environment]::SetEnvironmentVariable(
        $assetLockEnvironmentName,
        [string]$assetLock.Token,
        [EnvironmentVariableTarget]::Process
    )
    $assetLockEnvironmentChanged = $true

    & {
$allowedSfxRootPath = Join-Path $projectRoot 'assets\sfx'
Assert-NoReparseTree -Path $allowedSfxRootPath -Label 'Canonical SFX tree'
$pathComparison = if ($IsWindows) {
    [StringComparison]::OrdinalIgnoreCase
}
else {
    [StringComparison]::Ordinal
}
$pathStringComparer = if ($IsWindows) {
    [StringComparer]::OrdinalIgnoreCase
}
else {
    [StringComparer]::Ordinal
}
$sfxRoot = (Resolve-Path -LiteralPath $allowedSfxRootPath -ErrorAction Stop).Path
$sfxPrefix = $sfxRoot.TrimEnd(
    [System.IO.Path]::DirectorySeparatorChar,
    [System.IO.Path]::AltDirectorySeparatorChar
) + [System.IO.Path]::DirectorySeparatorChar
$resolvedConfig = (Resolve-Path -LiteralPath $ConfigPath -ErrorAction Stop).Path
if (-not $resolvedConfig.StartsWith(
    $sfxPrefix,
    $pathComparison
)) {
    throw "SFX configuration must stay below $sfxRoot`: $resolvedConfig"
}

$configSnapshot = Open-SfxConfigSnapshot -Path $resolvedConfig
$script:sfxConfigStream = $configSnapshot.Stream
$configSha256 = [string]$configSnapshot.Sha256
if (-not [string]::IsNullOrWhiteSpace($ExpectedConfigSha256) -and
    $configSha256 -cne $ExpectedConfigSha256.ToLowerInvariant()) {
    throw (
        'SFX configuration does not match the aggregator plan: ' +
        "expected=$($ExpectedConfigSha256.ToLowerInvariant()) " +
        "actual=$configSha256 path=$resolvedConfig"
    )
}
$config = $configSnapshot.Config
if ([int]$config.schema_version -ne 1 -or
    $null -eq $config.target -or
    $null -eq $config.normalization) {
    throw 'Invalid SFX configuration: schema_version 1, target, and normalization are required.'
}
$assets = @($config.assets)
if ($assets.Count -eq 0) {
    throw 'Invalid SFX configuration: at least one asset is required.'
}

try {
    $targetSampleRate = [int]$config.target.sample_rate_hz
    $targetChannels = [int]$config.target.channels
    $integratedLufs = [double]$config.normalization.integrated_lufs
    $truePeakDb = [double]$config.normalization.true_peak_db
    $loudnessRange = [double]$config.normalization.loudness_range
}
catch {
    throw "Invalid numeric SFX target or normalization value: $($_.Exception.Message)"
}
$targetCodec = [string]$config.target.codec
$targetContainer = [string]$config.target.container
if ($targetSampleRate -ne 22050 -or $targetChannels -ne 1 -or
    $targetCodec -cne 'pcm_s16le' -or $targetContainer -cne 'wav') {
    throw 'Invalid SFX target: firmware requires wav/pcm_s16le/22050 Hz/mono.'
}
if (-not (Test-FiniteNumber $integratedLufs) -or
    -not (Test-FiniteNumber $truePeakDb) -or
    -not (Test-FiniteNumber $loudnessRange) -or
    $integratedLufs -lt -70.0 -or $integratedLufs -gt -5.0 -or
    $truePeakDb -lt -9.0 -or $truePeakDb -gt 0.0 -or
    $loudnessRange -lt 1.0 -or $loudnessRange -gt 50.0) {
    throw 'Invalid SFX normalization; values must be finite and within loudnorm ranges.'
}

$sourceRoot = (Resolve-Path -LiteralPath (
    Join-Path $sfxRoot 'source'
) -ErrorAction Stop).Path
$generatedRoot = (Resolve-Path -LiteralPath (
    Join-Path $sfxRoot 'generated'
) -ErrorAction Stop).Path
$sourcePrefix = $sourceRoot.TrimEnd(
    [System.IO.Path]::DirectorySeparatorChar,
    [System.IO.Path]::AltDirectorySeparatorChar
) + [System.IO.Path]::DirectorySeparatorChar
$generatedPrefix = $generatedRoot.TrimEnd(
    [System.IO.Path]::DirectorySeparatorChar,
    [System.IO.Path]::AltDirectorySeparatorChar
) + [System.IO.Path]::DirectorySeparatorChar
$manifestPath = Join-Path $generatedRoot 'sfx.manifest.json'
$ffmpeg = Resolve-ToolPath -Name 'ffmpeg'
$ffprobe = Resolve-ToolPath -Name 'ffprobe'
$ffmpegVersion = Get-ToolVersionLine -FilePath $ffmpeg -Name 'ffmpeg'
$ffprobeVersion = Get-ToolVersionLine -FilePath $ffprobe -Name 'ffprobe'
$assetIds = [System.Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase
)
$assetOutputs = [System.Collections.Generic.HashSet[string]]::new(
    $pathStringComparer
)
$preparedAssets = [System.Collections.Generic.List[object]]::new()

# Validate the complete plan before any canonical output can be written.
foreach ($asset in $assets) {
    $assetId = [string]$asset.id
    $purpose = [string]$asset.purpose
    if ([string]::IsNullOrWhiteSpace($assetId) -or
        $assetId -match '[\x00-\x1F\x7F]') {
        throw 'Invalid SFX configuration: every asset requires an id.'
    }
    if ([string]::IsNullOrWhiteSpace($purpose)) {
        throw "Invalid SFX configuration: $assetId requires a purpose."
    }
    if (-not $assetIds.Add($assetId)) {
        throw "Duplicate SFX asset id: $assetId"
    }

    $source = Resolve-ChildPath -Root $sfxRoot `
        -RelativePath ([string]$asset.source) -Label "Source for $assetId"
    $output = Resolve-ChildPath -Root $sfxRoot `
        -RelativePath ([string]$asset.output) -Label "Output for $assetId"
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Missing SFX source for ${assetId}: $source"
    }
    $source = (Resolve-Path -LiteralPath $source -ErrorAction Stop).Path
    if (-not $source.StartsWith(
        $sourcePrefix,
        $pathComparison
    )) {
        throw "SFX source must stay below $sourceRoot`: $source"
    }
    if (-not $output.StartsWith(
        $generatedPrefix,
        $pathComparison
    )) {
        throw "SFX output must stay below $generatedRoot`: $output"
    }
    $outputLeaf = [System.IO.Path]::GetFileName($output)
    if (-not (Split-Path -Parent $output).Equals(
        $generatedRoot,
        $pathComparison
    ) -or [System.IO.Path]::GetExtension($output) -cne '.wav' -or
        $outputLeaf -cne $outputLeaf.ToLowerInvariant()) {
        throw "SFX output must be a lowercase .wav directly below $generatedRoot`: $output"
    }
    if ($output.Equals($manifestPath, $pathComparison)) {
        throw "SFX output collides with the canonical manifest: $output"
    }
    if (-not $assetOutputs.Add($output)) {
        throw "Duplicate SFX output path: $output"
    }

    try {
        $duration = [double]$asset.duration_s
        $trimStart = [double]$asset.trim_start_s
        $fadeIn = [double]$asset.fade_in_s
        $fadeOut = [double]$asset.fade_out_s
    }
    catch {
        throw "Invalid numeric duration, trim, or fade value for ${assetId}: $($_.Exception.Message)"
    }
    if (-not (Test-FiniteNumber $duration) -or
        -not (Test-FiniteNumber $trimStart) -or
        -not (Test-FiniteNumber $fadeIn) -or
        -not (Test-FiniteNumber $fadeOut) -or
        $duration -le 0 -or $duration -gt 3.0 -or $trimStart -lt 0 -or
        $fadeIn -lt 0 -or $fadeOut -lt 0) {
        throw "Invalid duration, trim, or fade value for $assetId."
    }
    if ($fadeIn -gt $duration -or $fadeOut -gt $duration) {
        throw "A fade exceeds duration_s for $assetId."
    }

    $sourceProbeArguments = @(
        '-v', 'error',
        '-show_entries', 'format=duration:stream=codec_type',
        '-of', 'json',
        $source
    )
    $sourceProbeJson = & $ffprobe @sourceProbeArguments
    if ($LASTEXITCODE -ne 0) {
        throw "ffprobe could not decode the source for $assetId (exit code $LASTEXITCODE)"
    }
    $sourceProbe = $sourceProbeJson | ConvertFrom-Json
    $sourceAudioStream = @($sourceProbe.streams) |
        Where-Object { [string]$_.codec_type -eq 'audio' } |
        Select-Object -First 1
    $sourceDuration = [double]$sourceProbe.format.duration
    if ($null -eq $sourceAudioStream -or
        -not (Test-FiniteNumber $sourceDuration) -or
        $sourceDuration -le 0 -or
        ($trimStart + $duration) -gt ($sourceDuration + 0.005)) {
        throw "SFX source duration/stream cannot satisfy the configured window for $assetId."
    }

    $preparedAssets.Add([pscustomobject]@{
        Asset = $asset
        Id = $assetId
        Purpose = $purpose
        SourceRelative = [string]$asset.source
        OutputRelative = [string]$asset.output
        Source = $source
        Output = $output
        Duration = $duration
        TrimStart = $trimStart
        FadeIn = $fadeIn
        FadeOut = $fadeOut
    })
}

if ($ValidateOnly) {
    Assert-SfxConfigUnchanged -Path $resolvedConfig `
        -ExpectedSha256 $configSha256 -Phase 'after preflight'
    Write-Host (
        "SFX preflight: PASS ($($preparedAssets.Count) asset(s), " +
        "config SHA-256 $configSha256, repository-confined paths, " +
        'finite normalization, ffmpeg/ffprobe ready)'
    )
    return
}

$stagingRoot = Join-Path $generatedRoot (
    '.sfx-staging-' + [Guid]::NewGuid().ToString('N')
)
$backupRoot = Join-Path $stagingRoot 'backup'
$generated = [System.Collections.Generic.List[object]]::new()
$commitEntries = [System.Collections.Generic.List[object]]::new()
$assetIndex = 0
$primaryFailure = $null
$commitCompleted = $false

try {
    [System.IO.Directory]::CreateDirectory($stagingRoot) | Out-Null
    foreach ($prepared in $preparedAssets) {
        $assetIndex++
        $asset = $prepared.Asset
        $assetId = $prepared.Id

        if (-not $NoProgress) {
            $percent = [Math]::Floor(($assetIndex / $assets.Count) * 100)
            Write-Progress -Id 20 -Activity 'Generating LUNASHELL SFX' `
                -Status "$assetIndex of $($assets.Count): $assetId" `
                -PercentComplete $percent
        }

        $source = $prepared.Source
        $output = $prepared.Output
        $duration = $prepared.Duration
        $trimStart = $prepared.TrimStart
        $fadeIn = $prepared.FadeIn
        $fadeOut = $prepared.FadeOut
        $stagedOutput = Join-Path $stagingRoot (
            'asset-{0:D4}.wav' -f $assetIndex
        )
        $fadeOutStart = $duration - $fadeOut
        $filter = @(
            (
                'atrim=start={0}:duration={1}' -f
                    (Format-InvariantDecimal $trimStart),
                    (Format-InvariantDecimal $duration)
            )
            'asetpts=PTS-STARTPTS'
            (
                'loudnorm=I={0}:TP={1}:LRA={2}' -f
                    (Format-InvariantDecimal $integratedLufs),
                    (Format-InvariantDecimal $truePeakDb),
                    (Format-InvariantDecimal $loudnessRange)
            )
            (
                'afade=t=in:st=0:d={0}' -f
                    (Format-InvariantDecimal $fadeIn)
            )
            (
                'afade=t=out:st={0}:d={1}' -f
                    (Format-InvariantDecimal $fadeOutStart),
                    (Format-InvariantDecimal $fadeOut)
            )
        ) -join ','

        $ffmpegArguments = @(
            '-hide_banner',
            '-loglevel', 'error',
            '-nostdin',
            '-y',
            '-i', $source,
            '-map', '0:a:0',
            '-vn', '-sn', '-dn',
            '-af', $filter,
            '-ar', [string]$targetSampleRate,
            '-ac', [string]$targetChannels,
            '-c:a', $targetCodec,
            '-map_metadata', '-1',
            '-fflags', '+bitexact',
            '-flags:a', '+bitexact',
            '-f', 'wav',
            $stagedOutput
        )
        Invoke-NativeCommand -FilePath $ffmpeg -ArgumentList $ffmpegArguments `
            -FailureMessage "ffmpeg failed for $assetId"

        $probeArguments = @(
            '-v', 'error',
            '-show_entries',
            'format=format_name,duration,size:stream=codec_name,sample_rate,channels,bits_per_sample',
            '-of', 'json',
            $stagedOutput
        )
        $probeJson = & $ffprobe @probeArguments
        if ($LASTEXITCODE -ne 0) {
            throw "ffprobe failed for $assetId (exit code $LASTEXITCODE)"
        }
        $probe = $probeJson | ConvertFrom-Json
        $stream = @($probe.streams)[0]
        if ($null -eq $stream -or $null -eq $probe.format) {
            throw "ffprobe returned incomplete audio metadata for $assetId."
        }
        if ([int]$stream.sample_rate -ne $targetSampleRate -or
            [int]$stream.channels -ne $targetChannels -or
            [string]$stream.codec_name -ne $targetCodec) {
            throw "Generated SFX format does not match the target for $assetId."
        }
        $observedContainers = @(
            ([string]$probe.format.format_name).Split(',')
        )
        if ($targetContainer -notin $observedContainers) {
            throw "Generated SFX container does not match the target for $assetId."
        }
        $observedDuration = [double]$probe.format.duration
        if (-not (Test-FiniteNumber $observedDuration)) {
            throw "ffprobe returned a non-finite duration for $assetId."
        }
        $durationTolerance = [Math]::Max(
            0.005,
            2.0 / [double]$targetSampleRate
        )
        if ([Math]::Abs($observedDuration - $duration) -gt $durationTolerance) {
            throw (
                "Generated SFX duration does not match $assetId`: " +
                "expected=$duration observed=$observedDuration"
            )
        }
        Assert-CanonicalFirmwareWav -Path $stagedOutput -AssetId $assetId

        $generated.Add([ordered]@{
            id = $assetId
            purpose = $prepared.Purpose
            source = $prepared.SourceRelative
            source_sha256 = (
                Get-FileHash -Algorithm SHA256 -LiteralPath $source
            ).Hash.ToLowerInvariant()
            output = $prepared.OutputRelative
            output_sha256 = (
                Get-FileHash -Algorithm SHA256 -LiteralPath $stagedOutput
            ).Hash.ToLowerInvariant()
            codec = [string]$stream.codec_name
            sample_rate_hz = [int]$stream.sample_rate
            channels = [int]$stream.channels
            bits_per_sample = [int]$stream.bits_per_sample
            duration_s = [Math]::Round($observedDuration, 6)
            bytes = [long]$probe.format.size
        })
        $commitEntries.Add([pscustomobject]@{
            Staged = $stagedOutput
            Destination = $output
        })
    }

    $manifest = [ordered]@{
        schema_version = 1
        generator = [ordered]@{
            powershell = $PSVersionTable.PSVersion.ToString()
            ffmpeg = $ffmpegVersion
            ffprobe = $ffprobeVersion
        }
        target = $config.target
        normalization = $config.normalization
        generated_assets = @($generated)
    }
    $stagedManifest = Join-Path $stagingRoot 'sfx.manifest.json'
    $manifestJson = $manifest | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText(
        $stagedManifest,
        $manifestJson + [Environment]::NewLine,
        [System.Text.UTF8Encoding]::new($false)
    )
    # The manifest is deliberately committed after every referenced output.
    $commitEntries.Add([pscustomobject]@{
        Staged = $stagedManifest
        Destination = $manifestPath
    })

    # Recheck the trust tree immediately before the first canonical mutation.
    Assert-NoReparseTree -Path $generatedRoot -Label 'Canonical generated SFX tree'
    Assert-SfxConfigUnchanged -Path $resolvedConfig `
        -ExpectedSha256 $configSha256 -Phase 'before commit'
    Commit-StagedFiles -Entries @($commitEntries) -BackupRoot $backupRoot `
        -StagingRoot $stagingRoot -PostCommitValidation {
            Assert-SfxConfigUnchanged -Path $resolvedConfig `
                -ExpectedSha256 $configSha256 -Phase 'after commit'
        }
    $commitCompleted = $true
}
catch {
    $primaryFailure = $_
    throw
}
finally {
    try {
        if (-not $NoProgress) {
            Write-Progress -Id 20 -Activity 'Generating LUNASHELL SFX' -Completed
        }
        if (-not $script:preserveSfxStaging -and
            (Test-Path -LiteralPath $stagingRoot)) {
            Remove-Item -LiteralPath $stagingRoot -Recurse -Force
        }
    }
    catch {
        $cleanupMessage = $_.Exception.Message
        if ($null -ne $primaryFailure) {
            throw (
                "SFX generation failed ($($primaryFailure.Exception.Message)); " +
                "staging cleanup also failed at ${stagingRoot}: $cleanupMessage"
            )
        }
        if ($commitCompleted) {
            throw (
                "SFX commit completed, but staging cleanup failed at " +
                "${stagingRoot}: $cleanupMessage"
            )
        }
        throw
    }
}

Write-Host "Generated and committed $($generated.Count) device SFX asset(s)."
Write-Host "Manifest: $manifestPath"
    }
}
catch {
    $workflowFailure = $_
}
finally {
    if ($null -ne $script:sfxConfigStream) {
        try {
            $script:sfxConfigStream.Dispose()
        }
        catch {
            $cleanupFailures.Add(
                "config snapshot release failed: $($_.Exception.Message)"
            )
        }
        $script:sfxConfigStream = $null
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
        if ($script:preserveSfxStaging) {
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
    $message = "SFX workflow failed: $($workflowFailure.Exception.Message)"
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
    throw "SFX workflow completed, but cleanup failed: $($cleanupFailures -join '; ')"
}
