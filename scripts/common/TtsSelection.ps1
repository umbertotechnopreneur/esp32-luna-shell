# SdkconfigText is the generated configuration used by this operation.
# Feature is the explicitly selected catalog feature, never inferred from binaries.
# ComponentRoots are the reviewed discovery roots admitted by that variant.
# Throws for stale/mismatched TTS selection, unsupported language or storage composition.
function Assert-FirmwareTtsConfiguration {
    param(
        [Parameter(Mandatory)][string]$SdkconfigText,
        [Parameter(Mandatory)][string]$Feature,
        [Parameter(Mandatory)][string[]]$ComponentRoots
    )
    $enabled = $Feature -in @('tts-sd', 'tts-flash')
    $sd = $Feature -eq 'tts-sd'
    $flash = $Feature -eq 'tts-flash'
    if (($ComponentRoots -ccontains 'LUNASHELL_tts') -ne $enabled) {
        throw "TTS component graph disagrees with explicit feature '$Feature'."
    }
    $expected = @{
        LUNASHELL_BUILD_ENABLE_TTS = $enabled
        LUNASHELL_TTS_DATA_SD = $sd
        LUNASHELL_TTS_DATA_FLASH = $flash
        LUNASHELL_TTS_LANGUAGE_ENGLISH = $enabled
        LUNASHELL_TTS_LANGUAGE_ITALIAN = $sd
        LUNASHELL_TTS_LANGUAGE_MANDARIN = $sd
        LUNASHELL_TTS_LANGUAGE_CANTONESE = $sd
    }
    if ($enabled) {
        $expected.LUNASHELL_BUILD_ENABLE_AUDIO = $true
        $expected.LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311 = $true
        $expected.SPIRAM = $true
        $expected.SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY = $true
        $expected.FREERTOS_UNICORE = $false
        $expected.FREERTOS_TASK_CREATE_ALLOW_EXT_MEM = $true
        $expected.LUNASHELL_BUILD_ENABLE_VOICE_AFE = $false
        $expected.LUNASHELL_BUILD_ENABLE_WAKE_WORD = $false
        if ($sd) {
            $expected.LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE = $true
            $expected.MBEDTLS_SHA256_C = $true
        }
    }
    $italianBoot = [regex]::IsMatch($SdkconfigText, '(?m)^CONFIG_LUNASHELL_TTS_BOOT_TEST_ITALIAN=y\r?$')
    if ($italianBoot -and -not $sd) {
        throw "Italian boot speech requires the explicitly selected tts-sd feature."
    }
    foreach ($symbol in $expected.Keys) {
        $actual = [regex]::IsMatch($SdkconfigText, "(?m)^CONFIG_$symbol=y`r?$")
        if ($actual -ne $expected[$symbol]) {
            throw "Feature '$Feature' rejected sdkconfig: $symbol expected=$($expected[$symbol]) actual=$actual"
        }
    }
}
