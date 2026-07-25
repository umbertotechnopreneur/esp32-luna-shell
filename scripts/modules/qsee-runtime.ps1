<#
.SYNOPSIS
Shared, passive runtime for QSee.ai PowerShell entrypoints.

.DESCRIPTION
Provides settings, preflight checks, console menus, branded headers and
footers, and per-run logging. Dot-sourcing this file has no console, filesystem,
or device side effects; entrypoints explicitly own initialization and cleanup.
#>

$script:QSeeLoggerPath = $null
$script:QSeeLoggerName = $null
$script:QSeeLoggerSeparator = '-' * 72

function Initialize-QSeeRuntime {
    [CmdletBinding()]
    param([switch]$ClearScreen)

    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
    [Console]::InputEncoding = [System.Text.Encoding]::UTF8
    $OutputEncoding = [System.Text.Encoding]::UTF8
    [Environment]::SetEnvironmentVariable('PYTHONUTF8', '1', 'Process')
    [Environment]::SetEnvironmentVariable('PYTHONIOENCODING', 'utf-8', 'Process')

    if (-not $ClearScreen) {
        return
    }

    try {
        if (-not [Console]::IsOutputRedirected) {
            Write-Host "`e[3J`e[H`e[2J" -NoNewline
        }
    }
    catch {
        # Clearing is cosmetic and may fail in redirected or headless hosts.
    }
}

function Initialize-QSeeLogger {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$ScriptName,
        [Parameter(Mandatory)][string]$LogsDirectory
    )

    [System.IO.Directory]::CreateDirectory($LogsDirectory) | Out-Null
    $cleanName = [System.IO.Path]::GetFileNameWithoutExtension($ScriptName)
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    $script:QSeeLoggerName = $cleanName
    $script:QSeeLoggerPath = Join-Path $LogsDirectory "$cleanName-$stamp.log"

    $header = @(
        "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') - Session started: $cleanName"
        $script:QSeeLoggerSeparator
    ) -join [Environment]::NewLine
    [System.IO.File]::WriteAllText(
        $script:QSeeLoggerPath,
        $header + [Environment]::NewLine,
        [System.Text.UTF8Encoding]::new($false)
    )
}

function Write-QSeeLog {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Message,
        [ValidateSet('INFO', 'WARN', 'ERROR', 'DEBUG', 'SUCCESS')]
        [string]$Level = 'INFO'
    )

    if ([string]::IsNullOrWhiteSpace($script:QSeeLoggerPath)) {
        return
    }

    $line = "[$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')] [$Level] $Message"
    try {
        [System.IO.File]::AppendAllText(
            $script:QSeeLoggerPath,
            $line + [Environment]::NewLine,
            [System.Text.UTF8Encoding]::new($false)
        )
    }
    catch {
        # Logging is best effort and must not mask the original workflow result.
    }
}

function Write-QSeeInfo {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Message)

    Write-QSeeLog -Message $Message
    Write-Host $Message -ForegroundColor Cyan
}

function Write-QSeeWarning {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Message)

    Write-QSeeLog -Message $Message -Level WARN
    Write-Host $Message -ForegroundColor Yellow
}

function Write-QSeeError {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Message)

    Write-QSeeLog -Message $Message -Level ERROR
    Write-Host $Message -ForegroundColor Red
}

function Write-QSeeSuccess {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Message)

    Write-QSeeLog -Message $Message -Level SUCCESS
    Write-Host $Message -ForegroundColor Green
}

function Get-QSeeLogPath {
    [CmdletBinding()]
    param()

    return $script:QSeeLoggerPath
}

function Close-QSeeLogger {
    [CmdletBinding()]
    param()

    if (-not [string]::IsNullOrWhiteSpace($script:QSeeLoggerPath)) {
        Write-QSeeLog -Message $script:QSeeLoggerSeparator
        Write-QSeeLog -Message "Session ended: $($script:QSeeLoggerName)"
    }

    $script:QSeeLoggerPath = $null
    $script:QSeeLoggerName = $null
}

function Get-QSeeConsoleWidth {
    [CmdletBinding()]
    param(
        [int]$Minimum = 64,
        [int]$Maximum = 104,
        [int]$Fallback = 84
    )

    try {
        $width = [Console]::WindowWidth - 2
        if ($width -gt 0) {
            return [Math]::Max($Minimum, [Math]::Min($width, $Maximum))
        }
    }
    catch {
        # Fall through to the stable width used by redirected hosts.
    }

    return $Fallback
}

function Show-QSeeBanner {
    [CmdletBinding()]
    param()

    $width = Get-QSeeConsoleWidth
    $left = '  QSEE.AI  |  Device Development Tools'
    $right = 'qsee.ai  '
    $fill = [Math]::Max(2, $width - $left.Length - $right.Length)

    Write-Host ($left + (' ' * $fill) + $right).PadRight($width) `
        -BackgroundColor DarkBlue -ForegroundColor White
    Write-Host ''
    Write-Host '  Clear signals. Confident decisions.' -ForegroundColor Gray
    Write-Host '  Umberto Giacobbi | hello@qsee.ai' -ForegroundColor Cyan
    Write-Host ''
    Write-Host ('-' * $width) -ForegroundColor DarkGray
}

function Show-QSeeCommandHeader {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Action,
        [AllowNull()][object]$Settings,
        [string]$LogPath
    )

    Write-Host ''
    Write-Host "  Action    : $Action" -ForegroundColor White
    if ($null -ne $Settings) {
        Write-Host "  Build dir : $($Settings.BuildDir)" -ForegroundColor DarkGray
        Write-Host "  Port      : $($Settings.Port)" -ForegroundColor DarkGray
    }
    if (-not [string]::IsNullOrWhiteSpace($LogPath)) {
        Write-Host "  Log       : $LogPath" -ForegroundColor DarkGray
    }
    Write-Host ''
}

function Format-QSeeElapsed {
    [CmdletBinding()]
    param([Parameter(Mandatory)][timespan]$Elapsed)

    if ($Elapsed.TotalHours -ge 1) {
        return '{0:D2}:{1:D2}:{2:D2}' -f (
            [int][Math]::Floor($Elapsed.TotalHours),
            $Elapsed.Minutes,
            $Elapsed.Seconds
        )
    }
    return '{0:D2}:{1:D2}' -f (
        [int][Math]::Floor($Elapsed.TotalMinutes),
        $Elapsed.Seconds
    )
}

function Show-QSeeFooter {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$ScriptName,
        [string]$Status = 'COMPLETED',
        [ConsoleColor]$StatusColor = 'Green',
        [Parameter(Mandatory)][datetime]$StartTime,
        [string]$LogPath
    )

    $endTime = Get-Date
    $width = Get-QSeeConsoleWidth
    Write-Host ''
    Write-Host ('=' * $width) -ForegroundColor DarkCyan
    Write-Host ("  [{0}] {1}" -f $Status, $ScriptName) -ForegroundColor $StatusColor
    Write-Host ("  Duration: {0}" -f (Format-QSeeElapsed -Elapsed ($endTime - $StartTime))) -ForegroundColor Gray
    if (-not [string]::IsNullOrWhiteSpace($LogPath)) {
        Write-Host "  Log: $LogPath" -ForegroundColor DarkGray
    }
    Write-Host ('=' * $width) -ForegroundColor DarkCyan
}

function Show-QSeeErrorFooter {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$ScriptName,
        [Parameter(Mandatory)][string]$ErrorMessage,
        [Parameter(Mandatory)][datetime]$StartTime,
        [string]$LogPath
    )

    $endTime = Get-Date
    $width = Get-QSeeConsoleWidth
    Write-Host ''
    Write-Host ('=' * $width) -ForegroundColor DarkRed
    Write-Host "  [FAILED] $ScriptName" -ForegroundColor Red
    Write-Host "  $ErrorMessage" -ForegroundColor White
    Write-Host ("  Duration: {0}" -f (Format-QSeeElapsed -Elapsed ($endTime - $StartTime))) -ForegroundColor Gray
    if (-not [string]::IsNullOrWhiteSpace($LogPath)) {
        Write-Host "  Log: $LogPath" -ForegroundColor DarkGray
    }
    Write-Host ('=' * $width) -ForegroundColor DarkRed
}

# Return the selected item, or null when the configured exit key is used.
function Select-QSeeMenuItem {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Title,
        [Parameter(Mandatory)][object[]]$Items,
        [string]$Prompt = 'Select an option',
        [string]$QuitKey = 'Q',
        [string]$QuitLabel = 'Quit'
    )

    if ($Items.Count -eq 0) {
        throw 'The interactive menu requires at least one item.'
    }
    if ([Console]::IsInputRedirected) {
        throw 'Interactive input is unavailable. Pass an explicit action for automation.'
    }

    while ($true) {
        Write-Host ''
        Write-Host "  $Title" -ForegroundColor Yellow
        Write-Host ('  ' + ('-' * [Math]::Max(28, $Title.Length))) -ForegroundColor DarkGray
        foreach ($item in $Items) {
            Write-Host -NoNewline ("  [{0}] " -f $item.Key) -ForegroundColor Cyan
            Write-Host -NoNewline ([string]$item.Label) -ForegroundColor White
            if ([string]::IsNullOrWhiteSpace([string]$item.Description)) {
                Write-Host ''
            }
            else {
                Write-Host (" - {0}" -f $item.Description) -ForegroundColor DarkGray
            }
        }
        Write-Host -NoNewline "  [$QuitKey] " -ForegroundColor DarkYellow
        Write-Host $QuitLabel -ForegroundColor Gray
        Write-Host ''

        $selection = (Read-Host "  $Prompt").Trim()
        if ($selection.Equals($QuitKey, [StringComparison]::OrdinalIgnoreCase)) {
            return $null
        }

        $match = @(
            $Items | Where-Object {
                ([string]$_.Key).Equals($selection, [StringComparison]::OrdinalIgnoreCase)
            }
        )
        if ($match.Count -eq 1) {
            return $match[0]
        }

        Write-Host '  Invalid selection. Try again.' -ForegroundColor Red
    }
}

# Normalize and reject unsafe serial-port values before they reach process matching.
function ConvertTo-QSeeSerialPortName {
    [CmdletBinding()]
    param([Parameter(Mandatory)][AllowEmptyString()][string]$Port)

    $normalized = $Port.Trim().ToUpperInvariant()
    if ($normalized -notmatch '^COM\d+$') {
        throw "Invalid serial port '$Port'. Expected a Windows port such as COM12."
    }
    return $normalized
}

function ConvertTo-QSeeSettingsObject {
    [CmdletBinding()]
    param([Parameter(Mandatory)][object]$RawSettings)

    return [pscustomobject]@{
        SchemaVersion = [int]$RawSettings.schema_version
        Port = [string]$RawSettings.port
        Baud = [int]$RawSettings.baud
        BuildDir = [string]$RawSettings.build_dir
        WaitSeconds = [int]$RawSettings.wait_seconds
        DurationSeconds = [int]$RawSettings.duration_seconds
        Jobs = [int]$RawSettings.jobs
        IdfPath = [string]$RawSettings.idf_path
        IdfToolsPath = [string]$RawSettings.idf_tools_path
        IdfPythonEnvPath = [string]$RawSettings.idf_python_env_path
        OpenOcdScripts = [string]$RawSettings.openocd_scripts
        EspRomElfDir = [string]$RawSettings.esp_rom_elf_dir
        AssetPython = [string]$RawSettings.asset_python
    }
}

function Merge-QSeeSettings {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][object]$Base,
        [Parameter(Mandatory)][object]$Override
    )

    $knownNames = @($Base.PSObject.Properties.Name)
    foreach ($property in $Override.PSObject.Properties) {
        if ($property.Name -in $knownNames -and $null -ne $property.Value) {
            $Base.($property.Name) = $property.Value
        }
    }
    return $Base
}

# Merge checked-in, local and environment settings into one validated object.
function Get-QSeeSettings {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$DefaultsPath,
        [Parameter(Mandatory)][string]$LocalPath
    )

    if (-not (Test-Path -LiteralPath $DefaultsPath -PathType Leaf)) {
        throw "Default settings file is missing: $DefaultsPath"
    }

    $raw = Get-Content -Raw -LiteralPath $DefaultsPath | ConvertFrom-Json
    if (Test-Path -LiteralPath $LocalPath -PathType Leaf) {
        $local = Get-Content -Raw -LiteralPath $LocalPath | ConvertFrom-Json
        $raw = Merge-QSeeSettings -Base $raw -Override $local
    }

    # Precedence is checked-in defaults, local override, then environment.
    if ($env:QSEE_PORT) { $raw.port = $env:QSEE_PORT }
    if ($env:QSEE_BAUD) { $raw.baud = $env:QSEE_BAUD }
    if ($env:QSEE_BUILD_DIR) { $raw.build_dir = $env:QSEE_BUILD_DIR }
    if ($env:QSEE_WAIT_SECONDS) { $raw.wait_seconds = $env:QSEE_WAIT_SECONDS }
    if ($env:QSEE_DURATION_SECONDS) { $raw.duration_seconds = $env:QSEE_DURATION_SECONDS }
    if ($env:QSEE_JOBS) { $raw.jobs = $env:QSEE_JOBS }
    if ($env:IDF_PATH) { $raw.idf_path = $env:IDF_PATH }
    if ($env:IDF_TOOLS_PATH) { $raw.idf_tools_path = $env:IDF_TOOLS_PATH }
    if ($env:IDF_PYTHON_ENV_PATH) { $raw.idf_python_env_path = $env:IDF_PYTHON_ENV_PATH }
    if ($env:OPENOCD_SCRIPTS) { $raw.openocd_scripts = $env:OPENOCD_SCRIPTS }
    if ($env:ESP_ROM_ELF_DIR) { $raw.esp_rom_elf_dir = $env:ESP_ROM_ELF_DIR }
    if ($env:QSEE_ASSET_PYTHON) { $raw.asset_python = $env:QSEE_ASSET_PYTHON }

    $settings = ConvertTo-QSeeSettingsObject -RawSettings $raw
    if ($settings.SchemaVersion -ne 1) {
        throw "Unsupported settings schema: $($settings.SchemaVersion)"
    }
    $settings.Port = ConvertTo-QSeeSerialPortName -Port $settings.Port
    if ($settings.Baud -le 0 -or $settings.WaitSeconds -le 0 -or $settings.DurationSeconds -le 0) {
        throw 'Baud, wait_seconds, and duration_seconds must be positive.'
    }
    if ($settings.Jobs -lt 0) {
        throw 'jobs cannot be negative.'
    }

    return $settings
}

function Save-QSeeSettings {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][object]$Settings,
        [Parameter(Mandatory)][string]$LocalPath
    )

    $directory = Split-Path -Parent $LocalPath
    [System.IO.Directory]::CreateDirectory($directory) | Out-Null
    $payload = [ordered]@{
        schema_version = 1
        port = [string]$Settings.Port
        baud = [int]$Settings.Baud
        build_dir = [string]$Settings.BuildDir
        wait_seconds = [int]$Settings.WaitSeconds
        duration_seconds = [int]$Settings.DurationSeconds
        jobs = [int]$Settings.Jobs
        idf_path = [string]$Settings.IdfPath
        idf_tools_path = [string]$Settings.IdfToolsPath
        idf_python_env_path = [string]$Settings.IdfPythonEnvPath
        openocd_scripts = [string]$Settings.OpenOcdScripts
        esp_rom_elf_dir = [string]$Settings.EspRomElfDir
        asset_python = [string]$Settings.AssetPython
    }
    $json = $payload | ConvertTo-Json -Depth 4
    [System.IO.File]::WriteAllText(
        $LocalPath,
        $json + [Environment]::NewLine,
        [System.Text.UTF8Encoding]::new($false)
    )
    Write-QSeeLog -Message "Saved local settings: $LocalPath"
}

function Show-QSeeSettings {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][object]$Settings,
        [Parameter(Mandatory)][string]$DefaultsPath,
        [Parameter(Mandatory)][string]$LocalPath
    )

    Write-Host ''
    Write-Host '  QSee.ai script settings' -ForegroundColor Yellow
    Write-Host "  Port            : $($Settings.Port)"
    Write-Host "  Baud            : $($Settings.Baud)"
    Write-Host "  Build directory : $($Settings.BuildDir)"
    Write-Host "  Wait seconds    : $($Settings.WaitSeconds)"
    Write-Host "  Capture seconds : $($Settings.DurationSeconds)"
    Write-Host "  Build jobs      : $($Settings.Jobs) (0 = automatic)"
    Write-Host "  ESP-IDF         : $($Settings.IdfPath)"
    Write-Host "  ESP-IDF tools   : $($Settings.IdfToolsPath)"
    Write-Host "  ESP-IDF Python  : $($Settings.IdfPythonEnvPath)"
    Write-Host "  OpenOCD scripts : $($Settings.OpenOcdScripts)"
    Write-Host "  ESP ROM ELFs    : $($Settings.EspRomElfDir)"
    Write-Host "  Asset Python    : $($Settings.AssetPython)"
    Write-Host ''
    Write-Host "  Defaults        : $DefaultsPath" -ForegroundColor DarkGray
    Write-Host "  Local override  : $LocalPath" -ForegroundColor DarkGray
    Write-Host "  Override exists : $(Test-Path -LiteralPath $LocalPath -PathType Leaf)" -ForegroundColor DarkGray
}

function New-QSeePreflightResult {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][bool]$Passed,
        [Parameter(Mandatory)][string]$Detail
    )

    return [pscustomobject]@{
        Name = $Name
        Passed = $Passed
        Detail = $Detail
    }
}

# Run only the dependency checks required by the selected workflow.
function Invoke-QSeePreflight {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Action,
        [Parameter(Mandatory)][object]$Settings,
        [Parameter(Mandatory)][string]$ProjectRoot,
        [Parameter(Mandatory)][string]$ScriptsRoot,
        [string]$ConfigPath,
        [string]$BootSource
    )

    $checks = [System.Collections.Generic.List[object]]::new()
    $checks.Add((New-QSeePreflightResult -Name 'Project root' `
        -Passed (Test-Path -LiteralPath (Join-Path $ProjectRoot 'CMakeLists.txt') -PathType Leaf) `
        -Detail $ProjectRoot))

    $firmwareActions = @(
        'build', 'clean', 'fullclean', 'flash', 'monitor', 'erase',
        'reconfigure', 'set-target', 'combo', 'recover'
    )
    if ($Action -in $firmwareActions) {
        $idfPy = Join-Path $Settings.IdfPath 'tools\idf.py'
        $idfPython = Join-Path $Settings.IdfPythonEnvPath 'Scripts\python.exe'
        $checks.Add((New-QSeePreflightResult -Name 'ESP-IDF' `
            -Passed (Test-Path -LiteralPath $idfPy -PathType Leaf) -Detail $idfPy))
        $checks.Add((New-QSeePreflightResult -Name 'ESP-IDF Python' `
            -Passed (Test-Path -LiteralPath $idfPython -PathType Leaf) -Detail $idfPython))
    }

    if ($Action -in @('build', 'combo', 'recover')) {
        $ninja = Get-Command ninja -ErrorAction SilentlyContinue
        $checks.Add((New-QSeePreflightResult -Name 'Ninja' `
            -Passed ($null -ne $ninja) `
            -Detail $(if ($ninja) { $ninja.Source } else { 'not found on PATH' })))
    }

    if ($Action -in @('ports', 'flash', 'monitor', 'capture', 'erase', 'combo', 'recover')) {
        $pnp = Get-Command Get-PnpDevice -ErrorAction SilentlyContinue
        $checks.Add((New-QSeePreflightResult -Name 'Windows port discovery' `
            -Passed ($null -ne $pnp) `
            -Detail $(if ($pnp) { 'Get-PnpDevice available' } else { 'Get-PnpDevice is unavailable' })))
    }

    if ($Action -in @('assets', 'boot-assets', 'pairing')) {
        $python = Get-Command $Settings.AssetPython -ErrorAction SilentlyContinue
        $checks.Add((New-QSeePreflightResult -Name 'Asset Python' `
            -Passed ($null -ne $python) `
            -Detail $(if ($python) { $python.Source } else { "$($Settings.AssetPython) not found" })))
    }

    if ($Action -in @('assets', 'boot-assets')) {
        $generator = Join-Path $ScriptsRoot 'generate_boot_assets.py'
        $checks.Add((New-QSeePreflightResult -Name 'Boot generator' `
            -Passed (Test-Path -LiteralPath $generator -PathType Leaf) -Detail $generator))
        if (-not [string]::IsNullOrWhiteSpace($BootSource)) {
            $checks.Add((New-QSeePreflightResult -Name 'Boot source' `
                -Passed (Test-Path -LiteralPath $BootSource -PathType Leaf) -Detail $BootSource))
        }
    }

    if ($Action -in @('assets', 'sfx')) {
        foreach ($toolName in @('ffmpeg', 'ffprobe')) {
            $tool = Get-Command $toolName -ErrorAction SilentlyContinue
            $checks.Add((New-QSeePreflightResult -Name $toolName `
                -Passed ($null -ne $tool) `
                -Detail $(if ($tool) { $tool.Source } else { 'not found on PATH' })))
        }
        $checks.Add((New-QSeePreflightResult -Name 'SFX configuration' `
            -Passed (Test-Path -LiteralPath $ConfigPath -PathType Leaf) -Detail $ConfigPath))
    }

    if ($Action -eq 'pairing') {
        $pairingScript = Join-Path $ScriptsRoot 'verify_pairing.py'
        $checks.Add((New-QSeePreflightResult -Name 'Pairing verifier' `
            -Passed (Test-Path -LiteralPath $pairingScript -PathType Leaf) -Detail $pairingScript))
    }

    Write-Host '  Preflight' -ForegroundColor Yellow
    foreach ($check in $checks) {
        $label = if ($check.Passed) { 'PASS' } else { 'FAIL' }
        $color = if ($check.Passed) { 'Green' } else { 'Red' }
        Write-Host -NoNewline ("  [{0}] " -f $label) -ForegroundColor $color
        Write-Host ("{0}: {1}" -f $check.Name, $check.Detail)
        Write-QSeeLog -Message "Preflight $label - $($check.Name): $($check.Detail)" `
            -Level $(if ($check.Passed) { 'DEBUG' } else { 'ERROR' })
    }
    Write-Host ''

    $failed = @($checks | Where-Object { -not $_.Passed })
    if ($failed.Count -gt 0) {
        throw "Preflight failed: $($failed.Name -join ', ')"
    }

    return @($checks)
}

# Invoke a native tool with array-safe arguments and enforce its exit code.
function Invoke-QSeeNativeCommand {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter(Mandatory)][string[]]$ArgumentList,
        [Parameter(Mandatory)][string]$FailureMessage
    )

    Write-QSeeLog -Message "Executing: $FilePath $($ArgumentList -join ' ')" -Level DEBUG
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "$FailureMessage (exit code $LASTEXITCODE)"
    }
}
