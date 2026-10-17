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

# Explicit, hardware-free checks of the real build-mode selection and safety gates.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $ProjectRoot 'scripts/common/BuildMode.ps1')
. (Join-Path $ProjectRoot 'scripts/common/Artifacts.ps1')
. (Join-Path $ProjectRoot 'scripts/common/FirmwareBuildMetadata.ps1')
$catalog = Get-Content -Raw -LiteralPath (Join-Path $ProjectRoot 'config/sdkconfig/v1/catalog.json') |
    ConvertFrom-Json -Depth 20

# Operation is the actual selection/configuration action expected to fail.
# ExpectedMessage identifies the failure reason; an unrelated exception is not a pass.
# Throws when the action succeeds or reports a different error.
function Assert-BuildModeRejection([scriptblock]$Operation, [string]$ExpectedMessage) {
    try {
        & $Operation | Out-Null
    } catch {
        if (-not $_.Exception.Message.Contains($ExpectedMessage)) { throw }
        return
    }
    throw "Expected build-mode rejection: $ExpectedMessage"
}

$debugSelection = Resolve-FirmwareBuildMode -Catalog $catalog
if ($debugSelection.Name -cne 'Debug') { throw 'An omitted mode did not select Debug.' }
Assert-BuildModeRejection {
    Resolve-FirmwareBuildMode -Catalog $catalog -BuildMode Release
} 'explicit -BuildMode Release'
$releaseSelection = Resolve-FirmwareBuildMode -Catalog $catalog -BuildMode release -ExplicitSelection $true
if ($releaseSelection.Name -cne 'Release' -or $releaseSelection.ArtifactSuffix -eq $debugSelection.ArtifactSuffix) {
    throw 'Explicit Release must have its own canonical identity and artifact namespace.'
}
$invalidCatalog = $catalog | ConvertTo-Json -Depth 20 | ConvertFrom-Json -Depth 20
$invalidCatalog.default_build_mode = 'Release'
Assert-BuildModeRejection { Resolve-FirmwareBuildMode -Catalog $invalidCatalog } 'must default to Debug'

# These fixtures represent generated IDF configuration, not source defaults.
$debugConfig = @'
CONFIG_LUNASHELL_BUILD_MODE_DEBUG=y
# CONFIG_LUNASHELL_BUILD_MODE_RELEASE is not set
# CONFIG_COMPILER_OPTIMIZATION_SIZE is not set
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_PM_LIGHT_SLEEP_CALLBACKS=y
# CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION is not set
CONFIG_LOG_DEFAULT_LEVEL=3
CONFIG_LOG_MAXIMUM_LEVEL=5
CONFIG_BOOTLOADER_LOG_LEVEL=3
'@
$releaseConfig = @'
CONFIG_LUNASHELL_BUILD_MODE_RELEASE=y
# CONFIG_LUNASHELL_BUILD_MODE_DEBUG is not set
CONFIG_COMPILER_OPTIMIZATION_PERF=y
# CONFIG_COMPILER_OPTIMIZATION_SIZE is not set
# CONFIG_PM_LIGHT_SLEEP_CALLBACKS is not set
# CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION is not set
CONFIG_LOG_DEFAULT_LEVEL=2
CONFIG_LOG_MAXIMUM_LEVEL=2
CONFIG_BOOTLOADER_LOG_LEVEL=2
'@
Assert-FirmwareBuildModeConfiguration -SdkconfigText $debugConfig -BuildMode Debug
Assert-FirmwareBuildModeConfiguration -SdkconfigText $releaseConfig -BuildMode Release
Assert-BuildModeRejection {
    Assert-FirmwareBuildModeConfiguration -SdkconfigText ($debugConfig.Replace('CONFIG_COMPILER_OPTIMIZATION_PERF=y', '# CONFIG_COMPILER_OPTIMIZATION_PERF is not set')) -BuildMode Debug
} 'COMPILER_OPTIMIZATION_PERF'
Assert-BuildModeRejection {
    Assert-FirmwareBuildModeConfiguration -SdkconfigText $releaseConfig -BuildMode Debug
} 'rejected sdkconfig'
Assert-BuildModeRejection {
    Assert-FirmwareBuildModeConfiguration -SdkconfigText ($releaseConfig.Replace('CONFIG_LOG_DEFAULT_LEVEL=2', 'CONFIG_LOG_DEFAULT_LEVEL=3')) -BuildMode Release
} 'LOG_DEFAULT_LEVEL must be 2'
Assert-BuildModeRejection {
    Assert-FirmwareBuildModeConfiguration -SdkconfigText ($releaseConfig.Replace('# CONFIG_PM_LIGHT_SLEEP_CALLBACKS is not set', 'CONFIG_PM_LIGHT_SLEEP_CALLBACKS=y')) -BuildMode Release
} 'PM_LIGHT_SLEEP_CALLBACKS'
Assert-BuildModeRejection {
    Assert-FirmwareBuildModeConfiguration -SdkconfigText ($releaseConfig.Replace('# CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION is not set', 'CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y')) -BuildMode Release
} 'USJ_NO_AUTO_LS_ON_CONNECTION'
# Explicit fixture inputs exercise the real serializer without reading this PC's
# hostname, timezone or clock, or running CMake/Ninja.
$fixtureZone = [TimeZoneInfo]::CreateCustomTimeZone('SIMULATED Zone +07', [TimeSpan]::FromHours(7),
    'SIMULATED Zone +07', 'SIMULATED Zone +07')
$fixtureInstant = [DateTimeOffset]::Parse('2026-10-14T21:35:10+00:00', [Globalization.CultureInfo]::InvariantCulture)
$metadata = New-FirmwareBuildMetadata -Instant $fixtureInstant -TimeZone $fixtureZone -ComputerName 'SIMULATED-PC'
if ($metadata.local_date_time -cne '15/10/26 04:35' -or $metadata.time_zone -cne $fixtureZone.Id -or
    $metadata.computer -cne 'SIMULATED-PC' -or $metadata.clock_state -cne 'Available') {
    throw 'Build identity lost the explicit local zone, date rollover or exact computer name.'
}
$quotedName = 'SIMULATED"PC\NAME'
$quotedMetadata = New-FirmwareBuildMetadata -Instant $fixtureInstant -TimeZone $fixtureZone -ComputerName $quotedName
if ((ConvertTo-FirmwareBuildCppString $quotedMetadata.computer) -cne '"SIMULATED\"PC\\NAME"') {
    throw 'Build identity C++ serialization did not preserve quote/backslash characters.'
}
$longMetadata = New-FirmwareBuildMetadata -Instant $fixtureInstant -TimeZone $fixtureZone -ComputerName ('X' * 64)
if ($longMetadata.computer_state -cne 'Unsupported' -or $longMetadata.computer -cne '') {
    throw 'An oversized computer name was truncated or presented as available.'
}
$maximumMetadata = New-FirmwareBuildMetadata -Instant $fixtureInstant -TimeZone $fixtureZone -ComputerName ('X' * 63)
if ($maximumMetadata.computer_state -cne 'Available' -or $maximumMetadata.computer -cne ('X' * 63)) {
    throw 'An exactly bounded computer name was rejected or changed.'
}
$unicodeMetadata = New-FirmwareBuildMetadata -Instant $fixtureInstant -TimeZone $fixtureZone -ComputerName ('PC' + [char]0xE9)
if ($unicodeMetadata.computer_state -cne 'Unsupported' -or $unicodeMetadata.computer -cne '') {
    throw 'Non-ASCII computer identity was stripped or transliterated.'
}
$emptyMetadata = New-FirmwareBuildMetadata -Instant $fixtureInstant -TimeZone $fixtureZone -ComputerName ''
if ($emptyMetadata.computer_state -cne 'Unavailable' -or $emptyMetadata.computer -cne '') {
    throw 'An absent hostname was replaced with an invented value.'
}
$unsupportedZone = [TimeZoneInfo]::CreateCustomTimeZone(('SIMULATED ' + [char]0xE9), [TimeSpan]::FromHours(7),
    'SIMULATED', 'SIMULATED')
$unsupportedMetadata = New-FirmwareBuildMetadata -Instant $fixtureInstant -TimeZone $unsupportedZone -ComputerName 'SIMULATED-PC'
if ($unsupportedMetadata.clock_state -cne 'Unsupported' -or $unsupportedMetadata.time_zone -cne '' -or
    $unsupportedMetadata.local_date_time -cne '') {
    throw 'An unsupported timezone was replaced with UTC or a guessed identity.'
}
$fixtureBuild = Resolve-ArtifactPath 'artifacts/checks/build-metadata-fixture' ''
Write-FirmwareBuildMetadata -BuildDirectory $fixtureBuild -Metadata $quotedMetadata
$headerPath = Join-Path $fixtureBuild 'generated/LUNASHELL_build_metadata_generated.hpp'
$firstHeader = [IO.File]::ReadAllText($headerPath)
if (-not $firstHeader.Contains((ConvertTo-FirmwareBuildCppString $quotedName))) {
    throw 'Generated immutable header lost the exact safely escaped computer name.'
}
$laterMetadata = New-FirmwareBuildMetadata -Instant ($fixtureInstant.AddSeconds(1)) -TimeZone $fixtureZone -ComputerName $quotedName
Write-FirmwareBuildMetadata -BuildDirectory $fixtureBuild -Metadata $laterMetadata
if ([IO.File]::ReadAllText($headerPath) -ceq $firstHeader) {
    throw 'A later real build entry reused an old immutable capture header.'
}
$savedMetadata = Get-Content -Raw -LiteralPath (Join-Path $fixtureBuild 'generated/firmware-build-metadata.json') | ConvertFrom-Json
if ($savedMetadata.captured_at -cne $laterMetadata.captured_at -or $savedMetadata.local_date_time -cne '15/10/26 04:35') {
    throw 'Build metadata audit artifact does not match the image header capture.'
}
Assert-BuildModeRejection {
    Write-FirmwareBuildMetadata -BuildDirectory (Join-Path $ProjectRoot 'forbidden-output') -Metadata $metadata
} 'must be below'
# Keep the captured string type: ConvertFrom-Json may infer DateTime from ISO text.
$invalidMetadata = New-FirmwareBuildMetadata -Instant $fixtureInstant -TimeZone $fixtureZone -ComputerName 'SIMULATED-PC'
$invalidMetadata.clock_state = 'Unavailable'
Assert-BuildModeRejection {
    Write-FirmwareBuildMetadata -BuildDirectory $fixtureBuild -Metadata $invalidMetadata
} 'validity disagrees'
Write-Host 'Build-mode and immutable local build identity contracts passed (synthetic metadata; no ESP-IDF or hardware access).'
