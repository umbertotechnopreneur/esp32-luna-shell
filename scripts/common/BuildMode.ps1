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

# Firmware mode selection is explicit and independent of the hardware graph.

# Catalog contains the versioned profile and build-mode declarations.
# BuildMode is the requested Debug or Release identity, never inferred from artifacts.
# ExplicitSelection records whether the caller supplied the mode parameter.
# Throws for malformed declarations or an implicit Release selection.
function Resolve-FirmwareBuildMode {
    param(
        [Parameter(Mandatory)][object]$Catalog,
        [ValidateSet('Debug','Release')][string]$BuildMode = 'Debug',
        [bool]$ExplicitSelection = $false
    )

    if ($Catalog.default_build_mode -cne 'Debug') {
        throw 'Firmware build-mode catalog must default to Debug.'
    }
    $modeNames = @($Catalog.build_modes.PSObject.Properties.Name)
    if ($modeNames.Count -ne 2 -or $modeNames -cnotcontains 'Debug' -or
        $modeNames -cnotcontains 'Release') {
        throw 'Firmware build-mode catalog must declare exactly Debug and Release.'
    }
    $canonicalMode = if ($BuildMode -eq 'Release') { 'Release' } else { 'Debug' }
    if ($canonicalMode -eq 'Release' -and -not $ExplicitSelection) {
        throw 'Release requires an explicit -BuildMode Release selection.'
    }
    $declaration = $Catalog.build_modes.PSObject.Properties[$canonicalMode].Value
    if ($declaration.defaults -isnot [array]) {
        throw "Firmware build mode '$canonicalMode' defaults must be an array."
    }
    $defaults = @($declaration.defaults)
    if ($defaults.Count -eq 0) {
        throw "Firmware build mode '$canonicalMode' has no defaults."
    }
    foreach ($path in $defaults) {
        if ($path -isnot [string] -or [string]::IsNullOrWhiteSpace($path)) {
            throw "Firmware build mode '$canonicalMode' has an invalid defaults path."
        }
    }
    return [pscustomobject]@{
        Name = $canonicalMode
        ArtifactSuffix = $canonicalMode.ToLowerInvariant()
        Defaults = $defaults
    }
}

# SdkconfigText is the generated ESP-IDF configuration, read before using its build.
# BuildMode is the explicit mode selected for this command.
# Throws when mode, optimization, logging or debug-only power instrumentation disagrees.
function Assert-FirmwareBuildModeConfiguration {
    param(
        [Parameter(Mandatory)][string]$SdkconfigText,
        [Parameter(Mandatory)][ValidateSet('Debug','Release')][string]$BuildMode
    )

    $release = $BuildMode -eq 'Release'
    $expectedFlags = @{
        LUNASHELL_BUILD_MODE_DEBUG = -not $release
        LUNASHELL_BUILD_MODE_RELEASE = $release
        COMPILER_OPTIMIZATION_SIZE = -not $release
        COMPILER_OPTIMIZATION_PERF = $release
        PM_LIGHT_SLEEP_CALLBACKS = -not $release
        USJ_NO_AUTO_LS_ON_CONNECTION = $false
    }
    foreach ($symbol in $expectedFlags.Keys) {
        $enabled = [regex]::IsMatch($SdkconfigText, "(?m)^CONFIG_$symbol=y`r?$")
        if ($enabled -ne $expectedFlags[$symbol]) {
            throw "Build mode $BuildMode rejected sdkconfig: $symbol expected=$($expectedFlags[$symbol]) actual=$enabled"
        }
    }
    $expectedLevels = @{
        LOG_DEFAULT_LEVEL = $(if ($release) { 2 } else { 3 })
        LOG_MAXIMUM_LEVEL = $(if ($release) { 2 } else { 5 })
        BOOTLOADER_LOG_LEVEL = $(if ($release) { 2 } else { 3 })
    }
    foreach ($symbol in $expectedLevels.Keys) {
        $expected = $expectedLevels[$symbol]
        if (-not [regex]::IsMatch($SdkconfigText, "(?m)^CONFIG_$symbol=$expected`r?$")) {
            throw "Build mode $BuildMode rejected sdkconfig: $symbol must be $expected."
        }
    }
}
