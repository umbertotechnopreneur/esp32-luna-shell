[CmdletBinding()]
# Route the common build, flash, and monitor workflows through one project-aware entrypoint.
param(
    [Parameter(Position = 0)]
    [ValidateSet("build", "clean", "fullclean", "flash", "reset", "monitor", "capture", "erase", "reconfigure", "set-target", "update-lock", "size", "combo", "ports", "discovery", "port-status", "recover")]
    [string]$Action = "build",

    [string]$Port = "COM8",

    [int]$Baud = 115200,

    [string]$BuildDir,

    [ValidateSet("full", "connected", "ui-minimal", "display-touch", "headless")]
    [string]$Profile = "full",

    [ValidateSet("none", "distance")]
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

    # Optional live tee for callers that cannot surface native process output
    # incrementally. Console output remains visible.
    [string]$LogPath,

    [switch]$ResetBeforeCapture,

    # Explicit parameters take precedence over the dedicated LUNASHELL_IDF_*
    # environment variables and the workstation defaults below.
    [string]$IdfPath,

    [string]$ToolsPath,

    [string]$PythonExe,

    # Limit local compiler fan-out so a firmware validation cannot starve the
    # workstation. CI may opt into a lower value, never a higher one.
    [int]$Jobs = 0
)

$ErrorActionPreference = "Stop"
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

# PowerShell transcripts do not reliably include native CMake/Ninja output.
# Re-run the action once as a child process and tee its complete output stream
# so the console and log file are both updated as each line arrives.
if (-not [string]::IsNullOrWhiteSpace($LogPath)) {
    $LogPath = [System.IO.Path]::GetFullPath($LogPath)
    $logDirectory = [System.IO.Path]::GetDirectoryName($LogPath)
    if (-not (Test-Path -LiteralPath $logDirectory -PathType Container)) {
        throw "Live log directory not found: $logDirectory"
    }

    $childArguments = @("-NoProfile", "-File", $PSCommandPath, $Action)
    foreach ($parameterName in @(
        "Port",
        "Baud",
        "BuildDir",
        "Profile",
        "Feature",
        "BoardConfig",
        "WaitSeconds",
        "DurationSeconds",
        "OutputPath",
        "SizeOutputFormat",
        "SizeOutputFile",
        "ResetBeforeCapture",
        "IdfPath",
        "ToolsPath",
        "PythonExe",
        "Jobs"
    )) {
        if (-not $PSBoundParameters.ContainsKey($parameterName)) {
            continue
        }

        $parameterValue = $PSBoundParameters[$parameterName]
        if ($parameterValue -is [System.Management.Automation.SwitchParameter]) {
            if ($parameterValue.IsPresent) {
                $childArguments += "-$parameterName"
            }
            continue
        }

        $childArguments += "-$parameterName"
        $childArguments += "$parameterValue"
    }

    $pwshExecutable = Join-Path $PSHOME "pwsh.exe"
    & $pwshExecutable @childArguments 2>&1 |
        Tee-Object -FilePath $LogPath -Append
    $childExitCode = $LASTEXITCODE
    if ($childExitCode -ne 0) {
        throw "Device action '$Action' failed with exit code $childExitCode. Live log: $LogPath"
    }
    return
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

$ProjectRoot = Split-Path -Parent $PSScriptRoot

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
        (Join-Path $PSScriptRoot "board_config.py") $BoardConfigPath 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Board configuration rejected: $($boardSelectionOutput -join ' | ')"
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
    if ($moduleBits -ne "0000000") {
        $graphKey = "a$([int]$audioEnabled)i$([int]($BoardModules.imu -eq 'service'))p$([int]($BoardModules.pmu -eq 'service'))"
        $ProfileBuildKey = "ui-minimal-board-$moduleBits"
        $SelectedProfileVariant = $SelectedProfileVariant |
            ConvertTo-Json -Depth 20 | ConvertFrom-Json -Depth 20
        if ($graphKey -ne "a0i0p0") {
            $SelectedProfileVariant.dependency_lock =
                "config/dependency-locks/v1/waveshare-esp32s3-touch-amoled-1.75/ui-board-$graphKey.lock"
        }
        $SelectedProfileVariant.generated_sdkconfig = ".sdkconfig/v1/$ProfileBuildKey"
        $SelectedProfileVariant.default_build_dir = "artifacts/build-$ProfileBuildKey"
        $extraRoots = @()
        if ($audioEnabled) { $extraRoots += "espressif__esp_codec_dev" }
        if ($BoardModules.imu -eq "service") { $extraRoots += "waveshare__qmi8658" }
        if ($BoardModules.pmu -eq "service") { $extraRoots += "cube32esp__xpowerslib"; $extraRoots += "esp_pm" }
        if ($BoardModules.sd -eq "service") { $extraRoots += "fatfs" }
        $SelectedProfileVariant.component_roots = @($SelectedProfileVariant.component_roots) + $extraRoots
    }
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
if ($null -ne $BoardModules -and $moduleBits -ne "0000000") {
    function Get-BoardSdkconfigFlag([string]$Name, [bool]$Enabled) {
        if ($Enabled) { return "CONFIG_$Name=y" }
        return ""
    }
    $overlayDirectory = Join-Path $ProjectRoot "artifacts/board-config"
    $null = New-Item -ItemType Directory -Path $overlayDirectory -Force
    $overlayPath = Join-Path $overlayDirectory "$ProfileBuildKey.defaults"
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
if ($SelectedSdkconfigDefaults.Count -eq 0) {
    throw "Firmware profile variant has no SDKCONFIG defaults: $ProfileBuildKey"
}
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
$ProfileSdkconfigPath = Resolve-ProfileCatalogPath(
    "$($SelectedProfileVariant.generated_sdkconfig)"
)
function Assert-BoardSdkconfig {
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
    $BuildDir = "$($SelectedProfileVariant.default_build_dir)"
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    throw "Firmware profile variant has no default build directory: $ProfileBuildKey"
}
$ArtifactsRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $ProjectRoot "artifacts")
)
$ResolvedBuildDir = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    [System.IO.Path]::GetFullPath($BuildDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot $BuildDir))
}
$RelativeBuildDir = [System.IO.Path]::GetRelativePath(
    $ArtifactsRoot,
    $ResolvedBuildDir
)
if ($RelativeBuildDir -eq "." -or
    $RelativeBuildDir -eq ".." -or
    $RelativeBuildDir.StartsWith("..\", [StringComparison]::Ordinal) -or
    [System.IO.Path]::IsPathRooted($RelativeBuildDir)) {
    throw "Build directory must remain under the repository artifacts root: $ArtifactsRoot"
}
Write-Host "Firmware profile: $ProfileBuildKey"
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
    throw "Impossibile esportare la toolchain ESP-IDF da $IdfPath"
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
    throw "Impossibile determinare la versione ESP-IDF: $($IdfVersionOutput -join ' | ')"
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

$MonitorScript = Join-Path $PSScriptRoot "device.ps1"
$CaptureScript = Join-Path $PSScriptRoot "capture-serial.ps1"
$PortReleaseGraceMilliseconds = 2000

function Get-SerialPorts {
    $openablePorts = @([System.IO.Ports.SerialPort]::GetPortNames() | ForEach-Object { $_.ToUpperInvariant() })

    $ports = Get-PnpDevice -Class Ports -ErrorAction SilentlyContinue |
        Where-Object { $_.FriendlyName -match '\(COM\d+\)' } |
        ForEach-Object {
            $deviceId = $null
            if ($_.InstanceId -match '\((COM\d+)\)') {
                $deviceId = $Matches[1].ToUpperInvariant()
            } elseif ($_.FriendlyName -match '\((COM\d+)\)') {
                $deviceId = $Matches[1].ToUpperInvariant()
            }

            [PSCustomObject]@{
                DeviceID = $deviceId
                FriendlyName = $_.FriendlyName
                InstanceId = $_.InstanceId
                Status = $_.Status
                IsEspressif = $_.InstanceId -match 'VID_303A'
                IsOpenable = $openablePorts -contains $deviceId
            }
        } |
        Where-Object { $_.DeviceID } |
        Sort-Object DeviceID

    return @($ports)
}

function Show-SerialPorts {
    $ports = Get-SerialPorts

    if ($ports.Count -eq 0) {
        Write-Host "No serial ports detected."
        return
    }

    Write-Host "Detected serial ports:"
    $ports | Format-Table DeviceID, FriendlyName, Status, IsOpenable -AutoSize
}

# Validate an explicitly selected COM port without querying USB or PnP state.
function Normalize-PortName([string]$RequestedPort) {
    if ([string]::IsNullOrWhiteSpace($RequestedPort)) {
        throw "Specify the serial port with -Port, for example -Port COM5"
    }

    $normalizedPort = $RequestedPort.Trim().ToUpperInvariant()
    if ($normalizedPort -notmatch '^COM\d+$') {
        throw "Invalid serial port '$RequestedPort'. Use a Windows COM name such as COM5."
    }

    return $normalizedPort
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
function Test-ExclusiveSerialPort([string]$RequestedPort, [int]$ProbeBaud = 115200) {
    $normalizedPort = Normalize-PortName $RequestedPort
    $probe = [System.IO.Ports.SerialPort]::new($normalizedPort, $ProbeBaud)
    $openError = $null
    $isAvailable = $false

    try {
        $probe.Open()
        $isAvailable = $true
    } catch {
        $openError = $_.Exception.Message
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
        IsMissing = -not $isAvailable -and $openError -match
            '(?i)could not find file|cannot find|does not exist|no such file'
        Error = $openError
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

function Wait-ForPort([string]$RequestedPort, [int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $normalizedRequestedPort = $RequestedPort.ToUpperInvariant()

    Write-Host "Waiting for an openable ESP serial port for up to $TimeoutSeconds seconds..."

    while ((Get-Date) -lt $deadline) {
        $ports = Get-SerialPorts
        $requested = @($ports | Where-Object { $_.DeviceID -eq $normalizedRequestedPort -and $_.IsOpenable })
        if ($requested.Count -gt 0) {
            Write-Host "Using requested port $normalizedRequestedPort."
            return $normalizedRequestedPort
        }

        Start-Sleep -Milliseconds 750
    }

    Write-Host "No openable ESP serial port became available."
    Show-SerialPorts
    throw "Timed out waiting for an openable ESP serial port."
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
function Wait-ForExclusiveSerialPort([string]$RequestedPort, [int]$TimeoutSeconds) {
    $RequestedPort = Normalize-PortName $RequestedPort
    $probe = Test-ExclusiveSerialPort $RequestedPort
    if ($probe.IsAvailable) {
        Start-Sleep -Milliseconds 250
        Write-Host "Serial port $RequestedPort is released and ready."
        return
    }
    if ($probe.IsMissing) {
        throw "Serial port $RequestedPort is not present. Reconnect the device before retrying."
    }

    $initialError = $probe.Error
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
            throw "Serial port $RequestedPort disappeared while waiting for release."
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
    throw "Serial port $RequestedPort stayed busy after direct cleanup. $candidateSummary $remainingSummary Last probe error: $lastError"
}

# Reset the target through esptool without changing any flash contents.
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
        throw "Device reset failed on $normalizedPort"
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
        " -BuildDir $RequestedBuildDir"
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
        "-DLUNASHELL_DEPENDENCIES_LOCK=$(Get-WorkingDependencyLockPath $RequestedBuildDir)" `
        "-DIDF_TARGET=esp32s3" `
        reconfigure 2>&1 | ForEach-Object { Write-Host "$_" }
    if ($LASTEXITCODE -ne 0) {
        throw "Profile configuration failed (ESP-IDF exit code $LASTEXITCODE)"
    }
    Assert-BoardSdkconfig
}

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

function Get-SetTargetRecoveryCommand([string]$RequestedBuildDir) {
    $selector = if ($null -ne $BoardConfigPath) {
        "-BoardConfig '$BoardConfig'"
    } else {
        "-Profile $Profile" +
        $(if ($Feature -eq "none") { "" } else { " -Feature $Feature" })
    }
    return (
        "pwsh -NoProfile -File .\scripts\device.ps1 set-target " +
        "$selector -BuildDir $RequestedBuildDir"
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

function Invoke-ProjectBuild([string]$RequestedBuildDir) {
    Assert-SdkconfigDefaultsApplied $RequestedBuildDir
    $buildNinja = Join-Path $RequestedBuildDir "build.ninja"
    $reconfigure = -not (Test-Path $buildNinja)

    if ($reconfigure) {
        Write-Host "Build configuration changed; running ESP-IDF reconfigure."
        & $PythonExe $IdfPy --no-hints `
            -B $RequestedBuildDir `
            "-DSDKCONFIG=$ProfileSdkconfigPath" `
            "-DLUNASHELL_DEPENDENCIES_LOCK=$(Get-WorkingDependencyLockPath $RequestedBuildDir)" `
            reconfigure
        if ($LASTEXITCODE -ne 0) {
            throw "Reconfigure failed"
        }
    } else {
        Write-Host (
            "Using incremental build; Ninja owns its generated CMake input " +
            "dependencies and will reconfigure only when required."
        )
    }

    $parallelJobs = if ($Jobs -gt 0) {
        [Math]::Max(2, [Math]::Min(8, $Jobs))
    } else {
        [Math]::Max(2, [Math]::Min(8, [Environment]::ProcessorCount))
    }
    Write-Host "Building with Ninja parallelism: -j $parallelJobs"
    & ninja -C $RequestedBuildDir -j $parallelJobs all
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed"
    }
    Assert-WorkingDependencyLock $RequestedBuildDir
    Save-SdkconfigDefaultsState $RequestedBuildDir
}

Push-Location $ProjectRoot
try {
    $commonArgs = @(
        "--no-hints",
        "-B",
        $BuildDir,
        "-DSDKCONFIG=$ProfileSdkconfigPath",
        "-DLUNASHELL_DEPENDENCIES_LOCK=$(Get-WorkingDependencyLockPath $BuildDir)"
    )

    switch ($Action) {
        "build" {
            Invoke-ProjectBuild $BuildDir
        }
        "clean" {
            & $PythonExe $IdfPy @commonArgs clean
            if ($LASTEXITCODE -ne 0) {
                throw "Clean failed"
            }
        }
        "fullclean" {
            Invoke-BuildDirectoryClean $BuildDir
        }
        "reconfigure" {
            Assert-SdkconfigDefaultsApplied $BuildDir
            & $PythonExe $IdfPy @commonArgs reconfigure
            if ($LASTEXITCODE -ne 0) {
                throw "Reconfigure failed"
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
                throw "Size report failed"
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
                throw "Erase failed"
            }
        }
        "flash" {
            Assert-SdkconfigDefaultsApplied $BuildDir
            $Port = Normalize-PortName $Port
            Stop-StaleMonitorProcesses $Port
            Wait-ForExclusiveSerialPort $Port $WaitSeconds
            & $PythonExe $IdfPy @commonArgs -p $Port -b $Baud flash
            if ($LASTEXITCODE -ne 0) {
                throw "Flash failed"
            }
            Assert-WorkingDependencyLock $BuildDir
            Save-SdkconfigDefaultsState $BuildDir
        }
        "reset" {
            Invoke-DeviceReset $Port $Baud
        }
        "monitor" {
            $Port = Normalize-PortName $Port
            Stop-StaleMonitorProcesses $Port
            Wait-ForExclusiveSerialPort $Port $WaitSeconds
            & $PythonExe $IdfPy @commonArgs -p $Port monitor
            if ($LASTEXITCODE -ne 0) {
                throw "Monitor failed"
            }
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
            Invoke-ProjectBuild $BuildDir

            $Port = Normalize-PortName $Port
            Stop-StaleMonitorProcesses $Port
            Wait-ForExclusiveSerialPort $Port $WaitSeconds
            & $PythonExe $IdfPy @commonArgs -p $Port -b $Baud flash
            if ($LASTEXITCODE -ne 0) {
                throw "Flash failed"
            }

            $monitorArgs = @(
                "-NoExit",
                "-NoProfile",
                "-ExecutionPolicy", "Bypass",
                "-File", "`"$MonitorScript`"",
                "monitor",
                "-Port", $Port,
                "-BuildDir", $BuildDir,
                "-IdfPath", "`"$IdfPath`"",
                "-ToolsPath", "`"$ToolsPath`"",
                "-PythonExe", "`"$PythonExe`""
            )

            if ($null -ne $BoardConfigPath) {
                $monitorArgs += @("-BoardConfig", "`"$BoardConfigPath`"")
            } else {
                $monitorArgs += @("-Profile", $Profile, "-Feature", $Feature)
            }

            Start-Process -FilePath "pwsh.exe" -ArgumentList $monitorArgs -WorkingDirectory $ProjectRoot
        }
        "recover" {
            Invoke-ProjectBuild $BuildDir

            $Port = Normalize-PortName $Port
            Stop-StaleMonitorProcesses $Port
            $Port = Wait-ForPort $Port $WaitSeconds
            Wait-ForExclusiveSerialPort $Port $WaitSeconds
            & $PythonExe $IdfPy @commonArgs -p $Port -b $Baud flash
            if ($LASTEXITCODE -ne 0) {
                throw "Flash failed"
            }
        }
        default {
            throw "Unsupported action: $Action"
        }
    }
}
finally {
    Pop-Location
}
