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
CONFIG_PM_LIGHT_SLEEP_CALLBACKS=y
# CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION is not set
CONFIG_LOG_DEFAULT_LEVEL=3
CONFIG_LOG_MAXIMUM_LEVEL=5
CONFIG_BOOTLOADER_LOG_LEVEL=3
'@
$releaseConfig = @'
CONFIG_LUNASHELL_BUILD_MODE_RELEASE=y
# CONFIG_LUNASHELL_BUILD_MODE_DEBUG is not set
# CONFIG_PM_LIGHT_SLEEP_CALLBACKS is not set
# CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION is not set
CONFIG_LOG_DEFAULT_LEVEL=2
CONFIG_LOG_MAXIMUM_LEVEL=2
CONFIG_BOOTLOADER_LOG_LEVEL=2
'@
Assert-FirmwareBuildModeConfiguration -SdkconfigText $debugConfig -BuildMode Debug
Assert-FirmwareBuildModeConfiguration -SdkconfigText $releaseConfig -BuildMode Release
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
Write-Host 'Build-mode selection and generated-configuration gates passed (no ESP-IDF or hardware access).'
