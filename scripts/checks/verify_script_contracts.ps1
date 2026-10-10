<#
.SYNOPSIS
Verify maintenance-script parsing and dispatch contracts without executing them.

.DESCRIPTION
Uses the PowerShell abstract syntax tree to check that every maintenance
entrypoint parses, asset/banner scripts remain standalone, and the public
device dispatcher preserves native firmware actions. It performs no build,
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

$scriptsRoot = Split-Path -Parent $PSScriptRoot
$paths = [ordered]@{
    Device = Join-Path $scriptsRoot 'device.ps1'
    Firmware = Join-Path $scriptsRoot 'commands\firmware.ps1'
    Assets = Join-Path $scriptsRoot 'generators\generate-device-assets.ps1'
    Sfx = Join-Path $scriptsRoot 'generators\generate-sfx.ps1'
    Banner = Join-Path $scriptsRoot 'commands\add-source-banner.ps1'
    SimulatorBuild = Join-Path $scriptsRoot 'commands\package-simulator.ps1'
}
$asts = @{}
foreach ($entry in $paths.GetEnumerator()) {
    $asts[$entry.Key] = Read-ScriptAst -Path $entry.Value
}

$deviceParameters = @(Get-ParameterNames -Ast $asts.Device)
foreach ($requiredSizeParameter in @('SizeOutputFormat', 'SizeOutputFile')) {
    Assert-Contains -Values $deviceParameters -Expected $requiredSizeParameter `
        -Message "device.ps1 no longer exposes $requiredSizeParameter."
}
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

$deviceScriptText = $asts.Firmware.Extent.Text
foreach ($requiredSizeFragment in @(
    '@("size", "--format", $SizeOutputFormat)',
    '@("--output-file", $resolvedSizeOutputFile)'
)) {
    Assert-TextContains -Text $deviceScriptText `
        -Expected $requiredSizeFragment `
        -Message "device.ps1 size forwarding lost $requiredSizeFragment."
}

$projectBuildFunction = $asts.Firmware.Find(
    {
        param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -ceq 'Invoke-ProjectBuild'
    },
    $true
)
if ($null -eq $projectBuildFunction) {
    throw 'device.ps1 no longer defines Invoke-ProjectBuild.'
}
$projectBuildText = $projectBuildFunction.Extent.Text
foreach ($requiredBuildFragment in @(
    '$reconfigure = -not (Test-Path $buildNinja)',
    '"-DSDKCONFIG=$ProfileSdkconfigPath"',
    '"-DLUNASHELL_DEPENDENCIES_LOCK=',
    '& ninja -C $RequestedBuildDir -j $parallelJobs all'
)) {
    Assert-TextContains -Text $projectBuildText `
        -Expected $requiredBuildFragment `
        -Message "device.ps1 incremental build lost $requiredBuildFragment."
}
if ($projectBuildText.Contains('Get-ChildItem', [StringComparison]::Ordinal)) {
    throw 'device.ps1 must not recursively rescan the repository before each Ninja build.'
}

$projectFlashFunction = $asts.Firmware.Find(
    {
        param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -ceq 'Invoke-ProjectFlash'
    },
    $true
)
if ($null -eq $projectFlashFunction) {
    throw 'firmware.ps1 no longer defines the guarded Invoke-ProjectFlash helper.'
}
$projectFlashText = $projectFlashFunction.Extent.Text
foreach ($requiredFlashFragment in @(
    'Invoke-ProjectBuild $RequestedBuildDir',
    'Stop-StaleMonitorProcesses $flashPort',
    'Wait-ForExclusiveSerialPort $flashPort $WaitSeconds',
    '& ninja -C $RequestedBuildDir -j $parallelJobs flash',
    'Assert-WorkingDependencyLock $RequestedBuildDir',
    'Save-SdkconfigDefaultsState $RequestedBuildDir',
    '$environmentBeforeFlash[$name]'
)) {
    Assert-TextContains -Text $projectFlashText -Expected $requiredFlashFragment `
        -Message "firmware.ps1 guarded flash lost $requiredFlashFragment."
}
Assert-TextOrder -Text $projectFlashText `
    -Earlier 'Invoke-ProjectBuild $RequestedBuildDir' `
    -Later 'Stop-StaleMonitorProcesses $flashPort' `
    -Message 'Flash must finish the guarded build before preparing the serial port.'
Assert-TextOrder -Text $projectFlashText `
    -Earlier 'Wait-ForExclusiveSerialPort $flashPort $WaitSeconds' `
    -Later '& ninja -C $RequestedBuildDir -j $parallelJobs flash' `
    -Message 'Flash must acquire the selected serial port before the bounded Ninja target.'
Assert-TextOrder -Text $projectFlashText `
    -Earlier '& ninja -C $RequestedBuildDir -j $parallelJobs flash' `
    -Later 'Assert-WorkingDependencyLock $RequestedBuildDir' `
    -Message 'Successful flash must verify the resulting dependency lock.'
Assert-TextOrder -Text $projectFlashText `
    -Earlier 'Assert-WorkingDependencyLock $RequestedBuildDir' `
    -Later 'Save-SdkconfigDefaultsState $RequestedBuildDir' `
    -Message 'Successful flash must attest the resulting sdkconfig after lock verification.'

$defaultsFunctions = @{}
foreach ($functionName in @(
    'Get-SdkconfigDefaultsState',
    'Get-SdkconfigDefaultsStatePath',
    'Get-SetTargetRecoveryCommand',
    'Save-SdkconfigDefaultsState',
    'Assert-SdkconfigDefaultsApplied'
)) {
    $functionAst = $asts.Firmware.Find(
        {
            param($node)
            $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
                $node.Name -ceq $functionName
        },
        $true
    )
    if ($null -eq $functionAst) {
        throw "device.ps1 lost sdkconfig state function $functionName."
    }
    $defaultsFunctions[$functionName] = $functionAst.Extent.Text
}

$defaultsStateText = $defaultsFunctions['Get-SdkconfigDefaultsState']
foreach ($requiredDefaultsStateFragment in @(
    '$SelectedSdkconfigDefaults',
    '$ProfileSdkconfigPath',
    '$resolvedDefault.esp32s3',
    'Get-FileHash -LiteralPath $defaultPath -Algorithm SHA256',
    'Get-FileHash -LiteralPath $generatedSdkconfig -Algorithm SHA256',
    'sdkconfig|$sdkconfigHash',
    'target=esp32s3',
    'profile=$Profile',
    'feature=$Feature'
)) {
    Assert-TextContains -Text $defaultsStateText `
        -Expected $requiredDefaultsStateFragment `
        -Message "device.ps1 defaults fingerprint lost $requiredDefaultsStateFragment."
}
foreach ($requiredAppliedStateFragment in @(
    'Get-SdkconfigDefaultsState',
    '[System.IO.File]::ReadAllText($statePath)',
    '[StringComparison]::Ordinal',
    'SDKCONFIG, defaults, or active overlays changed'
)) {
    Assert-TextContains `
        -Text $defaultsFunctions['Assert-SdkconfigDefaultsApplied'] `
        -Expected $requiredAppliedStateFragment `
        -Message "device.ps1 defaults comparison lost $requiredAppliedStateFragment."
}
Assert-TextContains -Text $defaultsFunctions['Save-SdkconfigDefaultsState'] `
    -Expected '[System.IO.File]::WriteAllText(' `
    -Message 'device.ps1 no longer persists the applied defaults fingerprint.'
Assert-TextOrder -Text $projectBuildText `
    -Earlier 'Assert-SdkconfigDefaultsApplied $RequestedBuildDir' `
    -Later '$buildNinja = Join-Path $RequestedBuildDir "build.ninja"' `
    -Message 'Project builds must verify defaults before inspecting build state.'
Assert-TextOrder -Text $projectBuildText `
    -Earlier '& ninja -C $RequestedBuildDir -j $parallelJobs all' `
    -Later 'Save-SdkconfigDefaultsState $RequestedBuildDir' `
    -Message 'Successful project builds must attest the resulting sdkconfig.'
Assert-TextContains -Text $projectBuildText `
    -Expected '"-DSDKCONFIG=$ProfileSdkconfigPath"' `
    -Message 'Incremental reconfigure lost the profile-scoped SDKCONFIG path.'

$actionSwitch = $asts.Firmware.Find(
    {
        param($node)
        $node -is [System.Management.Automation.Language.SwitchStatementAst] -and
            $node.Condition.Extent.Text -ceq '$Action'
    },
    $true
)
if ($null -eq $actionSwitch) {
    throw 'device.ps1 no longer dispatches actions through switch ($Action).'
}
$actionBodies = @{}
foreach ($clause in $actionSwitch.Clauses) {
    $actionBodies[$clause.Item1.SafeGetValue()] = $clause.Item2.Extent.Text
}
foreach ($protectedAction in @('reconfigure', 'size')) {
    Assert-TextOrder -Text $actionBodies[$protectedAction] `
        -Earlier 'Assert-SdkconfigDefaultsApplied $BuildDir' `
        -Later '& $PythonExe $IdfPy' `
        -Message "device.ps1 $protectedAction must verify defaults before ESP-IDF."
    Assert-TextOrder -Text $actionBodies[$protectedAction] `
        -Earlier '& $PythonExe $IdfPy' `
        -Later 'Save-SdkconfigDefaultsState $BuildDir' `
        -Message "device.ps1 $protectedAction must attest the resulting sdkconfig."
}
Assert-TextContains -Text $actionBodies['build'] `
    -Expected 'Invoke-ProjectBuild $BuildDir' `
    -Message 'firmware.ps1 build bypasses the guarded build path.'
foreach ($flashAction in @('flash', 'combo', 'recover')) {
    Assert-TextContains -Text $actionBodies[$flashAction] `
        -Expected 'Invoke-ProjectFlash $BuildDir' `
        -Message "firmware.ps1 $flashAction bypasses the guarded flash path."
}
Assert-TextContains -Text $actionBodies['recover'] `
    -Expected 'Invoke-ProjectFlash $BuildDir $true' `
    -Message 'Recovery must retain the bounded reconnect wait.'
Assert-TextOrder -Text $actionBodies['set-target'] `
    -Earlier 'Invoke-ProfileConfiguration $BuildDir $false' `
    -Later 'Save-SdkconfigDefaultsState $BuildDir' `
    -Message 'set-target must persist its defaults fingerprint after ESP-IDF succeeds.'

foreach ($standaloneKey in @('Assets', 'Sfx', 'Banner', 'SimulatorBuild')) {
    # Help examples may document the public entrypoint; executable strings must stay independent.
    $dispatcherReferences = @($asts[$standaloneKey].FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.StringConstantExpressionAst] -and
            $node.Value -match '(?i)device\.ps1'
    }, $true))
    if ($dispatcherReferences.Count -gt 0) {
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
    'Device', 'Assets', 'Sfx', 'Banner', 'SimulatorBuild'
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
        -Message "package-simulator.ps1 lost fail-fast guard $requiredGuard."
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

$bootPythonPath = Join-Path $scriptsRoot 'generators\generate_boot_assets.py'
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

$brandingPath = Join-Path $scriptsRoot 'checks\verify_public_branding.py'
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

# Capture belongs to the unified public dispatcher and the native firmware command.
foreach ($requiredParameter in @(
    'Port', 'Baud', 'DurationSeconds', 'OutputPath', 'BuildDir', 'WaitSeconds',
    'ResetBeforeCapture', 'IdfPath', 'ToolsPath', 'PythonExe', 'Jobs'
)) {
    Assert-Contains -Values $deviceParameters -Expected $requiredParameter `
        -Message "device.ps1 no longer exposes native capture parameter $requiredParameter."
}
Assert-TextContains -Text $deviceScriptText -Expected 'function Invoke-SerialCapture' `
    -Message 'firmware.ps1 no longer owns serial capture.'
if (Test-Path -LiteralPath (Join-Path $scriptsRoot 'capture-serial.ps1')) {
    throw 'The redundant capture-serial.ps1 alias must not return.'
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
    (Join-Path $scriptsRoot 'commands\package-simulator.ps1'),
    (Join-Path $scriptsRoot '..\simulator\windows\lunashell_simulator.py'),
    (Join-Path $scriptsRoot '..\docs\luna-shell\ui-guidelines.md')
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
