<#!
.SYNOPSIS
Launch the LUNASHELL Windows simulator with an interactive terminal UI or direct arguments.

.DESCRIPTION
If no arguments are passed, this script shows a branded guided menu to configure
scenario, screen, idle-state and switches before launching.

If arguments are passed, it starts directly using them.

.PARAMETER Scenario
Simulation scenario to start.

.PARAMETER BoardConfig
Preview the reviewed ui-bootstrap YAML and minimal firmware shell.

.PARAMETER Cody
Open the local Cody normalized cloud-rig PoC, without network/device services.

.PARAMETER CodyScreen
Select rig, boot-sleep, boot-waking or boot-ready in Cody mode.

.PARAMETER CodyRotation
Rotate the entire Cody canvas and inverse-map touch by 0, 90, 180 or 270 degrees.

.PARAMETER CodyAtMs
Choose deterministic animation time for a Cody capture.

.PARAMETER PythonExecutable
Explicit Python interpreter with Pillow and PyYAML for Cody mode.

.PARAMETER DumpBoardShell
Print the synthetic ui-bootstrap shell contract and exit.

.PARAMETER ShellTaps
Synthetic shell taps for headless contract checks.

.PARAMETER Screen
Initial simulator screen.

.PARAMETER IdleState
Initial idle state for startup.

.PARAMETER Capture
Optional capture path.

.PARAMETER SafeGuide
Enable safe guide mode.

.PARAMETER NoSound
Disable simulator sounds.

.PARAMETER PerformanceMode
Enable performance mode.

.PARAMETER WirelessRunning
Enable wireless-running mode.

.PARAMETER ScenarioFile
Load a scenario from an explicit JSON fixture path.

.PARAMETER AtMs
Start the deterministic scenario clock at this elapsed millisecond.

.PARAMETER ListScenarios
List the built-in scenario fixtures and exit.

.PARAMETER ValidateScenarios
Validate all built-in scenario fixtures and exit.

.PARAMETER DumpSnapshot
Write the current synthetic snapshot as JSON and exit.

.PARAMETER DumpApi
Write one synthetic HTTP API payload as JSON and exit.

.PARAMETER Action
Apply one or more semantic simulator actions before producing output.

.PARAMETER AdvanceMs
Advance the deterministic clock by this many milliseconds before producing output.

.PARAMETER Output
Write headless JSON output to this file instead of standard output.

.PARAMETER Help
Show full help and exit.

.EXAMPLE
.\run-simulator.ps1
# Opens the interactive menu.

.EXAMPLE
.\run-simulator.ps1 -Scenario battery-low -Screen battery -NoSound
# Launches directly with explicit args.

.EXAMPLE
.\run-simulator.ps1 -Scenario wifi-lifecycle -AtMs 5000 -Action start:wireless -DumpSnapshot
# Runs headlessly and writes a deterministic synthetic snapshot to standard output.
#>
[CmdletBinding()]
param(
    [switch]$Cody,
    [ValidateSet('rig','boot-sleep','boot-waking','boot-ready')]
    [string]$CodyScreen = 'rig',
    [ValidateSet(0,90,180,270)]
    [int]$CodyRotation = 0,
    [ValidateRange(0,2147483647)]
    [int]$CodyAtMs = 1000,
    [string]$PythonExecutable,
    [switch]$BoardConfig,
    [switch]$DumpBoardShell,
    [int]$ShellTaps = 0,
    [ValidateSet(
        'battery-charging','battery-low','battery-unavailable','boot-partial','gps-searching','healthy',
        'memory-pressure','pmic-no-cell','sd-card-lifecycle','sd-unformatted','stale-recovery',
        'standby-rollback','tof-one-missing','wifi-lifecycle'
    )]
    [string]$Scenario = 'healthy',

    [ValidateSet(
        'home','battery','logs','settings','storage','chrome-settings','power-settings','hardware',
        'audio','fft','fft-detail','imu','leg-accel','leg-gyro','leg-combo','leg-motion','leg-settings','gps','tof','tof-settings',
        'pairing-intro','pairing-bluetooth','pairing-app','pairing-app-qr',
        'pairing-device-qr','pairing-credentials','pairing-connecting',
        'pairing-success','pairing-failed'
    )]
    [string]$Screen = 'home',

    [ValidateSet('active','command-hidden','chrome-hidden','fullscreen','dimmed','off','standby')]
    [string]$IdleState = 'active',

    [string]$Capture,
    [switch]$SafeGuide,
    [switch]$NoSound,
    [switch]$PerformanceMode,
    [switch]$WirelessRunning,
    [string]$ScenarioFile,
    [long]$AtMs = 0,
    [switch]$ListScenarios,
    [switch]$ValidateScenarios,
    [switch]$DumpSnapshot,
    [ValidateSet(
        '/api/v1/health','/api/v1/capabilities','/api/v1/info','/api/v1/diagnostics',
        '/api/v1/rtc','/api/v1/gps','/api/v1/status'
    )]
    [string]$DumpApi,
    [string[]]$Action = @(),
    [long]$AdvanceMs = 0,
    [string]$Output,
    [switch]$Help
)

$ErrorActionPreference = 'Stop'
$script:LauncherBoundParameters = $PSBoundParameters

$ModulesPath = 'E:\tools\scripts\modules'
$Simulator = Join-Path $PSScriptRoot 'lunashell_simulator.py'

$BrandName = 'LUNASHELL Simulator Console'
$BrandTag = 'Windows simulator launcher'
$BrandVersion = 'v1.7'

$script:UiBootstrapDone = $false
$script:UiReady = $false

function Copy-SimulatorState {
    param([System.Collections.IDictionary]$Source)

    $copy = [ordered]@{}
    foreach ($key in $Source.Keys) {
        $copy[$key] = $Source[$key]
    }
    return ,$copy
}

$script:State = [ordered]@{
    BoardConfig = $false
    Scenario = 'healthy'
    ScenarioFile = $null
    AtMs = 0
    Screen = 'home'
    IdleState = 'active'
    Capture = $null
    SafeGuide = $false
    NoSound = $false
    PerformanceMode = $false
    WirelessRunning = $false
}

$DefaultState = Copy-SimulatorState -Source $script:State

$ScenarioCatalog = @(
    [pscustomobject]@{ Name = 'battery-charging'; Description = 'Simulate active charging with charging indicators.' },
    [pscustomobject]@{ Name = 'battery-low'; Description = 'Battery low warnings and alert bars.' },
    [pscustomobject]@{ Name = 'battery-unavailable'; Description = 'Battery telemetry unavailable state.' },
    [pscustomobject]@{ Name = 'boot-partial'; Description = 'Optional boot stages fail with explicit degraded state.' },
    [pscustomobject]@{ Name = 'gps-searching'; Description = 'GPS searching without a valid fix.' },
    [pscustomobject]@{ Name = 'healthy'; Description = 'Nominal startup with valid sensor values.' },
    [pscustomobject]@{ Name = 'memory-pressure'; Description = 'Low-memory diagnostics and recovery timeline.' },
    [pscustomobject]@{ Name = 'pmic-no-cell'; Description = 'Healthy PMIC observation with no battery cell present.' },
    [pscustomobject]@{ Name = 'sd-card-lifecycle'; Description = 'Verified SD format authorization and lifecycle actions.' },
    [pscustomobject]@{ Name = 'sd-unformatted'; Description = 'SD unformatted workflow and repair path.' },
    [pscustomobject]@{ Name = 'stale-recovery'; Description = 'Stale telemetry followed by deterministic recovery.' },
    [pscustomobject]@{ Name = 'standby-rollback'; Description = 'Standby transition failure and rollback state.' },
    [pscustomobject]@{ Name = 'tof-one-missing'; Description = 'One ToF sensor unavailable while peers remain valid.' },
    [pscustomobject]@{ Name = 'wifi-lifecycle'; Description = 'Wireless start, stop and state-transition actions.' }
)

$ScreenCatalog = @(
    [pscustomobject]@{ Name = 'home'; Description = 'Home dashboard and controls.' },
    [pscustomobject]@{ Name = 'battery'; Description = 'Battery status and charging indicators.' },
    [pscustomobject]@{ Name = 'logs'; Description = 'Log viewer and history timeline.' },
    [pscustomobject]@{ Name = 'settings'; Description = 'General settings.' },
    [pscustomobject]@{ Name = 'storage'; Description = 'Storage summary and SD management.' },
    [pscustomobject]@{ Name = 'chrome-settings'; Description = 'Chrome integration settings.' },
    [pscustomobject]@{ Name = 'power-settings'; Description = 'Power timeout and policy presets.' },
    [pscustomobject]@{ Name = 'hardware'; Description = 'Hardware diagnostics.' },
    [pscustomobject]@{ Name = 'audio'; Description = 'Audio controls and stream status.' },
    [pscustomobject]@{ Name = 'fft'; Description = 'FFT overview chart mode.' },
    [pscustomobject]@{ Name = 'fft-detail'; Description = 'Detailed FFT + overlays.' },
    [pscustomobject]@{ Name = 'imu'; Description = 'IMU orientation and stability indicators.' },
    [pscustomobject]@{ Name = 'gps'; Description = 'GPS status and fix simulation.' },
    [pscustomobject]@{ Name = 'tof'; Description = 'ToF raw sensor view.' },
    [pscustomobject]@{ Name = 'tof-settings'; Description = 'ToF sensor tuning and thresholds.' },
    [pscustomobject]@{ Name = 'pairing-intro'; Description = 'Pairing intro flow.' },
    [pscustomobject]@{ Name = 'pairing-bluetooth'; Description = 'Bluetooth pairing flow.' },
    [pscustomobject]@{ Name = 'pairing-app'; Description = 'App pairing sequence.' },
    [pscustomobject]@{ Name = 'pairing-app-qr'; Description = 'Pairing using app QR.' },
    [pscustomobject]@{ Name = 'pairing-device-qr'; Description = 'Pairing using device QR.' },
    [pscustomobject]@{ Name = 'pairing-credentials'; Description = 'Credential entry/check step.' },
    [pscustomobject]@{ Name = 'pairing-connecting'; Description = 'Active pairing connection.' },
    [pscustomobject]@{ Name = 'pairing-success'; Description = 'Pairing completed successfully.' },
    [pscustomobject]@{ Name = 'pairing-failed'; Description = 'Pairing failed fallback state.' }
)

$IdleStateCatalog = @(
    [pscustomobject]@{ Name = 'active'; Description = 'Keep UI fully visible and interactive.' },
    [pscustomobject]@{ Name = 'command-hidden'; Description = 'Hide command overlay controls.' },
    [pscustomobject]@{ Name = 'chrome-hidden'; Description = 'Hide Chrome-specific overlays.' },
    [pscustomobject]@{ Name = 'fullscreen'; Description = 'Fullscreen simulator output.' },
    [pscustomobject]@{ Name = 'dimmed'; Description = 'Dimmed rendering mode.' },
    [pscustomobject]@{ Name = 'off'; Description = 'Disable UI during idle.' },
    [pscustomobject]@{ Name = 'standby'; Description = 'Standby visual profile.' }
)

$MenuKeys = @('1','2','3','4','5','6','7','8','9','u','U','c','C','r','R','h','H','g','G','0','x','X')

function Get-ConsoleWidth {
    if (Get-Command Resolve-MenuConsoleWidth -ErrorAction SilentlyContinue) {
        return Resolve-MenuConsoleWidth -Minimum 88 -Maximum 170 -Fallback 112
    }

    try {
        $w = [Console]::WindowWidth
        if ($w -gt 30) { return [Math]::Max(88, [Math]::Min($w - 4, 170)) }
    }
    catch {}

    try {
        $w = $Host.UI.RawUI.WindowSize.Width
        if ($w -gt 30) { return [Math]::Max(88, [Math]::Min($w - 4, 170)) }
    }
    catch {}

    return 112
}

function Write-Colored {
    param(
        [Parameter(Mandatory)][AllowEmptyString()][string]$Text,
        [ConsoleColor]$Color = 'Gray',
        [switch]$NoNewLine
    )

    if ($NoNewLine) { Write-Host $Text -ForegroundColor $Color -NoNewline }
    else { Write-Host $Text -ForegroundColor $Color }
}

function Initialize-SimulatorUi {
    if ($script:UiBootstrapDone) { return $script:UiReady }

    $script:UiBootstrapDone = $true

    if (-not (Test-Path -LiteralPath $ModulesPath -PathType Container)) {
        $script:UiReady = $false
        return $false
    }

    try {
        $boot = Join-Path $ModulesPath 'boot-banner.ps1'
        $footer = Join-Path $ModulesPath 'footer-banner.ps1'
        $help = Join-Path $ModulesPath 'help-display.ps1'
        $menu = Join-Path $ModulesPath 'MenuManager\MenuManager.psd1'

        if (Test-Path -LiteralPath $boot -PathType Leaf) { . $boot }
        if (Test-Path -LiteralPath $footer -PathType Leaf) { . $footer }
        if (Test-Path -LiteralPath $help -PathType Leaf) { . $help }
        if (Test-Path -LiteralPath $menu -PathType Leaf) { Import-Module -Name $menu -Force -ErrorAction SilentlyContinue }
    }
    catch {
        $script:UiReady = $false
        return $false
    }

    $required = @('Read-MenuChoice','Read-IndexChoice','Write-MenuItem','Write-MenuSectionHeader','Write-MenuExitItem','Wait-ForEnter')
    $found = 0
    foreach ($c in $required) { if (Get-Command -Name $c -ErrorAction SilentlyContinue) { $found++ } }
    $script:UiReady = ($found -eq $required.Count)

    return $script:UiReady
}

function Show-Brand {
    if (Get-Command Show-Banner -ErrorAction SilentlyContinue) {
        try { Show-Banner; return } catch {}
    }

    $w = Get-ConsoleWidth
    $left = "⚡ $BrandName"
    $right = "$BrandTag • $BrandVersion"
    $gap = [Math]::Max(4, $w - $left.Length - $right.Length)

    Write-Colored ''
    Write-Colored ("  $left" + (' ' * $gap) + $right).PadRight($w) -Color White
    Write-Colored ('  ' + ('─' * [Math]::Min(130, $w - 2))) -Color DarkGray
    Write-Colored ''
}

function Get-Description {
    param([array]$Catalog, [string]$Name)
    foreach ($e in $Catalog) { if ($e.Name -eq $Name) { return $e.Description } }
    return ''
}

function Read-MenuInput {
    param([string]$Prompt, [string[]]$AllowedChoices)

    if (Get-Command Read-MenuChoice -ErrorAction SilentlyContinue) {
        return Read-MenuChoice -Prompt $Prompt -AllowedChoices $AllowedChoices
    }

    while ($true) {
        $raw = Read-Host "  $Prompt"
        if ([string]::IsNullOrWhiteSpace($raw)) { return '' }
        if (($AllowedChoices.Count -eq 0) -or ($AllowedChoices -contains $raw)) { return $raw }
        Write-Colored "  ✗ Invalid choice." -Color Red
    }
}

function Read-IndexInput {
    param([string]$Prompt, [int]$Min, [int]$Max)

    if (Get-Command Read-IndexChoice -ErrorAction SilentlyContinue) {
        return Read-IndexChoice -Prompt $Prompt -Min $Min -Max $Max
    }

    while ($true) {
        $raw = Read-Host "  $Prompt"
        if ([string]::IsNullOrWhiteSpace($raw)) { return $null }
        if ($raw -eq '0') { return $null }
        $n = 0
        if ([int]::TryParse($raw.Trim(), [ref]$n) -and $n -ge $Min -and $n -le $Max) { return $n }
        Write-Colored "  ✗ Enter a number between $Min and $Max (0 to return)." -Color Red
    }
}

function Build-LaunchArgs {
    param($state)

    $args = @()
    if ($state.BoardConfig) {
        $args += '--board-config'
        if (-not [string]::IsNullOrWhiteSpace($state.Capture)) { $args += @('--capture', $state.Capture) }
        return $args
    }
    if (-not [string]::IsNullOrWhiteSpace($state.ScenarioFile)) {
        $args += @('--scenario-file', $state.ScenarioFile)
    }
    else {
        $args += @('--scenario', $state.Scenario)
    }
    if ([long]$state.AtMs -gt 0) { $args += @('--at-ms', [string]$state.AtMs) }
    $args += @('--screen', $state.Screen, '--idle-state', $state.IdleState)
    if (-not [string]::IsNullOrWhiteSpace($state.Capture)) { $args += @('--capture', $state.Capture) }
    if ($state.SafeGuide) { $args += '--safe-guide' }
    if ($state.NoSound) { $args += '--no-sound' }
    if ($state.PerformanceMode) { $args += '--performance-mode' }
    if ($state.WirelessRunning) { $args += '--wireless-running' }
    return $args
}

function Build-HeadlessArgs {
    param([System.Collections.IDictionary]$BoundParameters)

    $args = @()
    if ($BoardConfig) {
        foreach ($setting in @('Scenario','ScenarioFile','AtMs','Screen','IdleState','SafeGuide',
                               'PerformanceMode','WirelessRunning','ListScenarios',
                               'ValidateScenarios','DumpSnapshot','DumpApi','Action','AdvanceMs',
                               'Capture','NoSound')) {
            if ($BoundParameters.ContainsKey($setting)) {
                throw "-BoardConfig cannot be combined with -$setting. The minimal shell has no device pages or scenarios."
            }
        }
        if ($DumpBoardShell) { $args += '--dump-board-shell' }
        $args += '--board-config'
        if ($BoundParameters.ContainsKey('ShellTaps')) { $args += @('--shell-taps', [string]$ShellTaps) }
        if ($BoundParameters.ContainsKey('Output')) { $args += @('--output', $Output) }
        return $args
    }
    if ($DumpBoardShell -or $BoundParameters.ContainsKey('ShellTaps')) {
        throw '-DumpBoardShell and -ShellTaps require -BoardConfig.'
    }
    if ($BoundParameters.ContainsKey('ScenarioFile')) {
        if ($BoundParameters.ContainsKey('Scenario')) {
            throw 'Use either -Scenario or -ScenarioFile, not both.'
        }
        $args += @('--scenario-file', $ScenarioFile)
    }
    else {
        $args += @('--scenario', $Scenario)
    }

    if ($BoundParameters.ContainsKey('AtMs')) { $args += @('--at-ms', [string]$AtMs) }
    if ($ListScenarios) { $args += '--list-scenarios' }
    if ($ValidateScenarios) { $args += '--validate-scenarios' }
    if ($DumpSnapshot) { $args += '--dump-snapshot' }
    if ($BoundParameters.ContainsKey('DumpApi')) { $args += @('--dump-api', $DumpApi) }
    foreach ($semanticAction in $Action) {
        if (-not [string]::IsNullOrWhiteSpace($semanticAction)) {
            $args += @('--action', $semanticAction)
        }
    }
    if ($BoundParameters.ContainsKey('AdvanceMs')) { $args += @('--advance-ms', [string]$AdvanceMs) }
    if ($BoundParameters.ContainsKey('Output')) { $args += @('--output', $Output) }
    return $args
}

function Resolve-PythonCommand {
    $venvPython = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) '.venv-simulator\Scripts\python.exe'
    if (Test-Path -LiteralPath $venvPython -PathType Leaf) {
        & $venvPython -c 'import PIL' 2>$null
        if ($LASTEXITCODE -eq 0) { return $venvPython }
    }
    foreach ($candidate in @('python', 'python3', 'py')) {
        if (Get-Command $candidate -ErrorAction SilentlyContinue) { return $candidate }
    }
    throw 'Python is not available in PATH. Please install Python 3 and retry.'
}

function Invoke-HeadlessSimulator {
    param([System.Collections.IDictionary]$BoundParameters)

    if (-not (Test-Path -LiteralPath $Simulator -PathType Leaf)) {
        throw "Missing simulator entrypoint: $Simulator"
    }

    $python = Resolve-PythonCommand
    if ($BoardConfig) {
        & $python -c 'import yaml' 2>$null
        if ($LASTEXITCODE -ne 0) { throw 'UI bootstrap requires PyYAML. Install it in .venv-simulator.' }
    }
    $args = Build-HeadlessArgs -BoundParameters $BoundParameters
    & $python -B $Simulator @args
    if ($LASTEXITCODE -ne 0) {
        throw "Simulator headless command failed with exit code $LASTEXITCODE."
    }
}

function Get-LaunchCommandString {
    param($state)
    $parts = @('python', '-B', $Simulator) + (Build-LaunchArgs -state $state)
    $quoted = foreach ($t in $parts) {
        if ($t -match '\s') { '"{0}"' -f ($t -replace '"', '\"') }
        else { $t }
    }
    return $quoted -join ' '
}

function Copy-LaunchCommand {
    param($state)
    $command = Get-LaunchCommandString -state $state
    try { if (Get-Command Set-Clipboard -ErrorAction SilentlyContinue) { Set-Clipboard -Value $command; return $true } } catch {}
    try { $command | clip.exe; return $true } catch { return $false }
}

function Show-CommandPanel {
    param($state)

    if (Get-Command Write-MenuSectionHeader -ErrorAction SilentlyContinue) {
        Write-MenuSectionHeader -Title 'PRESET PREVIEW'
    }
    else {
        Write-Colored '  PRESET PREVIEW' -Color Cyan
        Write-Colored ('  ' + ('─' * 56)) -Color DarkGray
    }

    Write-Colored ("  Scenario      : {0}" -f $state.Scenario) -Color White
    Write-Colored ("  Screen        : {0}" -f $state.Screen) -Color White
    Write-Colored ("  Idle state    : {0}" -f $state.IdleState) -Color White
    Write-Colored ("  Capture       : {0}" -f $(if ([string]::IsNullOrWhiteSpace($state.Capture)) { 'not set' } else { $state.Capture })) -Color Yellow

    $safe = if ($state.SafeGuide) { @{Text='ON';Color='Green'} } else { @{Text='OFF';Color='DarkGray'} }
    $sound = if ($state.NoSound) { @{Text='ON';Color='Green'} } else { @{Text='OFF';Color='DarkGray'} }
    $perf = if ($state.PerformanceMode) { @{Text='ON';Color='Green'} } else { @{Text='OFF';Color='DarkGray'} }
    $wire = if ($state.WirelessRunning) { @{Text='ON';Color='Green'} } else { @{Text='OFF';Color='DarkGray'} }

    Write-Colored '  Safe guide    : ' -Color DarkGray -NoNewLine
    Write-Colored $safe.Text -Color $safe.Color
    Write-Colored '  No sound      : ' -Color DarkGray -NoNewLine
    Write-Colored $sound.Text -Color $sound.Color
    Write-Colored '  Performance   : ' -Color DarkGray -NoNewLine
    Write-Colored $perf.Text -Color $perf.Color
    Write-Colored '  Wireless      : ' -Color DarkGray -NoNewLine
    Write-Colored $wire.Text -Color $wire.Color

    Write-Colored ''
    Write-Colored '  Command preview:' -Color Cyan
    $command = Get-LaunchCommandString -state $state
    $max = [Math]::Max(24, (Get-ConsoleWidth) - 8)
    if ($command.Length -gt $max) { $command = '... ' + $command.Substring($command.Length - ($max - 4)) }
    Write-Colored "    $command" -Color Gray
    Write-Colored ''
}

function Select-CatalogValue {
    param([string]$Title, [array]$Catalog, [string]$Current)

    Clear-Host
    Show-Brand
    Write-Colored "  $Title" -Color Cyan
    Write-Colored ('  ' + ('─' * 52)) -Color DarkGray

    for ($i = 0; $i -lt $Catalog.Count; $i++) {
        $entry = $Catalog[$i]
        if (Get-Command Write-MenuItem -ErrorAction SilentlyContinue) {
            Write-MenuItem -Number ($i + 1) -Emoji '🧩' -Label $entry.Name -Desc $entry.Description
        }
        else {
            Write-Colored ("  {0,2}) {1} — {2}" -f ($i + 1), $entry.Name, $entry.Description) -Color Gray
        }
    }

    Write-Colored ''
    $choice = Read-IndexInput -Prompt "Choose option [1-$($Catalog.Count)] or 0 to return" -Min 1 -Max $Catalog.Count
    if ($null -eq $choice) { return $Current }
    return [string]$Catalog[$choice - 1].Name
}

function Edit-CapturePath {
    param([string]$Current)

    Clear-Host
    Show-Brand
    Write-Colored '  CAPTURE PATH' -Color Cyan
    Write-Colored ('  ' + ('─' * 32)) -Color DarkGray
    if ([string]::IsNullOrWhiteSpace($Current)) {
        Write-Colored '  Current: not set' -Color DarkGray
    }
    else {
        Write-Colored "  Current: $Current" -Color Yellow
    }

    $raw = Read-Host '  Enter capture path (blank = clear, 0 = back)'
    if ($raw -eq '0') { return $Current }
    return $raw.Trim()
}

function Show-SwitchGuide {
    Clear-Host
    Show-Brand
    Write-Colored '  SWITCH GUIDE' -Color Cyan
    Write-Colored ('  ' + ('─' * 42)) -Color DarkGray

    $rows = @(
        [pscustomobject]@{ Flag = '--scenario'; Desc = 'Select scenario preset.' },
        [pscustomobject]@{ Flag = '--screen'; Desc = 'Open with this first screen.' },
        [pscustomobject]@{ Flag = '--idle-state'; Desc = 'Configure initial idle behavior.' },
        [pscustomobject]@{ Flag = '--capture'; Desc = 'Save capture output path.' },
        [pscustomobject]@{ Flag = '--safe-guide'; Desc = 'Enable safe guide overlay.' },
        [pscustomobject]@{ Flag = '--no-sound'; Desc = 'Disable simulator sounds.' },
        [pscustomobject]@{ Flag = '--performance-mode'; Desc = 'Enable performance-oriented simulation.' },
        [pscustomobject]@{ Flag = '--wireless-running'; Desc = 'Enable wireless running state.' },
        [pscustomobject]@{ Flag = '--scenario-file'; Desc = 'Load an explicit JSON scenario fixture.' },
        [pscustomobject]@{ Flag = '--at-ms'; Desc = 'Start at a deterministic timeline offset.' },
        [pscustomobject]@{ Flag = '--list-scenarios'; Desc = 'List built-in fixtures without opening the UI.' },
        [pscustomobject]@{ Flag = '--validate-scenarios'; Desc = 'Validate all built-in fixtures headlessly.' },
        [pscustomobject]@{ Flag = '--dump-snapshot'; Desc = 'Emit a synthetic snapshot without opening the UI.' },
        [pscustomobject]@{ Flag = '--dump-api'; Desc = 'Emit fake-device API payloads without opening the UI.' },
        [pscustomobject]@{ Flag = '--action'; Desc = 'Apply a repeatable semantic simulator action.' },
        [pscustomobject]@{ Flag = '--advance-ms'; Desc = 'Advance the virtual clock before output.' },
        [pscustomobject]@{ Flag = '--output'; Desc = 'Write headless JSON output to a file.' }
    )

    foreach ($r in $rows) {
        Write-Colored ("  {0,-20} {1}" -f $r.Flag, $r.Desc) -Color Gray
    }

    Write-Colored ''
    Write-Colored '  Press any key to return...' -Color DarkGray
    if (Get-Command Wait-ForEnter -ErrorAction SilentlyContinue) {
        Wait-ForEnter
    }
    else {
        $null = Read-Host
    }
}

function Show-MainMenu {
    Clear-Host
    Show-Brand
    Show-CommandPanel -state $script:State

    if (Get-Command Write-MenuSectionHeader -ErrorAction SilentlyContinue) {
        Write-MenuSectionHeader -Title 'MENU'
    }
    else {
        Write-Colored '  MENU' -Color Cyan
        Write-Colored ('  ' + ('─' * 40)) -Color DarkGray
    }

    $menuItems = @(
        [pscustomobject]@{ Number = 1; Emoji = '🧭'; Label = 'Scenario'; Desc = "$($script:State.Scenario) — $((Get-Description -Catalog $ScenarioCatalog -Name $script:State.Scenario))" },
        [pscustomobject]@{ Number = 2; Emoji = '🖼'; Label = 'Screen'; Desc = "$($script:State.Screen) — $((Get-Description -Catalog $ScreenCatalog -Name $script:State.Screen))" },
        [pscustomobject]@{ Number = 3; Emoji = '💤'; Label = 'Idle state'; Desc = "$($script:State.IdleState) — $((Get-Description -Catalog $IdleStateCatalog -Name $script:State.IdleState))" },
        [pscustomobject]@{ Number = 4; Emoji = '🛡'; Label = 'Safe guide'; Desc = $(if ($script:State.SafeGuide) { 'ON' } else { 'OFF' }) },
        [pscustomobject]@{ Number = 5; Emoji = '🔈'; Label = 'No sound'; Desc = $(if ($script:State.NoSound) { 'ON' } else { 'OFF' }) },
        [pscustomobject]@{ Number = 6; Emoji = '⚡'; Label = 'Performance'; Desc = $(if ($script:State.PerformanceMode) { 'ON' } else { 'OFF' }) },
        [pscustomobject]@{ Number = 7; Emoji = '📶'; Label = 'Wireless'; Desc = $(if ($script:State.WirelessRunning) { 'ON' } else { 'OFF' }) },
        [pscustomobject]@{ Number = 8; Emoji = '📁'; Label = 'Capture path'; Desc = $(if ([string]::IsNullOrWhiteSpace($script:State.Capture)) { 'not set' } else { $script:State.Capture }) },
        [pscustomobject]@{ Number = 9; Emoji = '▶'; Label = 'Launch simulator'; Desc = 'Start with current settings' },
        [pscustomobject]@{ Number = 0; Emoji = '🚪'; Label = 'Exit'; Desc = 'Close launcher' }
    )

    foreach ($item in $menuItems[0..8]) {
        if (Get-Command Write-MenuItem -ErrorAction SilentlyContinue) {
            Write-MenuItem -Number $item.Number -Emoji $item.Emoji -Label $item.Label -Desc $item.Desc
        }
        else {
            Write-Colored ("  [{0}] {1,-14} {2}" -f $item.Number, $item.Label, $item.Desc) -Color Yellow
        }
    }

    Write-Colored ''
    Write-Colored ('  [ U ] UI bootstrap: ' + $(if ($script:State.BoardConfig) { 'ON · minimal firmware shell' } else { 'OFF · full synthetic device' })) -Color Cyan
    Write-Colored '  [ C ] Copy command' -Color DarkGray
    Write-Colored '  [ R ] Reset defaults' -Color DarkGray
    Write-Colored '  [ G ] Switch guide' -Color DarkGray
    Write-Colored '  [ H ] Full help' -Color DarkGray
    Write-Colored '  [ 0 ] Exit' -Color DarkGray
}

function Run-Simulator {
    param($state)

    if (-not (Test-Path -LiteralPath $Simulator -PathType Leaf)) {
        throw "Missing simulator entrypoint: $Simulator"
    }

    $python = Resolve-PythonCommand
    if ($state.BoardConfig) {
        & $python -c 'import yaml' 2>$null
        if ($LASTEXITCODE -ne 0) { throw 'UI bootstrap requires PyYAML. Install it in .venv-simulator.' }
    }

    $args = Build-LaunchArgs -state $state
    Show-Brand
    Write-Colored '  Launching simulator...' -Color Green
    Write-Colored ('  python -B ' + $Simulator + ' ' + ($args -join ' ')) -Color Gray
    & $python -B $Simulator @args
    if ($LASTEXITCODE -ne 0) {
        throw "Simulator failed with exit code $LASTEXITCODE."
    }
}

function Run-Interactive {
    $script:State = Copy-SimulatorState -Source $DefaultState

    while ($true) {
        Show-MainMenu
        $choice = Read-MenuInput -Prompt 'Select' -AllowedChoices $MenuKeys

        switch ($choice) {
            'u' { $script:State.BoardConfig = -not $script:State.BoardConfig }
            'U' { $script:State.BoardConfig = -not $script:State.BoardConfig }
            '1' { $script:State.Scenario = Select-CatalogValue -Title 'SCENARIO' -Catalog $ScenarioCatalog -Current $script:State.Scenario }
            '2' { $script:State.Screen = Select-CatalogValue -Title 'SCREEN' -Catalog $ScreenCatalog -Current $script:State.Screen }
            '3' { $script:State.IdleState = Select-CatalogValue -Title 'IDLE STATE' -Catalog $IdleStateCatalog -Current $script:State.IdleState }
            '4' { $script:State.SafeGuide = -not $script:State.SafeGuide }
            '5' { $script:State.NoSound = -not $script:State.NoSound }
            '6' { $script:State.PerformanceMode = -not $script:State.PerformanceMode }
            '7' { $script:State.WirelessRunning = -not $script:State.WirelessRunning }
            '8' {
                $capt = Edit-CapturePath -Current $script:State.Capture
                if ($capt -ne $null) {
                    if ([string]::IsNullOrWhiteSpace($capt)) { $script:State.Capture = $null }
                    else { $script:State.Capture = $capt }
                }
            }
            '9' {
                Run-Simulator -state $script:State
                return
            }
            'c' {
                if (Copy-LaunchCommand -state $script:State) { Write-Colored '  Command copied to clipboard.' -Color Green }
                else { Write-Colored '  Clipboard unavailable.' -Color Red }
                Start-Sleep -Milliseconds 700
            }
            'C' {
                if (Copy-LaunchCommand -state $script:State) { Write-Colored '  Command copied to clipboard.' -Color Green }
                else { Write-Colored '  Clipboard unavailable.' -Color Red }
                Start-Sleep -Milliseconds 700
            }
            'r' {
                $script:State = Copy-SimulatorState -Source $DefaultState
                Write-Colored '  Defaults restored.' -Color Green
                Start-Sleep -Milliseconds 500
            }
            'R' {
                $script:State = Copy-SimulatorState -Source $DefaultState
                Write-Colored '  Defaults restored.' -Color Green
                Start-Sleep -Milliseconds 500
            }
            'g' {
                Show-SwitchGuide
            }
            'G' {
                Show-SwitchGuide
            }
            'h' {
                if (Get-Command Show-RichHelp -ErrorAction SilentlyContinue) {
                    Show-RichHelp -ScriptPath $PSCommandPath
                }
                else {
                    Get-Help -Name $PSCommandPath -Full
                }
                if (Get-Command Wait-ForEnter -ErrorAction SilentlyContinue) { Wait-ForEnter } else { $null = Read-Host '  Press Enter to continue...' }
            }
            'H' {
                if (Get-Command Show-RichHelp -ErrorAction SilentlyContinue) {
                    Show-RichHelp -ScriptPath $PSCommandPath
                }
                else {
                    Get-Help -Name $PSCommandPath -Full
                }
                if (Get-Command Wait-ForEnter -ErrorAction SilentlyContinue) { Wait-ForEnter } else { $null = Read-Host '  Press Enter to continue...' }
            }
            default {
                if ($choice -eq '0' -or $choice -eq 'x' -or $choice -eq 'X' -or [string]::IsNullOrWhiteSpace($choice)) {
                    return
                }
            }
        }
    }
}

# Bound arguments select one explicit host-only route; no firmware tool is invoked.
if ($Cody) {
    foreach ($argumentName in $script:LauncherBoundParameters.Keys) {
        if ($argumentName -notin @('Cody','CodyScreen','CodyRotation','CodyAtMs','PythonExecutable','Capture','SafeGuide','Help')) {
            throw "-Cody cannot be combined with -$argumentName. Cody has no device-twin scenarios or service actions."
        }
    }
    $codyPython = if ($PythonExecutable) { $PythonExecutable } else { Resolve-PythonCommand }
    $codyEntry = Join-Path $PSScriptRoot 'cody_simulator.py'
    if (-not (Test-Path -LiteralPath $codyEntry -PathType Leaf)) { throw "Missing Cody preview: $codyEntry" }
    & $codyPython -c 'import PIL, yaml'
    if ($LASTEXITCODE -ne 0) { throw 'Cody preview requires Pillow and PyYAML. Select the simulator interpreter with -PythonExecutable.' }
    $codyArguments = @('--screen', $CodyScreen, '--rotation', [string]$CodyRotation, '--at-ms', [string]$CodyAtMs)
    if ($Capture) { $codyArguments += @('--capture', $Capture) }
    if ($SafeGuide) { $codyArguments += '--safe-guide' }
    if ($Help) { $codyArguments = @('--help') }
    & $codyPython -B $codyEntry @codyArguments
    if ($LASTEXITCODE -ne 0) { throw "Cody preview failed with exit code $LASTEXITCODE." }
    return
}
foreach ($codyOption in @('CodyScreen','CodyRotation','CodyAtMs','PythonExecutable')) {
    if ($script:LauncherBoundParameters.ContainsKey($codyOption)) { throw "-$codyOption requires -Cody." }
}

if ($Help) {
    Initialize-SimulatorUi | Out-Null
    if (Get-Command Show-RichHelp -ErrorAction SilentlyContinue) { Show-RichHelp -ScriptPath $PSCommandPath }
    else { Get-Help -Name $PSCommandPath -Full }
    return
}

$headlessMode = $false
foreach ($p in @(
    'ListScenarios','ValidateScenarios','DumpSnapshot','DumpApi',
    'Action','AdvanceMs','Output','DumpBoardShell','ShellTaps'
)) {
    if ($script:LauncherBoundParameters.ContainsKey($p)) { $headlessMode = $true; break }
}

if ($headlessMode) {
    Invoke-HeadlessSimulator -BoundParameters $script:LauncherBoundParameters
    return
}

Initialize-SimulatorUi | Out-Null

$directMode = $false
foreach ($p in @('BoardConfig','Scenario','ScenarioFile','AtMs','Screen','IdleState','Capture','SafeGuide','NoSound','PerformanceMode','WirelessRunning')) {
    if ($PSBoundParameters.ContainsKey($p)) { $directMode = $true; break }
}

if (-not $directMode) {
    Run-Interactive
    return
}

$direct = Copy-SimulatorState -Source $DefaultState
if ($BoardConfig) {
    foreach ($setting in @('Scenario','ScenarioFile','AtMs','Screen','IdleState','SafeGuide','NoSound','PerformanceMode','WirelessRunning')) {
        if ($PSBoundParameters.ContainsKey($setting)) {
            throw "-BoardConfig cannot be combined with -$setting. The minimal shell has no device pages or scenarios."
        }
    }
}
$direct.BoardConfig = [bool]$BoardConfig
if ($PSBoundParameters.ContainsKey('ScenarioFile') -and $PSBoundParameters.ContainsKey('Scenario')) {
    throw 'Use either -Scenario or -ScenarioFile, not both.'
}
$direct.Scenario = $Scenario
$direct.ScenarioFile = $ScenarioFile
$direct.AtMs = $AtMs
$direct.Screen = $Screen
$direct.IdleState = $IdleState
$direct.Capture = $Capture
$direct.SafeGuide = [bool]$SafeGuide
$direct.NoSound = [bool]$NoSound
$direct.PerformanceMode = [bool]$PerformanceMode
$direct.WirelessRunning = [bool]$WirelessRunning

Run-Simulator -state $direct
