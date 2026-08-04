<#!
.SYNOPSIS
Launch the QSee Windows simulator with an interactive terminal UI or direct arguments.

.DESCRIPTION
If no arguments are passed, this script shows a branded guided menu to configure
scenario, screen, idle-state and switches before launching.

If arguments are passed, it starts directly using them.

.PARAMETER Scenario
Simulation scenario to start.

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

.PARAMETER Help
Show full help and exit.

.EXAMPLE
.\run-simulator.ps1
# Opens the interactive menu.

.EXAMPLE
.\run-simulator.ps1 -Scenario battery-low -Screen battery -NoSound
# Launches directly with explicit args.
#>
[CmdletBinding()]
param(
    [ValidateSet('healthy','sd-unformatted','gps-searching','battery-low','battery-charging','battery-unavailable')]
    [string]$Scenario = 'healthy',

    [ValidateSet(
        'home','battery','logs','settings','storage','chrome-settings','power-settings','hardware',
        'audio','fft','fft-detail','imu','gps','tof','tof-settings',
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
    [switch]$Help
)

$ErrorActionPreference = 'Stop'

$ModulesPath = 'E:\tools\scripts\modules'
$Simulator = Join-Path $PSScriptRoot 'qsee_simulator.py'

$BrandName = 'QSee Simulator Console'
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
    Scenario = 'healthy'
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
    [pscustomobject]@{ Name = 'healthy'; Description = 'Nominal startup with valid sensor values.' },
    [pscustomobject]@{ Name = 'sd-unformatted'; Description = 'SD unformatted workflow and repair path.' },
    [pscustomobject]@{ Name = 'gps-searching'; Description = 'Force GPS searching / no-fix behavior.' },
    [pscustomobject]@{ Name = 'battery-low'; Description = 'Battery low warnings and alert bars.' },
    [pscustomobject]@{ Name = 'battery-charging'; Description = 'Simulate active charging with charging indicators.' },
    [pscustomobject]@{ Name = 'battery-unavailable'; Description = 'Battery telemetry unavailable state.' }
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

$MenuKeys = @('1','2','3','4','5','6','7','8','9','c','C','r','R','h','H','g','G','0','x','X')

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

    $args = @('--scenario', $state.Scenario, '--screen', $state.Screen, '--idle-state', $state.IdleState)
    if (-not [string]::IsNullOrWhiteSpace($state.Capture)) { $args += @('--capture', $state.Capture) }
    if ($state.SafeGuide) { $args += '--safe-guide' }
    if ($state.NoSound) { $args += '--no-sound' }
    if ($state.PerformanceMode) { $args += '--performance-mode' }
    if ($state.WirelessRunning) { $args += '--wireless-running' }
    return $args
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
        [pscustomobject]@{ Flag = '--wireless-running'; Desc = 'Enable wireless running state.' }
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

    $python = $null
    foreach ($candidate in @('python', 'python3', 'py')) {
        if (Get-Command $candidate -ErrorAction SilentlyContinue) { $python = $candidate; break }
    }

    if (-not $python) {
        throw 'Python is not available in PATH. Please install Python 3 and retry.'
    }

    $args = Build-LaunchArgs -state $state
    Show-Brand
    Write-Colored '  Launching simulator...' -Color Green
    Write-Colored ('  python -B ' + $Simulator + ' ' + ($args -join ' ')) -Color Gray
    & $python -B $Simulator @args
}

function Run-Interactive {
    $script:State = Copy-SimulatorState -Source $DefaultState

    while ($true) {
        Show-MainMenu
        $choice = Read-MenuInput -Prompt 'Select' -AllowedChoices $MenuKeys

        switch ($choice) {
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

Initialize-SimulatorUi | Out-Null

if ($Help) {
    if (Get-Command Show-RichHelp -ErrorAction SilentlyContinue) { Show-RichHelp -ScriptPath $PSCommandPath }
    else { Get-Help -Name $PSCommandPath -Full }
    return
}

$directMode = $false
foreach ($p in @('Scenario','Screen','IdleState','Capture','SafeGuide','NoSound','PerformanceMode','WirelessRunning')) {
    if ($PSBoundParameters.ContainsKey($p)) { $directMode = $true; break }
}

if (-not $directMode) {
    Run-Interactive
    return
}

$direct = Copy-SimulatorState -Source $DefaultState
$direct.Scenario = $Scenario
$direct.Screen = $Screen
$direct.IdleState = $IdleState
$direct.Capture = $Capture
$direct.SafeGuide = [bool]$SafeGuide
$direct.NoSound = [bool]$NoSound
$direct.PerformanceMode = [bool]$PerformanceMode
$direct.WirelessRunning = [bool]$WirelessRunning

Run-Simulator -state $direct
