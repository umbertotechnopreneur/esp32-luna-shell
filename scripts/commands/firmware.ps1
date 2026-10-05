[CmdletBinding()]
# Internal firmware engine. Public workflows enter through ../device.ps1.
param(
    [Parameter(Position = 0)]
    [ValidateSet("build", "clean", "fullclean", "flash", "reset", "monitor", "capture", "erase", "reconfigure", "set-target", "update-lock", "size", "combo", "ports", "discovery", "port-status", "recover")]
    [string]$Action = "build",

    [string]$Port,

    [int]$Baud = 115200,

    [string]$BuildDir,

    [ValidateSet("full", "connected", "ui-minimal", "display-touch", "headless", "leg-edition", "cody-companion")]
    [string]$Profile = "cody-companion",

    [ValidateSet("Debug", "Release")]
    [string]$BuildMode = "Debug",

    [ValidateSet("none", "distance", "wake-word", "tts-sd", "tts-flash")]
    [string]$Feature = "none",

    # A reviewed board YAML selects its compiler profile before ESP-IDF runs.
    [string]$BoardConfig,

    [int]$WaitSeconds = 30,

    [ValidateRange(1, 86400)]
    [int]$DurationSeconds = 30,

    [string]$OutputPath,

    [ValidateSet("default", "text", "csv", "json2", "tree", "raw")]
    [string]$SizeOutputFormat = "default",

    [string]$SizeOutputFile,

    # Opt in to ESP-IDF resetting the device when the monitor attaches.
    [switch]$ResetBeforeMonitor,

    [switch]$ResetBeforeCapture,

    # Explicit parameters take precedence over the dedicated LUNASHELL_IDF_*
    # environment variables and the workstation defaults below.
    [string]$IdfPath,

    [string]$ToolsPath,

    [string]$PythonExe,

    # Limit local compiler fan-out so a firmware validation cannot starve the
    # workstation. CI may opt into a lower value, never a higher one.
    [ValidateRange(0, 8)][int]$Jobs = 0
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot '../common/SerialPorts.ps1')

# Resolve the user's COM choice before toolchain output, builds or process cleanup.
if ($Action -in @('flash','reset','monitor','capture','erase','combo','port-status','recover')) {
    if ($PSBoundParameters.ContainsKey('Port')) {
        $Port = Normalize-PortName $Port
    } else {
        $Port = Select-SerialPort -RequestedAction $Action
    }
}

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
[System.Console]::InputEncoding = [System.Text.Encoding]::UTF8
[System.Environment]::SetEnvironmentVariable("PYTHONUTF8", "1", "Process")
[System.Environment]::SetEnvironmentVariable("PYTHONIOENCODING", "utf-8", "Process")
[System.Environment]::SetEnvironmentVariable("PYTHONFAULTHANDLER", "1", "Process")

$ProfileBuildKey = if ($Feature -eq "none") {
    $Profile
} else {
    "$Profile-$Feature"
}

# Serial discovery does not need ESP-IDF. Run it before initializing the
# toolchain so a missing device is not obscured by unrelated environment output.
function Show-SerialDiscovery {
    try {
        $presentPorts = @(
            Get-PnpDevice -PresentOnly -Class Ports -ErrorAction Stop |
                Where-Object {
                    $_.FriendlyName -match '\(COM\d+\)' -and
                    ($_.InstanceId -match '^(USB|FTDIBUS)\\')
                }
        )
        $usbEnumerationErrors = @(
            Get-PnpDevice -PresentOnly -Class USB -ErrorAction Stop |
                Where-Object { $_.Status -ne "OK" }
        )
    } catch {
        throw "Unable to discover present USB serial devices: $($_.Exception.Message)"
    }

    $candidates = @(
        foreach ($device in $presentPorts) {
            if ($device.FriendlyName -notmatch '\((COM\d+)\)') {
                continue
            }

            $port = $Matches[1].ToUpperInvariant()
            $serial = [System.IO.Ports.SerialPort]::new($port, 115200)
            $openError = $null
            $isOpenable = $false
            try {
                $serial.Open()
                $isOpenable = $true
            } catch {
                $openError = $_.Exception.Message
            } finally {
                try {
                    if ($serial.IsOpen) {
                        $serial.Close()
                    }
                } finally {
                    $serial.Dispose()
                }
            }

            $isEspressif = $device.InstanceId -match '(?i)VID_303A'
            [PSCustomObject]@{
                Port = $port
                Availability = if ($isOpenable) { "Openable" } else { "Busy" }
                IsEspressif = $isEspressif
                Reason = if ($isEspressif) {
                    "Espressif USB VID_303A"
                } else {
                    "Present USB serial device"
                }
                FriendlyName = $device.FriendlyName
                InstanceId = $device.InstanceId
                ProbeError = $openError
            }
        }
    )

    if ($candidates.Count -eq 0) {
        Write-Host "No present USB serial candidates found."
        if ($usbEnumerationErrors.Count -gt 0) {
            Write-Host "USB enumeration errors (no COM port assigned):"
            $usbEnumerationErrors |
                Format-Table Status, FriendlyName, InstanceId -AutoSize
        }
        return
    }

    Write-Host "Present USB serial candidates:"
    $candidates |
        Sort-Object @{ Expression = "IsEspressif"; Descending = $true }, Port |
        Format-Table Port, Availability, IsEspressif, Reason, FriendlyName -AutoSize

    foreach ($candidate in @($candidates | Where-Object { $_.Availability -ne "Openable" })) {
        Write-Host "$($candidate.Port) probe: $($candidate.ProbeError)"
    }
    Write-Host "Hardware identifiers:"
    $candidates |
        Sort-Object Port |
        Format-Table Port, InstanceId -AutoSize

    if ($usbEnumerationErrors.Count -gt 0) {
        Write-Host "Other USB enumeration errors (no COM port assigned):"
        $usbEnumerationErrors |
            Format-Table Status, FriendlyName, InstanceId -AutoSize
    }
}

if ($Action -eq "discovery") {
    Show-SerialDiscovery
    return
}

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

$BoardConfigPath = $null
$BoardModules = $null
if (-not [string]::IsNullOrWhiteSpace($BoardConfig)) {
    if ($PSBoundParameters.ContainsKey("Profile") -or
        $PSBoundParameters.ContainsKey("Feature")) {
        throw "-BoardConfig selects the profile and cannot be combined with -Profile or -Feature"
    }
    $BoardConfigPath = [System.IO.Path]::GetFullPath(
        $(if ([System.IO.Path]::IsPathRooted($BoardConfig)) {
            $BoardConfig
        } else {
            Join-Path $ProjectRoot $BoardConfig
        })
    )
    if (-not (Test-Path -LiteralPath $BoardConfigPath -PathType Leaf)) {
        throw "Board configuration not found: $BoardConfigPath"
    }
    $boardPython = if ($PythonExe) {
        $PythonExe
    } elseif ($env:LUNASHELL_IDF_PYTHON) {
        $env:LUNASHELL_IDF_PYTHON
    } else {
        $earlyIdfPath = if ($IdfPath) { $IdfPath } elseif ($env:LUNASHELL_IDF_PATH) {
            $env:LUNASHELL_IDF_PATH
        } else { "E:\Espressif\.espressif\v6.0.2\esp-idf" }
        $earlyToolsPath = if ($ToolsPath) { $ToolsPath } elseif ($env:LUNASHELL_IDF_TOOLS_PATH) {
            $env:LUNASHELL_IDF_TOOLS_PATH
        } else { "E:\Espressif" }
        $idfInstallName = Split-Path -Leaf (Split-Path -Parent $earlyIdfPath)
        Join-Path $earlyToolsPath "tools\python\$idfInstallName\venv\Scripts\python.exe"
    }
    if (-not (Test-Path -LiteralPath $boardPython -PathType Leaf)) {
        throw "ESP-IDF Python required for board YAML not found: $boardPython"
    }
    $boardSelectionOutput = @(& $boardPython `
        (Join-Path $ProjectRoot "scripts/common/board_config.py") $BoardConfigPath 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Board configuration rejected (exit code $LASTEXITCODE): $($boardSelectionOutput -join ' | ')"
    }
    try {
        $boardSelection = ($boardSelectionOutput -join "`n") | ConvertFrom-Json
    } catch {
        throw "Board configuration selector returned invalid JSON: $($_.Exception.Message)"
    }
    $Profile = [string]$boardSelection.profile
    $Feature = [string]$boardSelection.feature
    $BoardModules = $boardSelection.modules
    $ProfileBuildKey = if ($Feature -eq "none") { $Profile } else { "$Profile-$Feature" }
}

$profileCatalogPath = Join-Path $ProjectRoot "config/sdkconfig/v1/catalog.json"
try {
    $profileCatalog = Get-Content -Raw -LiteralPath $profileCatalogPath |
        ConvertFrom-Json -Depth 20
} catch {
    throw "Firmware profile catalog is invalid: $($_.Exception.Message)"
}
if ($profileCatalog.schema_version -ne 1) {
    throw "Unsupported firmware profile catalog schema: $($profileCatalog.schema_version)"
}
. (Join-Path $PSScriptRoot '../common/BuildMode.ps1')
. (Join-Path $PSScriptRoot '../common/TtsSelection.ps1')
$modeSelection = Resolve-FirmwareBuildMode -Catalog $profileCatalog `
    -BuildMode $BuildMode -ExplicitSelection ($PSBoundParameters.ContainsKey('BuildMode'))
$BuildMode = $modeSelection.Name
if ($null -ne $BoardConfigPath -and $profileCatalog.board -cne $boardSelection.board) {
    throw "Board YAML and firmware profile catalog select different boards"
}
$variantProperty = $profileCatalog.variants.PSObject.Properties[$ProfileBuildKey]
if ($null -eq $variantProperty) {
    throw "Unsupported firmware profile/feature variant: $ProfileBuildKey"
}
$SelectedProfileVariant = $variantProperty.Value
if ($SelectedProfileVariant.profile -cne $Profile -or
    $SelectedProfileVariant.feature -cne $Feature) {
    throw "Firmware profile catalog identity mismatch for $ProfileBuildKey"
}

if ($null -ne $BoardModules) {
    # Only the seven reviewed service switches vary. The bit string isolates
    # sdkconfig/build state; the three dependency bits select a canonical lock.
    $moduleOrder = @("pmu", "battery", "imu", "gnss", "audio_output", "microphones", "sd")
    $moduleBits = ($moduleOrder | ForEach-Object {
        if ($BoardModules.PSObject.Properties[$_].Value -eq "service") { "1" } else { "0" }
    }) -join ""
    $audioEnabled = $BoardModules.audio_output -eq "service" -or
        $BoardModules.microphones -eq "service"
    if ($Profile -eq "ui-minimal" -and $moduleBits -ne "0000000") {
        $graphKey = "a$([int]$audioEnabled)i$([int]($BoardModules.imu -eq 'service'))p$([int]($BoardModules.pmu -eq 'service'))"
        $ProfileBuildKey = "ui-minimal-board-$moduleBits"
        $SelectedProfileVariant = $SelectedProfileVariant |
            ConvertTo-Json -Depth 20 | ConvertFrom-Json -Depth 20
        if ($graphKey -ne "a0i0p0") {
            $SelectedProfileVariant.dependency_lock =
                "config/dependency-locks/v1/waveshare-esp32s3-touch-amoled-1.75/ui-board-$graphKey.lock"
        }
        $SelectedProfileVariant.generated_sdkconfig = "artifacts/sdkconfig/v1/$ProfileBuildKey"
        $SelectedProfileVariant.default_build_dir = "artifacts/build-$ProfileBuildKey"
    }
    # The validated YAML adapter owns service dependency closure for every supported profile.
    if ($boardSelection.component_roots -isnot [array]) {
        throw 'Board YAML adapter must declare its module component roots.'
    }
    $SelectedProfileVariant.component_roots = @(
        (@($SelectedProfileVariant.component_roots) + @($boardSelection.component_roots)) |
            Select-Object -Unique
    )
    $env:LUNASHELL_BOARD_ENABLE_AUDIO = if ($audioEnabled) { "1" } else { "0" }
    $env:LUNASHELL_BOARD_ENABLE_SD = if ($BoardModules.sd -eq "service") { "1" } else { "0" }
} else {
    Remove-Item Env:LUNASHELL_BOARD_ENABLE_AUDIO -ErrorAction SilentlyContinue
    Remove-Item Env:LUNASHELL_BOARD_ENABLE_SD -ErrorAction SilentlyContinue
}

function Resolve-ProfileCatalogPath([string]$RelativePath) {
    if ([string]::IsNullOrWhiteSpace($RelativePath) -or
        [System.IO.Path]::IsPathRooted($RelativePath)) {
        throw "Firmware profile catalog path must be repository-relative: $RelativePath"
    }
    $resolvedPath = [System.IO.Path]::GetFullPath(
        (Join-Path $ProjectRoot $RelativePath)
    )
    $relativeToRoot = [System.IO.Path]::GetRelativePath(
        $ProjectRoot,
        $resolvedPath
    )
    if ($relativeToRoot -eq ".." -or
        $relativeToRoot.StartsWith("..\", [StringComparison]::Ordinal) -or
        [System.IO.Path]::IsPathRooted($relativeToRoot)) {
        throw "Firmware profile catalog path escapes the repository: $RelativePath"
    }
    return $resolvedPath
}

$SelectedSdkconfigDefaults = @(
    $SelectedProfileVariant.defaults |
        ForEach-Object { Resolve-ProfileCatalogPath "$_" }
)
if ($null -ne $BoardModules -and $Profile -eq "ui-minimal" -and
    $moduleBits -ne "0000000") {
    function Get-BoardSdkconfigFlag([string]$Name, [bool]$Enabled) {
        if ($Enabled) { return "CONFIG_$Name=y" }
        return ""
    }
    $overlayDirectory = Resolve-ArtifactPath '' "artifacts/board-config"
    $null = New-Item -ItemType Directory -Path $overlayDirectory -Force
    $overlayPath = Resolve-ArtifactPath (Join-Path $overlayDirectory "$ProfileBuildKey.defaults") ''
    $overlay = @(
        (Get-BoardSdkconfigFlag "LUNASHELL_BUILD_ENABLE_POWER_INPUTS" ($BoardModules.pmu -eq "service"))
        (Get-BoardSdkconfigFlag "LUNASHELL_BUILD_ENABLE_BATTERY" ($BoardModules.battery -eq "service"))
        (Get-BoardSdkconfigFlag "LUNASHELL_BUILD_ENABLE_IMU" ($BoardModules.imu -eq "service"))
        (Get-BoardSdkconfigFlag "LUNASHELL_BUILD_ENABLE_GNSS" ($BoardModules.gnss -eq "service"))
        (Get-BoardSdkconfigFlag "LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE" ($BoardModules.sd -eq "service"))
        (Get-BoardSdkconfigFlag "LUNASHELL_BUILD_ENABLE_AUDIO" $audioEnabled)
    )
    $overlay = @($overlay | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($audioEnabled) {
        $overlay += "CONFIG_LUNASHELL_BUILD_AUDIO_DRIVER_WAVESHARE_ES8311_ES7210=y"
        $overlay += $(if ($BoardModules.audio_output -eq "service") {
            "CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311=y"
        } else { "CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_NONE=y" })
        $overlay += $(if ($BoardModules.microphones -eq "service") {
            "CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_ES7210_DUAL=y"
        } else { "CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE=y" })
        # The codec component defaults many unrelated devices to enabled.
        # Gate its source lists as well as our BSP routes.
        $overlay += "CONFIG_CODEC_ES8311_SUPPORT=$(if ($BoardModules.audio_output -eq 'service') { 'y' } else { 'n' })"
        $overlay += "CONFIG_CODEC_ES7210_SUPPORT=$(if ($BoardModules.microphones -eq 'service') { 'y' } else { 'n' })"
        foreach ($codec in @('ES7243', 'ES7243E', 'ES8156', 'AW88298', 'ES8389',
                             'ES8374', 'ES8388', 'TAS5805M', 'ZL38063', 'CJC8910', 'DUMMY')) {
            $overlay += "CONFIG_CODEC_${codec}_SUPPORT=n"
        }
        $overlay += 'CONFIG_CODEC_DATA_ADC_SUPPORT=n'
    }
    [System.IO.File]::WriteAllLines($overlayPath, $overlay)
    $SelectedSdkconfigDefaults += $overlayPath
}
if ($Feature -eq "wake-word") {
    # ESP-SR's packer resolves this string from its own working directory.
    # Generate an absolute, portable-to-this-checkout path rather than hardcode E:.
    $wakeModelDirectory = (Resolve-ProfileCatalogPath "assets/wake-word/models") -replace '\\', '/'
    $wakeOverlayDirectory = Resolve-ArtifactPath '' "artifacts/wake-word"
    $null = New-Item -ItemType Directory -Path $wakeOverlayDirectory -Force
    $wakeOverlayPath = Join-Path $wakeOverlayDirectory "$ProfileBuildKey.defaults"
    [System.IO.File]::WriteAllLines($wakeOverlayPath, @(
        ('CONFIG_ESP_SR_EXTERNAL_MODEL_PATH="{0}"' -f $wakeModelDirectory)
    ))
    $SelectedSdkconfigDefaults += $wakeOverlayPath
}
if ($SelectedSdkconfigDefaults.Count -eq 0) {
    throw "Firmware profile variant has no SDKCONFIG defaults: $ProfileBuildKey"
}
# Apply mode policy last, independently of board/profile/module selection.
$SelectedSdkconfigDefaults += @(
    $modeSelection.Defaults | ForEach-Object { Resolve-ProfileCatalogPath $_ }
)
foreach ($defaultPath in $SelectedSdkconfigDefaults) {
    if (-not (Test-Path -LiteralPath $defaultPath -PathType Leaf)) {
        throw "Profile SDKCONFIG defaults file not found: $defaultPath"
    }
}
$env:SDKCONFIG_DEFAULTS = $SelectedSdkconfigDefaults -join ';'

$ProfileComponentRoots = @($SelectedProfileVariant.component_roots)
if ($ProfileComponentRoots.Count -eq 0 -or
    $ProfileComponentRoots -notcontains "main") {
    throw "Firmware profile variant must include the main component root: $ProfileBuildKey"
}
$env:LUNASHELL_PROFILE_COMPONENT_ROOTS = $ProfileComponentRoots -join ';'
$env:LUNASHELL_PROFILE_BUILD_KEY = $ProfileBuildKey
$env:LUNASHELL_PROFILE_TTS_BACKEND = if ($Feature -eq "tts-sd") { "sd" } elseif ($Feature -eq "tts-flash") { "flash" } else { "none" }

$ProfileComponentDirectories = @(
    $SelectedProfileVariant.component_dirs |
        ForEach-Object { Resolve-ProfileCatalogPath "$_" }
)
if ($ProfileComponentDirectories.Count -eq 0) {
    throw "Firmware profile variant has no component directories: $ProfileBuildKey"
}
foreach ($componentDirectory in $ProfileComponentDirectories) {
    if (-not (Test-Path -LiteralPath $componentDirectory -PathType Container) -or
        -not (Test-Path -LiteralPath (Join-Path $componentDirectory "CMakeLists.txt") -PathType Leaf)) {
        throw "Profile component directory is invalid: $componentDirectory"
    }
}
$env:LUNASHELL_PROFILE_COMPONENT_DIRS = $ProfileComponentDirectories -join ';'

$ProfileCanonicalDependencyLockPath = Resolve-ProfileCatalogPath(
    "$($SelectedProfileVariant.dependency_lock)"
)
$ProfileSdkconfigPath = Resolve-ArtifactPath(
    "$($SelectedProfileVariant.generated_sdkconfig)-$($modeSelection.ArtifactSuffix)"
)
# No parameters: selected generated configuration must match its explicit feature and board.
# Throws before build/size/flash if the selected graph or generated settings disagree.
function Assert-BoardSdkconfig {
    Assert-FirmwareTtsConfiguration -SdkconfigText (Get-Content -Raw -LiteralPath $ProfileSdkconfigPath) `
        -Feature $Feature -ComponentRoots $ProfileComponentRoots
    if ($null -eq $BoardModules) { return }
    if (-not (Test-Path -LiteralPath $ProfileSdkconfigPath -PathType Leaf)) {
        throw "Board sdkconfig not found: $ProfileSdkconfigPath"
    }
    $sdkconfigText = Get-Content -Raw -LiteralPath $ProfileSdkconfigPath
    $audioSelected = $BoardModules.audio_output -eq "service" -or
        $BoardModules.microphones -eq "service"
    $expected = @{
        LUNASHELL_BUILD_ENABLE_POWER_INPUTS = $BoardModules.pmu -eq "service"
        LUNASHELL_BUILD_ENABLE_BATTERY = $BoardModules.battery -eq "service"
        LUNASHELL_BUILD_ENABLE_IMU = $BoardModules.imu -eq "service"
        LUNASHELL_BUILD_ENABLE_GNSS = $BoardModules.gnss -eq "service"
        LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE = $BoardModules.sd -eq "service"
        LUNASHELL_BUILD_ENABLE_AUDIO = $audioSelected
    }
    if ($audioSelected) {
        $expected.LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311 =
            $BoardModules.audio_output -eq "service"
        $expected.LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_ES7210_DUAL =
            $BoardModules.microphones -eq "service"
        $expected.LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_NONE =
            $BoardModules.audio_output -eq "off"
        $expected.LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE =
            $BoardModules.microphones -eq "off"
        $expected.CODEC_ES8311_SUPPORT = $BoardModules.audio_output -eq "service"
        $expected.CODEC_ES7210_SUPPORT = $BoardModules.microphones -eq "service"
        foreach ($codec in @('ES7243', 'ES7243E', 'ES8156', 'AW88298', 'ES8389',
                             'ES8374', 'ES8388', 'TAS5805M', 'ZL38063', 'CJC8910', 'DUMMY')) {
            $expected["CODEC_${codec}_SUPPORT"] = $false
        }
        $expected.CODEC_DATA_ADC_SUPPORT = $false
    }
    foreach ($symbol in $expected.Keys) {
        $actual = [regex]::IsMatch($sdkconfigText, "(?m)^CONFIG_$symbol=y`r?$")
        if ($actual -ne $expected[$symbol]) {
            throw "Board sdkconfig disagrees with YAML: $symbol expected=$($expected[$symbol]) actual=$actual"
        }
    }
}
$profileSdkconfigDirectory = Split-Path -Parent $ProfileSdkconfigPath
if (-not (Test-Path -LiteralPath $profileSdkconfigDirectory -PathType Container)) {
    $null = New-Item -ItemType Directory -Path $profileSdkconfigDirectory
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = "$($SelectedProfileVariant.default_build_dir)-$($modeSelection.ArtifactSuffix)"
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    throw "Firmware profile variant has no default build directory: $ProfileBuildKey"
}
$ArtifactsRoot = Get-DeviceArtifactsRoot
$ResolvedBuildDir = Resolve-ArtifactPath $BuildDir ''
$BuildDir = $ResolvedBuildDir
Write-Host "Firmware profile: $ProfileBuildKey"
Write-Host "Firmware build mode: $BuildMode"
if ($BuildMode -eq 'Release') {
    if ($Feature -eq 'wake-word') {
        Write-Host 'Release: warnings/errors only, rendering diagnostics disabled; standby voice listening keeps CPU/audio active and disables silicon sleep.'
    } else {
        Write-Host 'Release: warnings/errors only, rendering diagnostics disabled, automatic Light-sleep allowed.'
        Write-Host 'Native USB Serial/JTAG may disconnect during Light-sleep; no flash or device operation is implied by building.'
    }
}
Write-Host "Build directory: $BuildDir"

if (-not $IdfPath) {
    $IdfPath = if ($env:LUNASHELL_IDF_PATH) {
        $env:LUNASHELL_IDF_PATH
    } else {
        "E:\Espressif\.espressif\v6.0.2\esp-idf"
    }
}
if (-not $ToolsPath) {
    $ToolsPath = if ($env:LUNASHELL_IDF_TOOLS_PATH) {
        $env:LUNASHELL_IDF_TOOLS_PATH
    } else {
        "E:\Espressif"
    }
}

if (-not (Test-Path -LiteralPath $IdfPath -PathType Container)) {
    throw "ESP-IDF non trovato in $IdfPath"
}
if (-not (Test-Path -LiteralPath $ToolsPath -PathType Container)) {
    throw "Root degli strumenti Espressif non trovata in $ToolsPath"
}

$IdfPath = (Resolve-Path -LiteralPath $IdfPath).Path
$ToolsPath = (Resolve-Path -LiteralPath $ToolsPath).Path
$IdfPy = Join-Path $IdfPath "tools\idf.py"
$IdfToolsPy = Join-Path $IdfPath "tools\idf_tools.py"
foreach ($requiredIdfFile in @($IdfPy, $IdfToolsPy)) {
    if (-not (Test-Path -LiteralPath $requiredIdfFile -PathType Leaf)) {
        throw "Installazione ESP-IDF incompleta: file non trovato $requiredIdfFile"
    }
}

if (-not $PythonExe) {
    $idfInstallName = Split-Path -Leaf (Split-Path -Parent $IdfPath)
    $PythonExe = Join-Path $ToolsPath "tools\python\$idfInstallName\venv\Scripts\python.exe"
    if ($env:LUNASHELL_IDF_PYTHON) {
        $PythonExe = $env:LUNASHELL_IDF_PYTHON
    }
}
if (-not (Test-Path -LiteralPath $PythonExe -PathType Leaf)) {
    throw "Python ESP-IDF non trovato in $PythonExe"
}
$PythonExe = (Resolve-Path -LiteralPath $PythonExe).Path
$PythonEnvPath = Split-Path -Parent (Split-Path -Parent $PythonExe)

$env:IDF_PATH = $IdfPath
$env:IDF_TOOLS_PATH = $ToolsPath
$env:IDF_PYTHON_ENV_PATH = $PythonEnvPath

# Ask the selected ESP-IDF checkout for its exact supported tool paths instead
# of duplicating CMake, Ninja, debugger, and compiler versions in this script.
$toolExports = & $PythonExe $IdfToolsPy export --format key-value
if ($LASTEXITCODE -ne 0) {
    throw "Impossibile esportare la toolchain ESP-IDF da $IdfPath (exit code $LASTEXITCODE)"
}
foreach ($line in $toolExports) {
    if ($line -notmatch '^([A-Z][A-Z0-9_]*)=(.*)$') {
        continue
    }
    $name = $Matches[1]
    $value = $Matches[2]
    if ($name -eq "PATH") {
        $value = $value.Replace("%PATH%", $env:PATH)
    }
    [System.Environment]::SetEnvironmentVariable($name, $value, "Process")
}

# Parallel ccache processes intermittently exit without diagnostics on this
# Windows workstation; deterministic firmware builds are more valuable here.
$env:IDF_CCACHE_ENABLE = "0"

function Get-ToolCommand([string]$Name) {
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -eq $command) {
        throw "Strumento ESP-IDF richiesto non trovato nel PATH: $Name"
    }
    return $command
}

$CMakeCommand = Get-ToolCommand "cmake"
$NinjaCommand = Get-ToolCommand "ninja"
$ToolchainCommand = Get-ToolCommand "xtensa-esp32s3-elf-gcc"
$IdfVersionOutput = @(& $PythonExe $IdfPy --version 2>&1 | ForEach-Object {
    "$_".Trim()
})
if ($LASTEXITCODE -ne 0) {
    throw "Impossibile determinare la versione ESP-IDF (exit code $LASTEXITCODE): $($IdfVersionOutput -join ' | ')"
}
$IdfVersionLines = @($IdfVersionOutput | Where-Object {
    $_ -match '^ESP-IDF v\d+\.\d+\.\d+(?:\s|$)'
})
if ($IdfVersionLines.Count -ne 1) {
    throw "Output versione ESP-IDF ambiguo: $($IdfVersionOutput -join ' | ')"
}
$IdfVersion = $IdfVersionLines[0]
foreach ($diagnostic in @($IdfVersionOutput | Where-Object {
    $_ -ne $IdfVersion
})) {
    if ($diagnostic) {
        Write-Warning "Diagnostica probe ESP-IDF: $diagnostic"
    }
}
$IdfVersion = @(
    $IdfVersionOutput |
        ForEach-Object { $_.ToString().Trim() } |
        Where-Object { $_ -match '^ESP-IDF v6\.0\.2(?:\s|$)' } |
        Select-Object -First 1
)
if ([string]::IsNullOrWhiteSpace($IdfVersion)) {
    $IdfVersion = @(
        $IdfVersionOutput |
            ForEach-Object { $_.ToString().Trim() } |
            Where-Object { $_ -ne "" } |
            Select-Object -Last 1
    )
}
$PythonVersion = (& $PythonExe --version 2>&1 | ForEach-Object { $_.ToString().Trim() } | Where-Object { $_ -ne "" } | Select-Object -Last 1)
if ($IdfVersion -notmatch '^ESP-IDF v6\.0\.2(?:\s|$)') {
    throw "Versione ESP-IDF non supportata: '$IdfVersion'. Il repository richiede ESP-IDF v6.0.2."
}
if ($PythonVersion -notmatch '^Python 3\.13\.\d+$') {
    throw "Versione Python non supportata: '$PythonVersion'. Il repository richiede Python 3.13.x."
}
$CMakeVersion = (& $CMakeCommand.Source --version | Select-Object -First 1)
$NinjaVersion = (& $NinjaCommand.Source --version | Select-Object -First 1)
$ToolchainVersion = (& $ToolchainCommand.Source --version | Select-Object -First 1)
Write-Host "ESP-IDF: $IdfVersion ($IdfPath)"
Write-Host "Python: $PythonVersion ($PythonExe)"
Write-Host "CMake: $CMakeVersion ($($CMakeCommand.Source))"
Write-Host "Ninja: $NinjaVersion ($($NinjaCommand.Source))"
Write-Host "Toolchain: $ToolchainVersion ($($ToolchainCommand.Source))"

$PortReleaseGraceMilliseconds = 2000

# No parameters: share the present-device inventory used by interactive selection.
# Throws when Windows serial discovery fails; a failed query is never an empty list.
function Get-SerialPorts {
    return @(Get-PresentSerialPorts)
}

# No parameters: display currently present COM identities without opening their handles.
# Throws when Windows serial discovery fails.
function Show-SerialPorts {
    $ports = Get-SerialPorts

    if ($ports.Count -eq 0) {
        Write-Host "No serial ports detected."
        return
    }

    Write-Host "Detected serial ports:"
    $ports | Format-Table DeviceID, FriendlyName, Status -AutoSize
}

# Protect this script and its caller chain from any automatic port cleanup.
function Get-ProtectedProcessIds([object[]]$AllProcesses) {
    $protectedProcessIds = [System.Collections.Generic.HashSet[int]]::new()
    $ancestorId = $PID
    while ($ancestorId -gt 0 -and $protectedProcessIds.Add([int]$ancestorId)) {
        $ancestor = $AllProcesses |
            Where-Object { $_.ProcessId -eq $ancestorId } |
            Select-Object -First 1
        if ($null -eq $ancestor -or $ancestor.ParentProcessId -eq $ancestorId) {
            break
        }
        $ancestorId = [int]$ancestor.ParentProcessId
    }

    return ,$protectedProcessIds
}

# List likely command-line owners; the exclusive open probe remains authoritative.
function Get-SerialPortProcessCandidates(
    [string]$RequestedPort,
    [object[]]$AllProcesses = $null,
    [System.Collections.Generic.HashSet[int]]$ProtectedProcessIds = $null
) {
    $normalizedPort = Normalize-PortName $RequestedPort
    $portPattern = [regex]::new("(?i)(?<![A-Z0-9])$([regex]::Escape($normalizedPort))(?![0-9])")
    $knownSerialToolPattern = [regex]::new(
        "(?i)(device\.ps1|capture-serial\.ps1|idf\.py|idf_monitor|esp_idf_monitor|esptool|pyserial|putty|plink|teraterm|ttermpro|arduino|platformio|serial)"
    )

    if ($null -eq $AllProcesses) {
        try {
            $AllProcesses = @(Get-CimInstance Win32_Process -ErrorAction Stop)
        } catch {
            Write-Warning "Unable to inspect process command lines while checking ${normalizedPort}: $($_.Exception.Message)"
            return @()
        }
    }
    if ($null -eq $ProtectedProcessIds) {
        $ProtectedProcessIds = Get-ProtectedProcessIds $AllProcesses
    }

    return @(
        $AllProcesses |
            Where-Object {
                -not $ProtectedProcessIds.Contains([int]$_.ProcessId) -and
                $_.CommandLine -and
                $portPattern.IsMatch($_.CommandLine)
            } |
            Sort-Object ProcessId |
            ForEach-Object {
                $detection = if ($knownSerialToolPattern.IsMatch($_.CommandLine)) {
                    "COM name in serial-tool command line"
                } else {
                    "COM name in process command line"
                }
                [PSCustomObject]@{
                    ProcessId = $_.ProcessId
                    ParentProcessId = $_.ParentProcessId
                    Name = $_.Name
                    CommandLine = $_.CommandLine
                    Detection = $detection
                }
            }
    )
}

function Format-SerialPortProcessCandidates([object[]]$Candidates) {
    if ($Candidates.Count -eq 0) {
        return "no command-line process candidates found"
    }

    return ($Candidates | ForEach-Object {
        "$($_.ProcessId) $($_.Name) [$($_.Detection)]"
    }) -join "; "
}

function Get-ProcessTreeIds(
    [int[]]$RootProcessIds,
    [object[]]$AllProcesses,
    [System.Collections.Generic.HashSet[int]]$ProtectedProcessIds
) {
    $treeIds = [System.Collections.Generic.HashSet[int]]::new()
    $queue = [System.Collections.Generic.Queue[int]]::new()

    foreach ($rootProcessId in $RootProcessIds) {
        if (-not $ProtectedProcessIds.Contains([int]$rootProcessId) -and $treeIds.Add([int]$rootProcessId)) {
            $queue.Enqueue([int]$rootProcessId)
        }
    }

    while ($queue.Count -gt 0) {
        $parentProcessId = $queue.Dequeue()
        foreach ($child in @($AllProcesses | Where-Object { $_.ParentProcessId -eq $parentProcessId })) {
            $childProcessId = [int]$child.ProcessId
            if ($ProtectedProcessIds.Contains($childProcessId)) {
                continue
            }
            if ($treeIds.Add($childProcessId)) {
                $queue.Enqueue($childProcessId)
            }
        }
    }

    return @($treeIds)
}

function Stop-ProcessTreeById(
    [int[]]$RootProcessIds,
    [object[]]$AllProcesses,
    [System.Collections.Generic.HashSet[int]]$ProtectedProcessIds,
    [string]$Reason
) {
    $treeIds = @(Get-ProcessTreeIds $RootProcessIds $AllProcesses $ProtectedProcessIds)
    $treeProcesses = @(
        foreach ($treeId in $treeIds) {
            $AllProcesses | Where-Object { $_.ProcessId -eq $treeId } | Select-Object -First 1
        }
    ) | Where-Object { $null -ne $_ } | Sort-Object ProcessId -Descending

    foreach ($process in $treeProcesses) {
        $processId = [int]$process.ProcessId
        try {
            Stop-Process -Id $processId -Force -ErrorAction Stop
            Write-Host "Stopped process $processId ($($process.Name)): $Reason."
        } catch {
            if (Get-Process -Id $processId -ErrorAction SilentlyContinue) {
                throw "Could not stop process $processId ($($process.Name)) while clearing the serial port: $($_.Exception.Message)"
            }
        }
    }

    foreach ($process in $treeProcesses) {
        $processId = [int]$process.ProcessId
        if (Get-Process -Id $processId -ErrorAction SilentlyContinue) {
            try {
                Wait-Process -Id $processId -Timeout 2 -ErrorAction Stop
            } catch {
                if (Get-Process -Id $processId -ErrorAction SilentlyContinue) {
                    throw "Process $processId ($($process.Name)) did not exit after being stopped."
                }
            }
        }
    }
}

# Probe the Windows handle exclusively and always release it before returning.
# RequestedPort is the COM identity to probe without retaining its handle.
# ProbeBaud is the baud rate used only for the temporary open attempt.
# Throws for invalid parameters or handle cleanup failure; an open failure is returned with its real error.
function Test-ExclusiveSerialPort([string]$RequestedPort, [int]$ProbeBaud = 115200) {
    $normalizedPort = Normalize-PortName $RequestedPort
    $probe = [System.IO.Ports.SerialPort]::new($normalizedPort, $ProbeBaud)
    $openError = $null
    $isAvailable = $false
    $isMissing = $false
    $openFailure = $null

    try {
        $probe.Open()
        $isAvailable = $true
    } catch {
        $openFailure = $_
        $serialException = $_.Exception
        while ($null -ne $serialException.InnerException) {
            $serialException = $serialException.InnerException
        }
        $openError = $serialException.Message
        # Windows file/path-not-found codes do not depend on the system language.
        # Preserve other failures instead of guessing that their text means absence.
        $nativeErrorCode = $serialException.HResult -band 0xFFFF
        $isMissing = ($serialException -is [System.IO.IOException]) -and
            ($nativeErrorCode -in @(2, 3))
    } finally {
        try {
            if ($probe.IsOpen) {
                $probe.Close()
            }
        } finally {
            $probe.Dispose()
        }
    }

    return [PSCustomObject]@{
        Port = $normalizedPort
        IsAvailable = $isAvailable
        IsMissing = $isMissing
        Error = $openError
        ErrorRecord = $openFailure
    }
}

# Report availability and likely holders without stopping any external process.
function Show-PortStatus([string]$RequestedPort) {
    $normalizedPort = Normalize-PortName $RequestedPort
    $probe = Test-ExclusiveSerialPort $normalizedPort
    $candidates = @(Get-SerialPortProcessCandidates $normalizedPort)

    Write-Host "Serial port: $normalizedPort"
    if ($probe.IsAvailable) {
        Write-Host "Availability: released and exclusively openable."
    } else {
        Write-Host "Availability: unavailable or busy. Probe error: $($probe.Error)"
    }

    if ($candidates.Count -eq 0) {
        Write-Host "Likely holders: none found from process command lines (this is not proof that no handle exists)."
    } else {
        Write-Host "Likely holders (advisory command-line match):"
        $candidates | Format-Table ProcessId, Name, Detection, CommandLine -AutoSize
    }

    if (-not $probe.IsAvailable) {
        throw "Serial port $normalizedPort is not exclusively openable. Close the reported process or reconnect the device, then retry."
    }
}

# RequestedPort is the COM identity that must return after a device reconnect.
# TimeoutSeconds bounds the Windows inventory wait before the exclusive-open probe.
# Throws for failed discovery or when the requested healthy device does not return.
function Wait-ForPort([string]$RequestedPort, [int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $normalizedRequestedPort = Normalize-PortName $RequestedPort

    Write-Host "Waiting for serial port $normalizedRequestedPort for up to $TimeoutSeconds seconds..."

    while ((Get-Date) -lt $deadline) {
        $ports = Get-SerialPorts
        $requested = @($ports | Where-Object { $_.DeviceID -eq $normalizedRequestedPort -and $_.Status -eq 'OK' })
        if ($requested.Count -gt 0) {
            Write-Host "Using requested port $normalizedRequestedPort."
            return $normalizedRequestedPort
        }

        Start-Sleep -Milliseconds 750
    }

    Write-Host "Serial port $normalizedRequestedPort did not become present and healthy."
    Show-SerialPorts
    throw "Timed out after $TimeoutSeconds seconds waiting for serial port $normalizedRequestedPort."
}

# Stop only known stale monitor processes, never this script or its callers.
function Stop-StaleMonitorProcesses([string]$RequestedPort) {
    $normalizedRequestedPort = $RequestedPort.ToUpperInvariant()
    $patterns = @(
        [regex]::new([regex]::Escape("idf.py -B $BuildDir -p $normalizedRequestedPort monitor")),
        [regex]::new([regex]::Escape("idf_monitor.py -p $normalizedRequestedPort")),
        [regex]::new([regex]::Escape("esp_idf_monitor -p $normalizedRequestedPort")),
        [regex]::new("(?i)pyserial.*$normalizedRequestedPort")
    )

    $genericMonitorPattern = [regex]::new(
        "(?i)(device\.ps1.*(?:monitor|capture).*-Port\s+$normalizedRequestedPort|capture-serial\.ps1.*-Port\s+$normalizedRequestedPort|idf\.py.*-p\s+$normalizedRequestedPort.*monitor|idf_monitor\.py.*-p\s+$normalizedRequestedPort|esp_idf_monitor.*-p\s+$normalizedRequestedPort)"
    )

    $allProcesses = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue)
    $protectedProcessIds = Get-ProtectedProcessIds $allProcesses

    $toStop = $allProcesses |
        Where-Object {
            -not $protectedProcessIds.Contains([int]$_.ProcessId) -and
            $_.CommandLine -and
            ($genericMonitorPattern.IsMatch($_.CommandLine) -or
             @($patterns | Where-Object { $_.IsMatch($_.CommandLine) }).Count -gt 0)
        } |
        Sort-Object ProcessId -Descending

    if (@($toStop).Count -eq 0) {
        return
    }

    Stop-ProcessTreeById `
        -RootProcessIds @($toStop | ForEach-Object { [int]$_.ProcessId }) `
        -AllProcesses $allProcesses `
        -ProtectedProcessIds $protectedProcessIds `
        -Reason "stale serial monitor for $normalizedRequestedPort"
}

function Stop-SerialPortOwnerProcesses([string]$RequestedPort) {
    $normalizedRequestedPort = Normalize-PortName $RequestedPort
    try {
        $allProcesses = @(Get-CimInstance Win32_Process -ErrorAction Stop)
    } catch {
        throw "Serial port $normalizedRequestedPort is busy, and process inspection failed: $($_.Exception.Message)"
    }

    $protectedProcessIds = Get-ProtectedProcessIds $allProcesses
    $candidates = @(Get-SerialPortProcessCandidates `
        -RequestedPort $normalizedRequestedPort `
        -AllProcesses $allProcesses `
        -ProtectedProcessIds $protectedProcessIds)

    if ($candidates.Count -eq 0) {
        return @()
    }

    Write-Host "Serial port $normalizedRequestedPort is busy; stopping owner candidates: $(Format-SerialPortProcessCandidates $candidates)"
    Stop-ProcessTreeById `
        -RootProcessIds @($candidates | ForEach-Object { [int]$_.ProcessId }) `
        -AllProcesses $allProcesses `
        -ProtectedProcessIds $protectedProcessIds `
        -Reason "serial port $normalizedRequestedPort owner candidate"

    return $candidates
}

# Wait for an exclusive handle. Busy ports are cleared decisively instead of
# burning the full timeout on a known owner.
# RequestedPort is the normalized COM identity to open exclusively.
# TimeoutSeconds bounds the short release wait after known owner cleanup.
# Throws for a missing or busy port and preserves the real serial probe error.
function Wait-ForExclusiveSerialPort([string]$RequestedPort, [int]$TimeoutSeconds) {
    $RequestedPort = Normalize-PortName $RequestedPort
    $probe = Test-ExclusiveSerialPort $RequestedPort
    if ($probe.IsAvailable) {
        Start-Sleep -Milliseconds 250
        Write-Host "Serial port $RequestedPort is released and ready."
        return
    }
    if ($probe.IsMissing) {
        throw [System.InvalidOperationException]::new(
            "Serial port $RequestedPort is not present. Reconnect the device or rerun without -Port to select from detected ports. Probe error: $($probe.Error)",
            $probe.ErrorRecord.Exception)
    }

    $initialError = $probe.Error
    $initialFailure = $probe.ErrorRecord
    $stoppedCandidates = @(Stop-SerialPortOwnerProcesses $RequestedPort)
    $releaseGraceMilliseconds = [Math]::Max(
        200,
        [Math]::Min($PortReleaseGraceMilliseconds, [Math]::Max(0, $TimeoutSeconds) * 1000)
    )
    $deadline = (Get-Date).AddMilliseconds($releaseGraceMilliseconds)

    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 200
        $probe = Test-ExclusiveSerialPort $RequestedPort
        if ($probe.IsAvailable) {
            Write-Host "Serial port $RequestedPort released after owner cleanup."
            return
        }
        if ($probe.IsMissing) {
            throw [System.InvalidOperationException]::new(
                "Serial port $RequestedPort disappeared while waiting for release. Reconnect the device and retry. Probe error: $($probe.Error)",
                $probe.ErrorRecord.Exception)
        }
    }

    $remainingCandidates = @(Get-SerialPortProcessCandidates $RequestedPort)
    $candidateSummary = if ($stoppedCandidates.Count -gt 0) {
        "Stopped candidates: $(Format-SerialPortProcessCandidates $stoppedCandidates)."
    } else {
        "No process command line named $RequestedPort."
    }
    $remainingSummary = "Current candidates: $(Format-SerialPortProcessCandidates $remainingCandidates)."
    $lastError = if ($probe.Error) { $probe.Error } else { $initialError }
    $lastFailure = if ($probe.ErrorRecord) { $probe.ErrorRecord } else { $initialFailure }
    throw [System.InvalidOperationException]::new(
        "Serial port $RequestedPort stayed busy after direct cleanup. $candidateSummary $remainingSummary Last probe error: $lastError",
        $lastFailure.Exception)
}

# Reset the target through esptool without changing any flash contents.
# RequestedPort is the selected Windows COM identity.
# RequestedBaud is the esptool connection baud rate.
# Throws if port preparation or esptool fails, including the native exit code.
function Invoke-DeviceReset([string]$RequestedPort, [int]$RequestedBaud) {
    $normalizedPort = Normalize-PortName $RequestedPort
    Stop-StaleMonitorProcesses $normalizedPort
    Wait-ForExclusiveSerialPort $normalizedPort $WaitSeconds

    & $PythonExe -m esptool `
        --chip esp32s3 `
        --port $normalizedPort `
        --baud $RequestedBaud `
        --before default-reset `
        --after hard-reset `
        chip-id
    if ($LASTEXITCODE -ne 0) {
        throw "Device reset failed on $normalizedPort (exit code $LASTEXITCODE)"
    }
}

# Capture serial output for a bounded interval and always release the port.
function Invoke-SerialCapture([string]$RequestedPort, [int]$RequestedBaud, [int]$Seconds, [string]$Destination) {
    $normalizedPort = Normalize-PortName $RequestedPort
    if ($Seconds -lt 1) {
        throw "Capture duration must be at least one second."
    }

    $serial = [System.IO.Ports.SerialPort]::new(
        $normalizedPort,
        $RequestedBaud,
        [System.IO.Ports.Parity]::None,
        8,
        [System.IO.Ports.StopBits]::One
    )
    $serial.ReadTimeout = 100
    $writer = $null

    try {
        if (-not [string]::IsNullOrWhiteSpace($Destination)) {
            $destinationPath = [System.IO.Path]::GetFullPath($Destination)
            $destinationDirectory = [System.IO.Path]::GetDirectoryName($destinationPath)
            if (-not (Test-Path -LiteralPath $destinationDirectory -PathType Container)) {
                throw "Capture output directory not found: $destinationDirectory"
            }

            $writer = [System.IO.StreamWriter]::new(
                $destinationPath,
                $false,
                [System.Text.UTF8Encoding]::new($false)
            )
        }

        $serial.Open()
        $deadline = (Get-Date).AddSeconds($Seconds)
        while ((Get-Date) -lt $deadline) {
            $chunk = $serial.ReadExisting()
            if (-not [string]::IsNullOrEmpty($chunk)) {
                [Console]::Write($chunk)
                if ($null -ne $writer) {
                    $writer.Write($chunk)
                    $writer.Flush()
                }
            }
            Start-Sleep -Milliseconds 50
        }
    } catch {
        throw "Serial capture failed on $normalizedPort`: $($_.Exception.Message)"
    } finally {
        if ($serial.IsOpen) {
            $serial.Close()
        }
        $serial.Dispose()
        if ($null -ne $writer) {
            $writer.Dispose()
        }
    }
}

function Get-BuildDirectoryPath([string]$RequestedBuildDir) {
    if ([System.IO.Path]::IsPathRooted($RequestedBuildDir)) {
        return [System.IO.Path]::GetFullPath($RequestedBuildDir)
    }
    return [System.IO.Path]::GetFullPath(
        (Join-Path $ProjectRoot $RequestedBuildDir)
    )
}

function Get-WorkingDependencyLockPath([string]$RequestedBuildDir) {
    return Join-Path (Get-BuildDirectoryPath $RequestedBuildDir) "dependencies.lock"
}

# RequestedBuildDir identifies the selected mode's build directory.
function Get-DependencyLockUpdateCommand([string]$RequestedBuildDir) {
    $selector = if ($null -ne $BoardConfigPath) {
        "-BoardConfig '$BoardConfig'"
    } else {
        "-Profile $Profile" +
        $(if ($Feature -eq "none") { "" } else { " -Feature $Feature" })
    }
    return (
        "pwsh -NoProfile -File .\scripts\device.ps1 update-lock " +
        $selector +
        " -BuildMode $BuildMode -BuildDir '" + $RequestedBuildDir.Replace("'", "''") + "'"
    )
}

function Initialize-WorkingDependencyLock(
    [string]$RequestedBuildDir,
    [bool]$AllowMissingCanonical,
    [bool]$RefreshFromResolver = $false
) {
    $buildDirectory = Get-BuildDirectoryPath $RequestedBuildDir
    if (-not (Test-Path -LiteralPath $buildDirectory -PathType Container)) {
        $null = New-Item -ItemType Directory -Path $buildDirectory
    }
    $workingLock = Get-WorkingDependencyLockPath $RequestedBuildDir
    if ($RefreshFromResolver) {
        if (Test-Path -LiteralPath $workingLock -PathType Leaf) {
            Remove-Item -LiteralPath $workingLock -Force
        }
        return
    }
    if (Test-Path -LiteralPath $ProfileCanonicalDependencyLockPath -PathType Leaf) {
        Copy-Item -LiteralPath $ProfileCanonicalDependencyLockPath `
            -Destination $workingLock -Force
        return
    }
    if (-not $AllowMissingCanonical) {
        throw (
            "Canonical dependency lock not found: " +
            "$ProfileCanonicalDependencyLockPath. Run '" +
            (Get-DependencyLockUpdateCommand $RequestedBuildDir) + "'."
        )
    }
    if (Test-Path -LiteralPath $workingLock -PathType Leaf) {
        Remove-Item -LiteralPath $workingLock -Force
    }
}

function Assert-WorkingDependencyLock([string]$RequestedBuildDir) {
    $workingLock = Get-WorkingDependencyLockPath $RequestedBuildDir
    foreach ($requiredLock in @(
        $ProfileCanonicalDependencyLockPath,
        $workingLock
    )) {
        if (-not (Test-Path -LiteralPath $requiredLock -PathType Leaf)) {
            throw (
                "Dependency lock is unverified: $requiredLock. Run '" +
                (Get-DependencyLockUpdateCommand $RequestedBuildDir) + "'."
            )
        }
    }
    $canonicalHash = (
        Get-FileHash -LiteralPath $ProfileCanonicalDependencyLockPath `
            -Algorithm SHA256
    ).Hash
    $workingHash = (
        Get-FileHash -LiteralPath $workingLock -Algorithm SHA256
    ).Hash
    if (-not [string]::Equals(
            $canonicalHash,
            $workingHash,
            [StringComparison]::OrdinalIgnoreCase
        )) {
        throw (
            "Resolved dependencies changed for profile $ProfileBuildKey. " +
            "Review and run '" +
            (Get-DependencyLockUpdateCommand $RequestedBuildDir) + "'."
        )
    }
}

function Publish-WorkingDependencyLock([string]$RequestedBuildDir) {
    $workingLock = Get-WorkingDependencyLockPath $RequestedBuildDir
    if (-not (Test-Path -LiteralPath $workingLock -PathType Leaf)) {
        throw "ESP-IDF did not produce a dependency lock: $workingLock"
    }
    $canonicalDirectory = Split-Path -Parent $ProfileCanonicalDependencyLockPath
    if (-not (Test-Path -LiteralPath $canonicalDirectory -PathType Container)) {
        $null = New-Item -ItemType Directory -Path $canonicalDirectory
    }
    Copy-Item -LiteralPath $workingLock `
        -Destination $ProfileCanonicalDependencyLockPath -Force
    Assert-WorkingDependencyLock $RequestedBuildDir
}

function Invoke-BuildDirectoryClean([string]$RequestedBuildDir) {
    $buildDirectory = [System.IO.Path]::GetFullPath(
        (Get-BuildDirectoryPath $RequestedBuildDir)
    )
    if (-not $buildDirectory.StartsWith(
            ($ArtifactsRoot + [System.IO.Path]::DirectorySeparatorChar),
            [StringComparison]::OrdinalIgnoreCase
        )) {
        throw "Refusing to clean outside the artifacts directory: $buildDirectory"
    }
    # IDF 6.0.2's component-manager extension appends removal of the shared
    # managed_components tree to fullclean. Disable that extension only for
    # the build-directory clean; dependency resolution/checks remain enabled
    # during the following configure and build, with all local patches intact.
    $managerSetting = [Environment]::GetEnvironmentVariable('IDF_COMPONENT_MANAGER', 'Process')
    try {
        $env:IDF_COMPONENT_MANAGER = '0'
        & $PythonExe $IdfPy --no-hints -B $buildDirectory fullclean
        if ($LASTEXITCODE -ne 0) {
            throw "Build directory clean failed (ESP-IDF exit code $LASTEXITCODE)"
        }
    } finally {
        # PowerShell preserves an absent variable when restoring $null; passing
        # it to the .NET string overload can instead create an empty variable.
        $env:IDF_COMPONENT_MANAGER = $managerSetting
    }
}

# RequestedBuildDir identifies the selected mode's build directory.
# RefreshDependencyLock explicitly enables the existing dependency-resolution workflow.
# Throws on configuration, dependency, board, or build-mode policy failure.
function Invoke-ProfileConfiguration(
    [string]$RequestedBuildDir,
    [bool]$RefreshDependencyLock
) {
    # Keep the build directory empty while ESP-IDF validates/removes an old
    # configuration. A dependency lock copied before fullclean makes a fresh
    # directory look unsafe to delete.
    $buildDirectory = Get-BuildDirectoryPath $RequestedBuildDir
    if (Test-Path -LiteralPath $buildDirectory -PathType Container) {
        $buildEntries = @(Get-ChildItem -LiteralPath $buildDirectory -Force)
        if ($buildEntries.Count -gt 0) {
            $cmakeCache = Join-Path $buildDirectory "CMakeCache.txt"
            $ninjaFile = Join-Path $buildDirectory "build.ninja"
            if (-not (Test-Path -LiteralPath $cmakeCache -PathType Leaf) -and
                -not (Test-Path -LiteralPath $ninjaFile -PathType Leaf)) {
                throw (
                    "Refusing to clean non-CMake build directory: " +
                    $buildDirectory
                )
            }
            Invoke-BuildDirectoryClean $RequestedBuildDir
        }
    }

    Initialize-WorkingDependencyLock `
        $RequestedBuildDir `
        $RefreshDependencyLock `
        $RefreshDependencyLock

    if (Test-Path -LiteralPath $ProfileSdkconfigPath -PathType Leaf) {
        Move-Item -LiteralPath $ProfileSdkconfigPath `
            -Destination "$ProfileSdkconfigPath.old" -Force
    }

    Write-Host "Configuring profile '$ProfileBuildKey' with ESP-IDF"
    & $PythonExe $IdfPy --no-hints `
        -B $RequestedBuildDir `
        "-DSDKCONFIG=$ProfileSdkconfigPath" `
        "-DLUNASHELL_BUILD_MODE=$BuildMode" `
        "-DLUNASHELL_DEPENDENCIES_LOCK=$(Get-WorkingDependencyLockPath $RequestedBuildDir)" `
        "-DIDF_TARGET=esp32s3" `
        reconfigure 2>&1 | ForEach-Object { Write-Host "$_" }
    if ($LASTEXITCODE -ne 0) {
        throw "Profile configuration failed (ESP-IDF exit code $LASTEXITCODE)"
    }
    Assert-BoardSdkconfig
    Assert-FirmwareBuildModeConfiguration `
        -SdkconfigText (Get-Content -Raw -LiteralPath $ProfileSdkconfigPath) -BuildMode $BuildMode
}

# No parameters: fingerprint the selected profile, mode, defaults and generated configuration.
# Throws if any required input is missing or disagrees with the selected mode.
function Get-SdkconfigDefaultsState {
    $activeDefaults = [System.Collections.Generic.List[string]]::new()
    foreach ($baseDefault in $SelectedSdkconfigDefaults) {
        $candidate = $baseDefault.Trim()
        if (-not [System.IO.Path]::IsPathRooted($candidate)) {
            $candidate = Join-Path $ProjectRoot $candidate
        }
        $resolvedDefault = [System.IO.Path]::GetFullPath($candidate)
        if (-not (Test-Path -LiteralPath $resolvedDefault -PathType Leaf)) {
            throw "Active SDKCONFIG_DEFAULTS file not found: $resolvedDefault"
        }
        $activeDefaults.Add($resolvedDefault)

        $targetDefault = "$resolvedDefault.esp32s3"
        if (Test-Path -LiteralPath $targetDefault -PathType Leaf) {
            $activeDefaults.Add($targetDefault)
        }
    }

    $signatureEntries = foreach ($defaultPath in $activeDefaults) {
        $normalizedPath = [System.IO.Path]::GetRelativePath(
            $ProjectRoot,
            $defaultPath
        ).Replace('\', '/').ToLowerInvariant()
        $hash = (Get-FileHash -LiteralPath $defaultPath -Algorithm SHA256).Hash
        "$normalizedPath|$($hash.ToLowerInvariant())"
    }
    $generatedSdkconfig = $ProfileSdkconfigPath
    if (-not (Test-Path -LiteralPath $generatedSdkconfig -PathType Leaf)) {
        throw "Generated sdkconfig not found: $generatedSdkconfig"
    }
    Assert-FirmwareTtsConfiguration -SdkconfigText (Get-Content -Raw -LiteralPath $generatedSdkconfig) `
        -Feature $Feature -ComponentRoots $ProfileComponentRoots
    Assert-FirmwareBuildModeConfiguration `
        -SdkconfigText (Get-Content -Raw -LiteralPath $generatedSdkconfig) -BuildMode $BuildMode
    $sdkconfigHash = (
        Get-FileHash -LiteralPath $generatedSdkconfig -Algorithm SHA256
    ).Hash.ToLowerInvariant()
    $catalogHash = (
        Get-FileHash -LiteralPath $profileCatalogPath -Algorithm SHA256
    ).Hash.ToLowerInvariant()
    $boardConfigEntry = if ($null -ne $BoardConfigPath) {
        $boardConfigHash = (
            Get-FileHash -LiteralPath $BoardConfigPath -Algorithm SHA256
        ).Hash.ToLowerInvariant()
        "board_config|$boardConfigHash"
    } else {
        "board_config|none"
    }
    $dependencyLockHash = (
        Get-FileHash -LiteralPath $ProfileCanonicalDependencyLockPath `
            -Algorithm SHA256
    ).Hash.ToLowerInvariant()
    return [pscustomobject]@{
        Files = @($activeDefaults)
        Signature = (
            @(
                "target=esp32s3"
                "schema=1"
                "profile=$Profile"
                "feature=$Feature"
                "build_mode=$BuildMode"
                "catalog|$catalogHash"
                $boardConfigEntry
                "dependency_lock|$dependencyLockHash"
            ) +
            $signatureEntries +
            "sdkconfig|$sdkconfigHash"
        ) -join "`n"
    }
}

function Get-SdkconfigDefaultsStatePath([string]$RequestedBuildDir) {
    $buildDirectory = if (
        [System.IO.Path]::IsPathRooted($RequestedBuildDir)
    ) {
        [System.IO.Path]::GetFullPath($RequestedBuildDir)
    } else {
        [System.IO.Path]::GetFullPath(
            (Join-Path $ProjectRoot $RequestedBuildDir)
        )
    }
    return Join-Path $buildDirectory ".LUNASHELL-sdkconfig-defaults.state"
}

# RequestedBuildDir identifies the selected mode's build directory.
function Get-SetTargetRecoveryCommand([string]$RequestedBuildDir) {
    $selector = if ($null -ne $BoardConfigPath) {
        "-BoardConfig '$BoardConfig'"
    } else {
        "-Profile $Profile" +
        $(if ($Feature -eq "none") { "" } else { " -Feature $Feature" })
    }
    return (
        "pwsh -NoProfile -File .\scripts\device.ps1 set-target " +
        "$selector -BuildMode $BuildMode -BuildDir '" + $RequestedBuildDir.Replace("'", "''") + "'"
    )
}

function Save-SdkconfigDefaultsState([string]$RequestedBuildDir) {
    $state = Get-SdkconfigDefaultsState
    $statePath = Get-SdkconfigDefaultsStatePath $RequestedBuildDir
    $stateDirectory = Split-Path -Parent $statePath
    if (-not (Test-Path -LiteralPath $stateDirectory -PathType Container)) {
        throw "Build directory missing after set-target: $stateDirectory"
    }
    [System.IO.File]::WriteAllText(
        $statePath,
        $state.Signature,
        [System.Text.UTF8Encoding]::new($false)
    )
}

function Assert-SdkconfigDefaultsApplied([string]$RequestedBuildDir) {
    Assert-WorkingDependencyLock $RequestedBuildDir
    $recoveryCommand = Get-SetTargetRecoveryCommand $RequestedBuildDir
    $generatedSdkconfig = $ProfileSdkconfigPath
    $statePath = Get-SdkconfigDefaultsStatePath $RequestedBuildDir
    if (-not (Test-Path -LiteralPath $generatedSdkconfig -PathType Leaf) -or
        -not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
        throw "SDKCONFIG defaults state is unverified; run '$recoveryCommand'."
    }

    $expectedState = Get-SdkconfigDefaultsState
    $appliedSignature = [System.IO.File]::ReadAllText($statePath)
    if (-not [string]::Equals(
            $appliedSignature,
            $expectedState.Signature,
            [StringComparison]::Ordinal
        )) {
        throw (
            "SDKCONFIG, defaults, or active overlays changed; run " +
            "'$recoveryCommand'."
        )
    }
    Assert-BoardSdkconfig
}

# RequestedBuildDir names the selected, verified firmware build under artifacts/.
# Throws on configuration, compilation, lock or signature failure.
function Invoke-ProjectBuild([string]$RequestedBuildDir) {
    Assert-SdkconfigDefaultsApplied $RequestedBuildDir
    $buildNinja = Join-Path $RequestedBuildDir "build.ninja"
    $reconfigure = -not (Test-Path $buildNinja)

    if ($reconfigure) {
        Write-Host "Build configuration changed; running ESP-IDF reconfigure."
        & $PythonExe $IdfPy --no-hints `
            -B $RequestedBuildDir `
            "-DSDKCONFIG=$ProfileSdkconfigPath" `
            "-DLUNASHELL_BUILD_MODE=$BuildMode" `
            "-DLUNASHELL_DEPENDENCIES_LOCK=$(Get-WorkingDependencyLockPath $RequestedBuildDir)" `
            reconfigure
        if ($LASTEXITCODE -ne 0) {
            throw "Reconfigure failed (ESP-IDF exit code $LASTEXITCODE)"
        }
    } else {
        Write-Host (
            "Using incremental build; Ninja owns its generated CMake input " +
            "dependencies and will reconfigure only when required."
        )
    }

    $parallelJobs = if ($Jobs -gt 0) {
        $Jobs
    } else {
        [Math]::Max(1, [Math]::Min(8, [Environment]::ProcessorCount))
    }
    Write-Host "Building with Ninja parallelism: -j $parallelJobs"
    & ninja -C $RequestedBuildDir -j $parallelJobs all
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed (Ninja exit code $LASTEXITCODE)"
    }
    Assert-WorkingDependencyLock $RequestedBuildDir
    Save-SdkconfigDefaultsState $RequestedBuildDir
}

# RequestedBuildDir names the build whose binaries and flash target must remain available.
# WaitForReconnect enables the existing bounded USB recovery wait before opening the port.
# Throws on build, port, flash, lock or SDKCONFIG errors; restores the serial target environment.
function Invoke-ProjectFlash([string]$RequestedBuildDir, [bool]$WaitForReconnect = $false) {
    Invoke-ProjectBuild $RequestedBuildDir
    $flashPort = Normalize-PortName $Port
    Stop-StaleMonitorProcesses $flashPort
    if ($WaitForReconnect) { $flashPort = Wait-ForPort $flashPort $WaitSeconds }
    Wait-ForExclusiveSerialPort $flashPort $WaitSeconds
    $parallelJobs = if ($Jobs -gt 0) { $Jobs } else { [Math]::Max(1, [Math]::Min(8, [Environment]::ProcessorCount)) }
    $environmentBeforeFlash = @{}
    $flashEnvironment = @{ ESPPORT = $flashPort; ESPBAUD = [string]$Baud }
    try {
        foreach ($name in $flashEnvironment.Keys) {
            $environmentBeforeFlash[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
            [Environment]::SetEnvironmentVariable($name, $flashEnvironment[$name], 'Process')
        }
        # IDF 6.0.2 dispatches this same target. An explicit -j also bounds an implicit rebuild.
        & ninja -C $RequestedBuildDir -j $parallelJobs flash
        if ($LASTEXITCODE -ne 0) { throw "Flash failed (exit code $LASTEXITCODE)" }
    } finally {
        foreach ($name in $environmentBeforeFlash.Keys) {
            [Environment]::SetEnvironmentVariable($name, $environmentBeforeFlash[$name], 'Process')
        }
    }
    Assert-WorkingDependencyLock $RequestedBuildDir
    Save-SdkconfigDefaultsState $RequestedBuildDir
}

# RequestedPort is the selected COM identity; ESP-IDF owns its single serial open.
# RequestedBaud is the explicit monitor baud rate, independent of flash or environment defaults.
# TimeoutSeconds bounds the wait for Windows to report the selected port present and healthy.
# ResetOnAttach allows ESP-IDF's startup reset only after the caller explicitly requests it.
# Throws the real ESP-IDF monitor failure with its native exit code; existing owners are never stopped here.
function Invoke-ProjectMonitor([string]$RequestedPort, [int]$RequestedBaud, [int]$TimeoutSeconds, [bool]$ResetOnAttach = $false) {
    $normalizedPort = Normalize-PortName $RequestedPort
    # Windows inventory is safe to wait for here; it never opens or probes the serial handle.
    $normalizedPort = Wait-ForPort -RequestedPort $normalizedPort -TimeoutSeconds $TimeoutSeconds
    $monitorArguments = @('-p', $normalizedPort, 'monitor', '--monitor-baud', [string]$RequestedBaud)
    if ($ResetOnAttach) {
        Write-Host "Monitor: $normalizedPort at $RequestedBaud baud; startup reset explicitly requested."
    } else {
        # A temporary SerialPort probe can toggle control lines too, so let IDF open the port once.
        $monitorArguments += '--no-reset'
        Write-Host "Monitor: $normalizedPort at $RequestedBaud baud; attaching without a startup reset."
    }

    & $PythonExe $IdfPy @commonArgs @monitorArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Monitor failed (ESP-IDF exit code $LASTEXITCODE)"
    }
}

Push-Location $ProjectRoot
try {
    $commonArgs = @(
        "--no-hints",
        "-B",
        $BuildDir,
        "-DSDKCONFIG=$ProfileSdkconfigPath",
        "-DLUNASHELL_BUILD_MODE=$BuildMode",
        "-DLUNASHELL_DEPENDENCIES_LOCK=$(Get-WorkingDependencyLockPath $BuildDir)"
    )

    switch ($Action) {
        "build" {
            Invoke-ProjectBuild $BuildDir
        }
        "clean" {
            & $PythonExe $IdfPy @commonArgs clean
            if ($LASTEXITCODE -ne 0) {
                throw "Clean failed (ESP-IDF exit code $LASTEXITCODE)"
            }
        }
        "fullclean" {
            Invoke-BuildDirectoryClean $BuildDir
        }
        "reconfigure" {
            Assert-SdkconfigDefaultsApplied $BuildDir
            & $PythonExe $IdfPy @commonArgs reconfigure
            if ($LASTEXITCODE -ne 0) {
                throw "Reconfigure failed (ESP-IDF exit code $LASTEXITCODE)"
            }
            Assert-WorkingDependencyLock $BuildDir
            Save-SdkconfigDefaultsState $BuildDir
        }
        "set-target" {
            Invoke-ProfileConfiguration $BuildDir $false
            Assert-WorkingDependencyLock $BuildDir
            Save-SdkconfigDefaultsState $BuildDir
        }
        "update-lock" {
            Invoke-ProfileConfiguration $BuildDir $true
            Publish-WorkingDependencyLock $BuildDir
            Save-SdkconfigDefaultsState $BuildDir
        }
        "size" {
            Assert-SdkconfigDefaultsApplied $BuildDir
            $sizeArguments = @("size", "--format", $SizeOutputFormat)
            if (-not [string]::IsNullOrWhiteSpace($SizeOutputFile)) {
                $resolvedSizeOutputFile = [System.IO.Path]::GetFullPath($SizeOutputFile)
                $sizeOutputDirectory = [System.IO.Path]::GetDirectoryName($resolvedSizeOutputFile)
                if (-not (Test-Path -LiteralPath $sizeOutputDirectory -PathType Container)) {
                    throw "Size output directory not found: $sizeOutputDirectory"
                }
                $sizeArguments += @("--output-file", $resolvedSizeOutputFile)
            }
            & $PythonExe $IdfPy @commonArgs @sizeArguments
            if ($LASTEXITCODE -ne 0) {
                throw "Size report failed (ESP-IDF exit code $LASTEXITCODE)"
            }
            Assert-WorkingDependencyLock $BuildDir
            Save-SdkconfigDefaultsState $BuildDir
        }
        "ports" {
            Show-SerialPorts
        }
        "discovery" {
            Show-SerialDiscovery
        }
        "port-status" {
            Show-PortStatus $Port
        }
        "erase" {
            $Port = Normalize-PortName $Port
            Stop-StaleMonitorProcesses $Port
            Wait-ForExclusiveSerialPort $Port $WaitSeconds
            & $PythonExe $IdfPy @commonArgs -p $Port -b $Baud erase-flash
            if ($LASTEXITCODE -ne 0) {
                throw "Erase failed (ESP-IDF exit code $LASTEXITCODE)"
            }
        }
        "flash" {
            Invoke-ProjectFlash $BuildDir
        }
        "reset" {
            Invoke-DeviceReset $Port $Baud
        }
        "monitor" {
            Invoke-ProjectMonitor -RequestedPort $Port -RequestedBaud $Baud -TimeoutSeconds $WaitSeconds -ResetOnAttach ([bool]$ResetBeforeMonitor)
        }
        "capture" {
            $Port = Normalize-PortName $Port
            Stop-StaleMonitorProcesses $Port
            Wait-ForExclusiveSerialPort $Port $WaitSeconds
            if ($ResetBeforeCapture) {
                Invoke-DeviceReset $Port $Baud
            }
            Invoke-SerialCapture $Port $Baud $DurationSeconds $OutputPath
        }
        "combo" {
            Invoke-ProjectFlash $BuildDir
            # Keep monitor in this terminal and the same artifact operation lock.
            Invoke-ProjectMonitor -RequestedPort $Port -RequestedBaud $Baud -TimeoutSeconds $WaitSeconds -ResetOnAttach ([bool]$ResetBeforeMonitor)
        }
        "recover" {
            Invoke-ProjectFlash $BuildDir $true
        }
        default {
            throw "Unsupported action: $Action"
        }
    }
}
finally {
    Pop-Location
}
