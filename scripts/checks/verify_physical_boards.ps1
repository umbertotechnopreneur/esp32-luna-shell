# Hardware-free regression suite for physical-board selection and flash boundaries.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $ProjectRoot 'scripts/common/WakeWord.ps1')

# Operation: Production guard invoked with a deliberately incompatible configuration.
# Message: Required reason; unrelated failures must fail the suite.
# Throws when the unsafe configuration is admitted or fails for another reason.
function Assert-BoardRejection([scriptblock]$Operation, [string]$Message) {
    try {
        & $Operation | Out-Null
    } catch {
        if (-not $_.Exception.Message.Contains($Message)) { throw }
        return
    }
    throw "Unsafe board configuration was admitted: $Message"
}

# Value: Partition CSV address/length using the supported IDF hexadecimal or K syntax.
# Throws for invalid numeric input.
function Convert-PartitionNumber([string]$Value) {
    if ($Value.EndsWith('K')) { return [long]$Value.TrimEnd('K') * 1024 }
    return [Convert]::ToInt64($Value, $(if ($Value.StartsWith('0x')) { 16 } else { 10 }))
}

foreach ($Board in @('waveshare-esp32s3-touch-amoled-1.75', 'waveshare-esp32s3-touch-amoled-1.75c')) {
    $capacity = if ($Board.EndsWith('1.75c')) { 32MB } else { 16MB }
    foreach ($Feature in @('none', 'wake-word')) {
        $path = Get-FirmwareFeaturePartitionPath -ProjectRoot $ProjectRoot -Feature $Feature -Board $Board
        [long]$end = 0
        $slots = @()
        foreach ($line in (Get-Content -LiteralPath $path)) {
            if ([string]::IsNullOrWhiteSpace($line) -or $line.TrimStart().StartsWith('#')) { continue }
            $fields = $line.Split(',') | ForEach-Object { $_.Trim() }
            $offset = Convert-PartitionNumber $fields[3]
            $size = Convert-PartitionNumber $fields[4]
            if ($offset -lt $end -or $size -le 0 -or $offset % 4096 -ne 0) {
                throw "Overlapping, empty or unaligned flash partition: $line"
            }
            if ($fields[1] -eq 'app') {
                if ($offset % 65536 -ne 0) { throw 'Application slot is not 64-KiB aligned.' }
                $slots += $size
            }
            $end = $offset + $size
            # The bootloader maps app segments and ESP-SR maps its model. IDF's
            # 24-bit quad-flash cache must address the entire half-open range.
            $usesFlashCache = $fields[1] -eq 'app' -or $fields[0] -eq 'model'
            if ($usesFlashCache -and $end -gt 16MB) {
                throw "Partition '$($fields[0])' exceeds the 16 MiB cache range on $Board / $Feature (offset=$($fields[3]), size=$($fields[4]))."
            }
        }
        if ($end -ne $capacity -or $slots.Count -ne 2 -or $slots[0] -ne $slots[1]) {
            throw "Physical flash/OTA geometry disagrees with $Board / $Feature."
        }
        Write-Host "Flash layout passed: $Board / $Feature"
    }
}

# Load the real pre-build guard without running the firmware engine or opening a port.
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $ProjectRoot 'scripts/commands/firmware.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
$guard = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Assert-BoardSdkconfig'
}, $true)
if (-not $guard) { throw 'Production board guard is missing.' }
. ([scriptblock]::Create($guard.Extent.Text))

# SdkconfigText: Irrelevant TTS input; the independent suite does not exercise TTS.
# Feature: Irrelevant optional feature for this board-identity regression.
# ComponentRoots: Unchanged source dependency graph, unused by this focused fixture.
function Assert-FirmwareTtsConfiguration($SdkconfigText, $Feature, $ComponentRoots) {}
$BoardModules = $null
$ActivityClassifier = $false
$ActivityCapture = $false
$Feature = 'none'
$ProfileComponentRoots = @('main')
$testDirectory = Join-Path $ProjectRoot 'artifacts/tests/physical-boards'
[void][IO.Directory]::CreateDirectory($testDirectory)
$ProfileSdkconfigPath = Join-Path $testDirectory 'sdkconfig'
foreach ($Board in @('waveshare-esp32s3-touch-amoled-1.75', 'waveshare-esp32s3-touch-amoled-1.75c')) {
    $suffix = if ($Board.EndsWith('1.75c')) { 'C' } else { '' }
    $capacity = if ($suffix) { '32MB' } else { '16MB' }
    $valid = "CONFIG_LUNASHELL_BUILD_BOARD_PROFILE_WAVESHARE_ESP32S3_TOUCH_AMOLED_1_75$suffix=y`nCONFIG_ESPTOOLPY_FLASHSIZE=`"$capacity`"`n"
    [IO.File]::WriteAllText($ProfileSdkconfigPath, $valid)
    Assert-BoardSdkconfig
    [IO.File]::WriteAllText($ProfileSdkconfigPath, $valid.Replace($capacity, '8MB'))
    Assert-BoardRejection { Assert-BoardSdkconfig } 'disagrees with physical board'
    [IO.File]::WriteAllText($ProfileSdkconfigPath, $valid.Replace("1_75$suffix=y", 'UNKNOWN=y'))
    Assert-BoardRejection { Assert-BoardSdkconfig } 'disagrees with physical board'
    if ($suffix) {
        foreach ($peripheral in @('RTC', 'GNSS', 'REMOVABLE_STORAGE')) {
            [IO.File]::WriteAllText($ProfileSdkconfigPath, $valid + "CONFIG_LUNASHELL_BUILD_ENABLE_$peripheral=y`n")
            Assert-BoardRejection { Assert-BoardSdkconfig } 'physically absent prototype peripheral'
        }
    }
}
Write-Host 'Physical-board guards passed: correct identity/capacity, wrong board/flash and absent peripherals.'
