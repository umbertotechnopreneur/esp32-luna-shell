# The public feature selection controls recognition, dependency admission and flash layout.

# ProjectRoot is the current checkout, never another worktree's generated state.
# Feature is the explicit catalog feature selected by device.ps1.
# Throws for a missing table, ambiguous partition identity or unselected model storage.
function Get-FirmwareFeaturePartitionPath {
    param([string]$ProjectRoot, [string]$Feature)

    $wakeEnabled = $Feature -eq 'wake-word'
    $relativePath = if ($wakeEnabled) { 'config/partitions/wake-word.csv' } else { 'partitions.csv' }
    $partitionPath = Join-Path $ProjectRoot $relativePath
    $names = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $modelCount = 0
    foreach ($line in (Get-Content -LiteralPath $partitionPath -ErrorAction Stop)) {
        if ([string]::IsNullOrWhiteSpace($line) -or $line.TrimStart().StartsWith('#')) {
            continue
        }
        $columns = $line.Split(',') | ForEach-Object { $_.Trim() }
        if ($columns.Count -lt 5 -or -not $names.Add($columns[0])) {
            throw "Feature $Feature rejected malformed or duplicate partition: $line"
        }
        if ($columns[0] -ieq 'model') {
            $modelCount++
            if (-not $wakeEnabled -or $columns[0] -cne 'model' -or
                $columns[1] -cne 'data' -or $columns[4] -cne '512K') {
                throw "Feature $Feature rejected model partition: $line"
            }
        }
    }
    $expectedModelCount = if ($wakeEnabled) { 1 } else { 0 }
    if ($modelCount -ne $expectedModelCount) {
        throw "Feature $Feature requires $expectedModelCount model partitions, found $modelCount."
    }
    return $partitionPath
}

# Variant is the selected catalog declaration, after board module admission.
# Feature is the explicit feature controlling both roots and generated defaults.
# Throws when a disabled feature admits recognition roots/defaults or an enabled one omits them.
function Assert-WakeWordVariant {
    param([object]$Variant, [string]$Feature)

    $wakeEnabled = $Feature -eq 'wake-word'
    $hasRoot = @($Variant.component_roots) -ccontains 'espressif__esp-sr'
    $hasDefaults = @($Variant.defaults) -ccontains 'config/sdkconfig/v1/features/wake-word.defaults'
    if ($hasRoot -ne $wakeEnabled -or $hasDefaults -ne $wakeEnabled) {
        throw "Feature $Feature disagrees with wake-word component roots or defaults."
    }
    if (-not $wakeEnabled) {
        foreach ($root in @($Variant.component_roots)) {
            if ($root -match '^(espressif__)?(esp-sr|esp-dsp|dl_fft)$') {
                throw "Disabled wake word rejected recognition component root: $root"
            }
        }
    }
}

# SdkconfigText is the actual generated configuration, not merely requested defaults.
# ProjectRoot is the checkout owning the reviewed partition table and model.
# Feature is the explicit selector; a mismatched cached configuration is rejected.
# Throws for leaked engine/model selection, missing PSRAM capabilities or renderer-core overlap.
function Assert-WakeWordConfiguration {
    param([string]$SdkconfigText, [string]$ProjectRoot, [string]$Feature)

    $wakeEnabled = $Feature -eq 'wake-word'
    $expectedFlags = @{
        LUNASHELL_BUILD_ENABLE_WAKE_WORD = $wakeEnabled
        LUNASHELL_BUILD_ENABLE_VOICE_AFE = $wakeEnabled
        LUNASHELL_BUILD_WAKE_WORD_SOPHIA = $wakeEnabled
        LUNASHELL_BUILD_WAKE_WORD_STANDBY_LISTENING = $wakeEnabled
    }
    if ($wakeEnabled) {
        $expectedFlags.LUNASHELL_BUILD_ENABLE_POWER_INPUTS = $true
        $expectedFlags.LUNASHELL_BUILD_VOICE_CAPTURE_AVAILABLE = $true
        $expectedFlags.LUNASHELL_BUILD_VOICE_ENGINE_AVAILABLE = $true
        $expectedFlags.MODEL_IN_FLASH = $true
        $expectedFlags.FREERTOS_UNICORE = $false
        $expectedFlags.SPIRAM = $true
        $expectedFlags.SPIRAM_BOOT_INIT = $true
        $expectedFlags.SPIRAM_USE_MALLOC = $true
        $expectedFlags.SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY = $true
        $expectedFlags.FREERTOS_SUPPORT_STATIC_ALLOCATION = $true
        $expectedFlags.FREERTOS_TASK_CREATE_ALLOW_EXT_MEM = $true
    }
    foreach ($symbol in $expectedFlags.Keys) {
        $actual = [regex]::IsMatch($SdkconfigText, "(?m)^CONFIG_$symbol=y`r?$")
        if ($actual -ne $expectedFlags[$symbol]) {
            throw "Feature $Feature rejected sdkconfig: $symbol expected=$($expectedFlags[$symbol]) actual=$actual"
        }
    }
    $partitionPath = Get-FirmwareFeaturePartitionPath -ProjectRoot $ProjectRoot -Feature $Feature
    $relativePartition = [IO.Path]::GetRelativePath($ProjectRoot, $partitionPath).Replace('\', '/')
    $tableMatch = [regex]::Match($SdkconfigText, '(?m)^CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="([^"]+)"\r?$')
    if (-not $tableMatch.Success -or $tableMatch.Groups[1].Value.Replace('\', '/') -cne $relativePartition) {
        throw "Feature $Feature requires partition table $relativePartition."
    }
    $modelMatch = [regex]::Match($SdkconfigText, '(?m)^CONFIG_ESP_SR_EXTERNAL_MODEL_PATH="([^"]*)"\r?$')
    if (-not $wakeEnabled) {
        if ($modelMatch.Success -and $modelMatch.Groups[1].Value.Length -gt 0) {
            throw 'Disabled wake word rejected an external recognition model path.'
        }
        if ([regex]::IsMatch($SdkconfigText, '(?m)^CONFIG_(MODEL_IN_FLASH|SR_[A-Z0-9_]+)=y\r?$')) {
            throw 'Disabled wake word rejected ESP-SR model/algorithm configuration.'
        }
        return
    }
    $expectedModelPath = (Join-Path $ProjectRoot 'assets/wake-word/models').Replace('\', '/')
    if (-not [regex]::IsMatch($SdkconfigText, '(?m)^CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0\r?$')) {
        throw 'Wake word requires ordinary heap allocations to prefer PSRAM.'
    }
    $internalReserve = [regex]::Match($SdkconfigText, '(?m)^CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=(\d+)\r?$')
    if (-not $internalReserve.Success -or [int]$internalReserve.Groups[1].Value -lt 65536) {
        throw 'Wake word requires at least 64 KiB reserved internal RAM for DMA and internal-only allocations.'
    }
    if (-not $modelMatch.Success -or $modelMatch.Groups[1].Value -cne $expectedModelPath) {
        throw 'Wake word requires this checkout''s reviewed external model directory.'
    }
    if ([regex]::IsMatch($SdkconfigText, '(?m)^CONFIG_(SR_WN_[A-Z0-9_]+|SR_MN_(CN|EN)_(?!NONE)[A-Z0-9_]+|SR_NSN_NSNET[A-Z0-9_]*|SR_VADN_VADNET[A-Z0-9_]*)=y\r?$')) {
        throw 'Wake word rejects additional stock recognition models.'
    }
    if ([regex]::IsMatch($SdkconfigText, '(?m)^CONFIG_LUNASHELL_BUILD_ENABLE_UI=y\r?$')) {
        if (-not [regex]::IsMatch($SdkconfigText, '(?m)^CONFIG_BROOKESIA_GUI_LVGL_DISPLAY_SOURCE_TASK_CORE_ID=1\r?$') -or
            -not [regex]::IsMatch($SdkconfigText, '(?m)^CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1\r?$')) {
            throw 'Wake word requires the Luna LVGL renderer on CPU1 with one software draw unit.'
        }
    }
}

# LockText is the resolver's actual YAML dependency closure.
# Feature controls whether the speech engine and its private DSP dependencies are admitted.
# Throws on omitted ESP-SR or speech dependencies leaking into a disabled variant.
function Assert-WakeWordDependencyLock {
    param([string]$LockText, [string]$Feature)

    $wakeEnabled = $Feature -eq 'wake-word'
    $hasEngine = [regex]::IsMatch($LockText, '(?m)^  espressif/esp-sr:\r?$')
    if ($hasEngine -ne $wakeEnabled) {
        throw "Feature $Feature disagrees with ESP-SR in the resolved dependency lock."
    }
    if (-not $wakeEnabled -and
        [regex]::IsMatch($LockText, '(?m)^  espressif/(esp-dsp|dl_fft):\r?$')) {
        throw 'Disabled wake word rejected speech DSP dependencies in the resolved lock.'
    }
}

# ProjectRoot owns the versioned model and provenance, not a runtime download.
# Throws on any extra, missing, renamed or modified weight before the upstream packer runs.
function Get-ReviewedWakeWordModelDirectory {
    param([string]$ProjectRoot)

    $modelRoot = Join-Path $ProjectRoot 'assets/wake-word/models'
    $directories = @(Get-ChildItem -LiteralPath $modelRoot -Directory -Force)
    $unexpectedFiles = @(Get-ChildItem -LiteralPath $modelRoot -File -Force)
    if ($directories.Count -ne 1 -or $directories[0].Name -cne 'wn9_sophia_tts_v1' -or
        $unexpectedFiles.Count -ne 0) {
        throw 'Wake-word packer input must contain only wn9_sophia_tts_v1.'
    }
    $source = Get-Content -Raw -LiteralPath (Join-Path $ProjectRoot 'assets/wake-word/source.json') |
        ConvertFrom-Json -Depth 10
    if ($source.model -cne 'wn9_sophia_tts_v1' -or $source.wake_word -cne 'Sophia' -or
        $source.commit -cne 'cfdbb52172d4f1b3e5f7469d6c3c6e5dd0738143') {
        throw 'Wake-word source identity disagrees with the reviewed Sophia model.'
    }
    $modelDirectory = Join-Path $modelRoot 'wn9_sophia_tts_v1'
    $entries = @(Get-ChildItem -LiteralPath $modelDirectory -Force)
    if ($entries.Count -ne 3 -or @($source.files.PSObject.Properties).Count -ne 3) {
        throw 'Wake-word model must contain exactly its three reviewed weight/metadata files.'
    }
    foreach ($name in @('_MODEL_INFO_', 'wn9_index', 'wn9_data')) {
        $metadata = $source.files.PSObject.Properties[$name]
        $file = Get-Item -LiteralPath (Join-Path $modelDirectory $name) -ErrorAction Stop
        if ($null -eq $metadata -or $file.PSIsContainer -or $file.Length -ne $metadata.Value.bytes) {
            throw "Wake-word weight identity or length rejected: $name"
        }
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        if ($hash -cne ([string]$metadata.Value.sha256).ToUpperInvariant()) {
            throw "Wake-word weight provenance rejected: $name"
        }
    }
    return $modelRoot.Replace('\', '/')
}

# BuildDirectory owns the binaries produced by the selected firmware variant.
# Feature determines whether the normal flash target must include exactly one model image.
# Throws for omitted, leaked or missing model images before opening a serial port.
function Assert-WakeWordFlashImages {
    param([string]$BuildDirectory, [string]$Feature)

    $flashArguments = Get-Content -Raw -LiteralPath (Join-Path $BuildDirectory 'flasher_args.json') |
        ConvertFrom-Json -Depth 20
    if ($null -eq $flashArguments.flash_files) {
        throw 'Flash image inventory is missing; refusing to infer model delivery.'
    }
    $modelImages = @($flashArguments.flash_files.PSObject.Properties |
        Where-Object { [IO.Path]::GetFileName([string]$_.Value) -eq 'srmodels.bin' })
    $expectedCount = if ($Feature -eq 'wake-word') { 1 } else { 0 }
    if ($modelImages.Count -ne $expectedCount) {
        throw "Feature $Feature requires $expectedCount packed model images, found $($modelImages.Count)."
    }
    foreach ($entry in $modelImages) {
        $buildRoot = [IO.Path]::GetFullPath($BuildDirectory)
        $imagePath = [IO.Path]::GetFullPath([string]$entry.Value, $buildRoot)
        if (-not $imagePath.StartsWith($buildRoot + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "Packed wake-word flash image must belong to this build: $imagePath"
        }
        if (-not (Test-Path -LiteralPath $imagePath -PathType Leaf)) {
            throw "Packed wake-word flash image is missing: $imagePath"
        }
        $imageSize = (Get-Item -LiteralPath $imagePath).Length
        if ($imageSize -le 0 -or $imageSize -gt 524288) {
            throw "Packed wake-word flash image exceeds its 512 KiB partition or is empty: $imageSize bytes"
        }
    }
}
