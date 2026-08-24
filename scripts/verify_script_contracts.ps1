<#
.SYNOPSIS
Verify maintenance-script parsing and dispatch contracts without executing them.

.DESCRIPTION
Uses the PowerShell abstract syntax tree to check that every maintenance
entrypoint parses, asset/banner scripts remain standalone, and the serial
alias forwards only parameters accepted by device.ps1. It performs no build,
asset generation, serial discovery, capture, or other device operation.
#>

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Read-ScriptAst {
    param([Parameter(Mandatory)][string]$Path)

    $tokens = $null
    $parseErrors = $null
    $resolvedPath = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
    $ast = [System.Management.Automation.Language.Parser]::ParseFile(
        $resolvedPath,
        [ref]$tokens,
        [ref]$parseErrors
    )
    if ($parseErrors.Count -gt 0) {
        $messages = $parseErrors | ForEach-Object {
            "$($_.Extent.StartLineNumber):$($_.Extent.StartColumnNumber) $($_.Message)"
        }
        throw "PowerShell parse failed for ${resolvedPath}: $($messages -join '; ')"
    }

    return $ast
}

function Get-ParameterNames {
    param(
        [Parameter(Mandatory)]
        [System.Management.Automation.Language.ScriptBlockAst]$Ast
    )

    return @(
        $Ast.ParamBlock.Parameters |
            ForEach-Object { $_.Name.VariablePath.UserPath }
    )
}

function Assert-Contains {
    param(
        [Parameter(Mandatory)][object[]]$Values,
        [Parameter(Mandatory)][object]$Expected,
        [Parameter(Mandatory)][string]$Message
    )

    if ($Expected -notin $Values) {
        throw $Message
    }
}

function Assert-TextContains {
    param(
        [Parameter(Mandatory)][string]$Text,
        [Parameter(Mandatory)][string]$Expected,
        [Parameter(Mandatory)][string]$Message
    )

    if (-not $Text.Contains($Expected, [StringComparison]::Ordinal)) {
        throw $Message
    }
}

function Assert-TextOrder {
    param(
        [Parameter(Mandatory)][string]$Text,
        [Parameter(Mandatory)][string]$Earlier,
        [Parameter(Mandatory)][string]$Later,
        [Parameter(Mandatory)][string]$Message
    )

    $earlierIndex = $Text.IndexOf($Earlier, [StringComparison]::Ordinal)
    $laterIndex = $Text.IndexOf($Later, [StringComparison]::Ordinal)
    if ($earlierIndex -lt 0 -or $laterIndex -lt 0 -or
        $earlierIndex -ge $laterIndex) {
        throw $Message
    }
}

function Read-UInt16LittleEndian {
    param(
        [Parameter(Mandatory)][byte[]]$Bytes,
        [Parameter(Mandatory)][int]$Offset
    )

    return [uint16]([uint32]$Bytes[$Offset] +
        ([uint32]$Bytes[$Offset + 1] * 256))
}

function Read-UInt32LittleEndian {
    param(
        [Parameter(Mandatory)][byte[]]$Bytes,
        [Parameter(Mandatory)][int]$Offset
    )

    return [uint32]([uint64]$Bytes[$Offset] +
        ([uint64]$Bytes[$Offset + 1] * 256) +
        ([uint64]$Bytes[$Offset + 2] * 65536) +
        ([uint64]$Bytes[$Offset + 3] * 16777216))
}

$scriptsRoot = $PSScriptRoot
$paths = [ordered]@{
    Device = Join-Path $scriptsRoot 'device.ps1'
    Capture = Join-Path $scriptsRoot 'capture-serial.ps1'
    Assets = Join-Path $scriptsRoot 'generate-device-assets.ps1'
    Sfx = Join-Path $scriptsRoot 'generate-sfx.ps1'
    Banner = Join-Path $scriptsRoot 'add-source-banner.ps1'
    SimulatorBuild = Join-Path $scriptsRoot '..\simulator\windows\build-exe.ps1'
}
$asts = @{}
foreach ($entry in $paths.GetEnumerator()) {
    $asts[$entry.Key] = Read-ScriptAst -Path $entry.Value
}

$deviceParameters = @(Get-ParameterNames -Ast $asts.Device)
$actionParameter = $asts.Device.ParamBlock.Parameters |
    Where-Object { $_.Name.VariablePath.UserPath -eq 'Action' } |
    Select-Object -First 1
if ($null -eq $actionParameter) {
    throw 'device.ps1 does not declare an Action parameter.'
}
$validateSet = $actionParameter.Attributes |
    Where-Object {
        $_ -is [System.Management.Automation.Language.AttributeAst] -and
        $_.TypeName.Name -eq 'ValidateSet'
    } |
    Select-Object -First 1
if ($null -eq $validateSet) {
    throw 'device.ps1 Action does not declare a ValidateSet contract.'
}
$deviceActions = @(
    $validateSet.PositionalArguments | ForEach-Object { $_.SafeGetValue() }
)
Assert-Contains -Values $deviceActions -Expected 'capture' `
    -Message 'device.ps1 no longer exposes the capture action.'

foreach ($standaloneKey in @('Assets', 'Sfx', 'Banner', 'SimulatorBuild')) {
    $standaloneText = Get-Content -Raw -LiteralPath $paths[$standaloneKey]
    if ($standaloneText -match '(?i)device\.ps1') {
        throw "$($paths[$standaloneKey]) must remain standalone from device.ps1."
    }
}

$bannerText = Get-Content -Raw -LiteralPath $paths.Banner
foreach ($ownedRoot in @(
    'main',
    'components/LUNASHELL_app',
    'components/LUNASHELL_boot',
    'components/LUNASHELL_logger',
    'components/LUNASHELL_platform'
)) {
    $ownedRootCount = ([regex]::Matches(
        $bannerText,
        [regex]::Escape("'$ownedRoot'")
    )).Count
    if ($ownedRootCount -ne 1) {
        throw "add-source-banner.ps1 lost owned-root allowlist entry $ownedRoot."
    }
}
foreach ($requiredGuard in @(
    'Source root is not LUNASHELL-owned',
    'FileAttributes]::ReparsePoint',
    'esp32_s3_touch_amoled_1_75',
    'LeafIsFile',
    'Get-LeadingHeaderClassification',
    'LegacyPersonal',
    'Source banner apply refused before writing'
)) {
    if (-not $bannerText.Contains($requiredGuard, [StringComparison]::Ordinal)) {
        throw "add-source-banner.ps1 lost guard $requiredGuard."
    }
}

foreach ($entrypointKey in @(
    'Capture', 'Assets', 'Sfx', 'Banner', 'SimulatorBuild'
)) {
    $entrypointText = Get-Content -Raw -LiteralPath $paths[$entrypointKey]
    if ($entrypointText -match '(?i)(?:^|[\\/])idf\.py(?:\s|''|"|$)') {
        throw "$($paths[$entrypointKey]) must not invoke idf.py directly."
    }
}

$simulatorBuildText = Get-Content -Raw -LiteralPath $paths.SimulatorBuild
foreach ($requiredGuard in @(
    'Simulator virtual-environment creation failed',
    'Simulator packaging dependencies failed to install',
    'Simulator executable build failed',
    'completed without a valid executable'
)) {
    Assert-TextContains -Text $simulatorBuildText -Expected $requiredGuard `
        -Message "build-exe.ps1 lost fail-fast guard $requiredGuard."
}

$assetText = Get-Content -Raw -LiteralPath $paths.Assets
foreach ($requiredReference in @('generate_boot_assets.py', 'generate-sfx.ps1')) {
    if (-not $assetText.Contains($requiredReference, [StringComparison]::Ordinal)) {
        throw "generate-device-assets.ps1 does not invoke $requiredReference."
    }
}
foreach ($requiredGuard in @(
    'bootSourceRoot',
    'bootGeneratedRoot',
    '[switch]$ValidateOnly',
    'Backup-PackageFiles',
    'Restore-PackageFiles',
    'package rollback incomplete',
    '.device-asset-generator.lock',
    'LUNASHELL_ASSET_LOCK_TOKEN',
    'Open-DeviceAssetConfigSnapshot',
    'Assert-DeviceAssetConfigUnchanged',
    'ExpectedConfigSha256',
    'asset-lock environment restore failed',
    'package state='
)) {
    if (-not $assetText.Contains($requiredGuard, [StringComparison]::Ordinal)) {
        throw "generate-device-assets.ps1 lost boot path guard $requiredGuard."
    }
}
Assert-TextOrder -Text $assetText `
    -Earlier "@bootArguments '--validate-only'" `
    -Later 'Backup-PackageFiles -Destinations' `
    -Message 'Boot preflight must finish before package backup or generation.'
Assert-TextOrder -Text $assetText `
    -Earlier '& $sfxGenerator -ConfigPath $resolvedConfig -NoProgress -ValidateOnly' `
    -Later 'Backup-PackageFiles -Destinations' `
    -Message 'SFX preflight must finish before package backup or generation.'
Assert-TextOrder -Text $assetText `
    -Earlier 'Backup-PackageFiles -Destinations' `
    -Later "Write-Host 'Generating deterministic boot assets.'" `
    -Message 'The complete package must be backed up before canonical writes.'
Assert-TextOrder -Text $assetText `
    -Earlier '$configSnapshot = Open-DeviceAssetConfigSnapshot' `
    -Later "@bootArguments '--validate-only'" `
    -Message 'The immutable config snapshot must precede child preflight.'
Assert-TextOrder -Text $assetText `
    -Earlier "-ExpectedSha256 `$configSha256 -Phase 'after package commit'" `
    -Later '$packageCommitted = $true' `
    -Message 'The config hash must be rechecked before package commit succeeds.'
if ($assetText.Contains(
    'LUNASHELL_COMPLETE_DEVICE_ASSET_PACKAGE',
    [StringComparison]::Ordinal
)) {
    throw 'generate-device-assets.ps1 still declares the superseded package mutex.'
}
$sfxText = Get-Content -Raw -LiteralPath $paths.Sfx
foreach ($requiredTool in @('ffmpeg', 'ffprobe')) {
    if (-not $sfxText.Contains($requiredTool, [StringComparison]::Ordinal)) {
        throw "generate-sfx.ps1 does not declare its $requiredTool workflow."
    }
}
foreach ($requiredGuard in @(
    'preparedAssets',
    'sourceRoot',
    'generatedRoot',
    'assetOutputs',
    'durationTolerance',
    'format=format_name',
    '[switch]$ValidateOnly',
    'Test-FiniteNumber',
    'Assert-CanonicalFirmwareWav',
    'Commit-StagedFiles',
    'rollback incomplete',
    '.device-asset-generator.lock',
    'LUNASHELL_ASSET_LOCK_TOKEN',
    'ExpectedConfigSha256',
    'Open-SfxConfigSnapshot',
    'Assert-SfxConfigUnchanged',
    'PostCommitValidation',
    'firmware requires wav/pcm_s16le/22050 Hz/mono',
    'duration -gt 3.0',
    'outputLeaf -cne $outputLeaf.ToLowerInvariant()',
    'normalization = $config.normalization',
    "'-nostdin'",
    "'-map', '0:a:0'"
)) {
    if (-not $sfxText.Contains($requiredGuard, [StringComparison]::Ordinal)) {
        throw "generate-sfx.ps1 lost preflight guard $requiredGuard."
    }
}
Assert-TextOrder -Text $sfxText `
    -Earlier '# The manifest is deliberately committed after every referenced output.' `
    -Later 'Commit-StagedFiles -Entries' `
    -Message 'The SFX manifest-last commit contract is missing.'
Assert-TextOrder -Text $sfxText `
    -Earlier '$configSnapshot = Open-SfxConfigSnapshot' `
    -Later 'if ($ValidateOnly)' `
    -Message 'SFX preflight must use the immutable config snapshot.'
Assert-TextOrder -Text $sfxText `
    -Earlier "-ExpectedSha256 `$configSha256 -Phase 'before commit'" `
    -Later 'Commit-StagedFiles -Entries' `
    -Message 'SFX must recheck the config hash before canonical mutation.'
if ($sfxText.Contains(
    'LUNASHELL_SFX_ASSET_GENERATOR',
    [StringComparison]::Ordinal
)) {
    throw 'generate-sfx.ps1 still declares the superseded SFX mutex.'
}

$bootPythonPath = Join-Path $scriptsRoot 'generate_boot_assets.py'
$bootPythonText = Get-Content -Raw -LiteralPath $bootPythonPath
foreach ($requiredGuard in @(
    '--validate-only',
    'source.relative_to(source_root)',
    'assert_no_reparse_tree',
    'FILE_ATTRIBUTE_REPARSE_POINT',
    'validate_boot_outputs',
    'commit_staged_files',
    'RollbackIncompleteError',
    '.device-asset-generator.lock',
    'LUNASHELL_ASSET_LOCK_TOKEN',
    'enter_asset_package_lock',
    'exit_asset_package_lock',
    'primary_error',
    'cleanup_errors',
    'tempfile.mkdtemp',
    'PILLOW_VERSION',
    'poppler_version',
    'selected_font'
)) {
    Assert-TextContains -Text $bootPythonText -Expected $requiredGuard `
        -Message "generate_boot_assets.py lost guard $requiredGuard."
}
Assert-TextOrder -Text $bootPythonText `
    -Earlier 'source.relative_to(source_root)' `
    -Later 'tempfile.mkdtemp' `
    -Message 'generate_boot_assets.py must validate containment before staging.'
Assert-TextOrder -Text $bootPythonText `
    -Earlier '(staged_guide, guide_path),' `
    -Later '(staged_manifest, manifest_path),' `
    -Message 'The boot manifest must remain last in its commit list.'
if ($bootPythonText.Contains(
    '.boot-assets-generator.lock',
    [StringComparison]::Ordinal
)) {
    throw 'generate_boot_assets.py still declares the superseded boot-only lock.'
}

$brandingPath = Join-Path $scriptsRoot 'verify_public_branding.py'
$brandingText = Get-Content -Raw -LiteralPath $brandingPath
foreach ($requiredTraversalGuard in @(
    'os.walk(',
    'topdown=True',
    'directory_names[:]',
    'is_excluded_directory'
)) {
    Assert-TextContains -Text $brandingText -Expected $requiredTraversalGuard `
        -Message "verify_public_branding.py lost traversal guard $requiredTraversalGuard."
}
if ($brandingText.Contains('ROOT.rglob(', [StringComparison]::Ordinal)) {
    throw 'verify_public_branding.py must prune excluded trees during traversal.'
}

$captureCommands = @(
    $asts.Capture.FindAll(
        {
            param($node)
            if ($node -isnot [System.Management.Automation.Language.CommandAst]) {
                return $false
            }
            return @(
                $node.CommandElements |
                    Where-Object {
                        $_ -is [System.Management.Automation.Language.StringConstantExpressionAst] -and
                        $_.Value -eq 'capture'
                    }
            ).Count -eq 1
        },
        $true
    )
)
if ($captureCommands.Count -ne 1) {
    throw 'capture-serial.ps1 must dispatch exactly one capture command.'
}
$captureCommandElements = @($captureCommands[0].CommandElements)
if ($captureCommandElements.Count -lt 2 -or
    $captureCommandElements[0].Extent.Text -cne '$entrypoint' -or
    $captureCommandElements[1].SafeGetValue() -cne 'capture') {
    throw 'capture-serial.ps1 must invoke the device.ps1 entrypoint capture action.'
}
$captureText = Get-Content -Raw -LiteralPath $paths.Capture
Assert-TextContains -Text $captureText -Expected "'device.ps1'" `
    -Message 'capture-serial.ps1 no longer resolves scripts/device.ps1.'

$forwardedParameters = @(
    $captureCommands[0].CommandElements |
        Where-Object {
            $_ -is [System.Management.Automation.Language.CommandParameterAst]
        } |
        ForEach-Object { $_.ParameterName }
)
foreach ($parameterName in $forwardedParameters) {
    Assert-Contains -Values $deviceParameters -Expected $parameterName `
        -Message "capture-serial.ps1 forwards unsupported parameter -$parameterName."
}
foreach ($requiredParameter in @(
    'Port', 'Baud', 'DurationSeconds', 'OutputPath', 'BuildDir', 'WaitSeconds',
    'ResetBeforeCapture', 'IdfPath', 'ToolsPath', 'PythonExe', 'Jobs'
)) {
    Assert-Contains -Values $forwardedParameters -Expected $requiredParameter `
        -Message "capture-serial.ps1 does not forward -$requiredParameter."
}

$captureParameters = @(Get-ParameterNames -Ast $asts.Capture)
if ($captureParameters.Count -ne $forwardedParameters.Count -or
    @($captureParameters | Where-Object { $_ -notin $forwardedParameters }).Count -gt 0) {
    throw 'capture-serial.ps1 must forward every declared option exactly once.'
}
$capturePortParameter = $asts.Capture.ParamBlock.Parameters |
    Where-Object { $_.Name.VariablePath.UserPath -eq 'Port' } |
    Select-Object -First 1
if ($null -eq $capturePortParameter -or $null -ne $capturePortParameter.DefaultValue) {
    throw 'capture-serial.ps1 must require an explicit serial port.'
}
foreach ($removedParameter in @(
    'NonInteractive', 'NoBanner', 'NoProgress', 'StopStaleMonitors'
)) {
    if ($removedParameter -in $captureParameters) {
        throw "capture-serial.ps1 still exposes removed parameter -$removedParameter."
    }
}

$sfxConfigPath = Join-Path $scriptsRoot '..\assets\sfx\sfx-config.json'
$sfxManifestPath = Join-Path $scriptsRoot '..\assets\sfx\generated\sfx.manifest.json'
$sfxConfig = Get-Content -Raw -LiteralPath $sfxConfigPath | ConvertFrom-Json
$sfxManifest = Get-Content -Raw -LiteralPath $sfxManifestPath | ConvertFrom-Json
$expectedBootSound = 'generated/lunashell-boot.wav'
$configBootSound = @($sfxConfig.assets) |
    Where-Object { [string]$_.id -ceq 'LUNASHELL_boot' } |
    Select-Object -First 1
$manifestBootSound = @($sfxManifest.generated_assets) |
    Where-Object { [string]$_.id -ceq 'LUNASHELL_boot' } |
    Select-Object -First 1
if ($null -eq $configBootSound -or
    [string]$configBootSound.output -cne $expectedBootSound -or
    $null -eq $manifestBootSound -or
    [string]$manifestBootSound.output -cne $expectedBootSound) {
    throw 'SFX configuration and manifest must use the canonical lowercase boot WAV.'
}

$lowercaseReferencePaths = @(
    (Join-Path $scriptsRoot '..\components\LUNASHELL_boot\CMakeLists.txt'),
    (Join-Path $scriptsRoot '..\simulator\windows\build-exe.ps1'),
    (Join-Path $scriptsRoot '..\simulator\windows\lunashell_simulator.py'),
    (Join-Path $scriptsRoot '..\docs\ui\round-display-guidelines.md')
)
foreach ($referencePath in $lowercaseReferencePaths) {
    $referenceText = Get-Content -Raw -LiteralPath $referencePath
    if ($referenceText.Contains('LUNASHELL-boot.wav', [StringComparison]::Ordinal) -or
        -not $referenceText.Contains('lunashell-boot.wav', [StringComparison]::Ordinal)) {
        throw "Boot WAV casing is inconsistent in $referencePath."
    }
}

$generatedSfxRoot = Join-Path $scriptsRoot '..\assets\sfx\generated'
$actualBootSounds = @(
    Get-ChildItem -LiteralPath $generatedSfxRoot -File |
        Where-Object { $_.Name.Equals('lunashell-boot.wav', [StringComparison]::OrdinalIgnoreCase) }
)
if ($actualBootSounds.Count -ne 1 -or
    $actualBootSounds[0].Name -cne 'lunashell-boot.wav') {
    throw 'The canonical boot WAV must exist with exact lowercase casing.'
}
$wavBytes = [System.IO.File]::ReadAllBytes($actualBootSounds[0].FullName)
if ($wavBytes.Length -lt 44 -or
    [System.Text.Encoding]::ASCII.GetString($wavBytes, 0, 4) -cne 'RIFF' -or
    [System.Text.Encoding]::ASCII.GetString($wavBytes, 8, 4) -cne 'WAVE' -or
    [System.Text.Encoding]::ASCII.GetString($wavBytes, 12, 4) -cne 'fmt ' -or
    [System.Text.Encoding]::ASCII.GetString($wavBytes, 36, 4) -cne 'data' -or
    (Read-UInt32LittleEndian -Bytes $wavBytes -Offset 4) -ne ($wavBytes.Length - 8) -or
    (Read-UInt32LittleEndian -Bytes $wavBytes -Offset 16) -ne 16 -or
    (Read-UInt16LittleEndian -Bytes $wavBytes -Offset 20) -ne 1 -or
    (Read-UInt16LittleEndian -Bytes $wavBytes -Offset 22) -ne 1 -or
    (Read-UInt32LittleEndian -Bytes $wavBytes -Offset 24) -ne 22050 -or
    (Read-UInt32LittleEndian -Bytes $wavBytes -Offset 28) -ne 44100 -or
    (Read-UInt16LittleEndian -Bytes $wavBytes -Offset 32) -ne 2 -or
    (Read-UInt16LittleEndian -Bytes $wavBytes -Offset 34) -ne 16 -or
    (Read-UInt32LittleEndian -Bytes $wavBytes -Offset 40) -ne ($wavBytes.Length - 44) -or
    (($wavBytes.Length - 44) % 2) -ne 0) {
    throw 'The tracked boot WAV does not match the exact firmware RIFF/PCM contract.'
}

Write-Host (
    'maintenance script contracts: PASS ' +
    "($($paths.Count) scripts parsed, $($deviceActions.Count) device actions checked)"
)
