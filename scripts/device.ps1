<#
.SYNOPSIS
Unified QSee.ai firmware, serial, asset, verification, and repository utility.

.DESCRIPTION
Run without parameters to open the interactive menu. Pass an action and any
options for prompt-free CLI execution. This is the canonical PowerShell
entrypoint; legacy script names under scripts are thin compatibility shims.

.PARAMETER Action
Workflow to run. Firmware actions retain their historical positional syntax.

.PARAMETER NonInteractive
Run the historical default action, build, when no Action is supplied.

.PARAMETER StopStaleMonitors
Allow serial workflows to close matching stale monitor processes. Without this
explicit switch, they report the conflicting processes and stop safely.

.PARAMETER NoReset
Capture serial output from the already running firmware without pulsing EN.

.EXAMPLE
pwsh -NoProfile -File .\scripts\device.ps1

Opens the interactive menu.

.EXAMPLE
pwsh -NoProfile -File .\scripts\device.ps1 build -BuildDir build-brookesia

Runs a prompt-free incremental firmware build.

.EXAMPLE
pwsh -NoProfile -File .\scripts\device.ps1 assets -NoProgress

Regenerates all device assets without progress rendering.

.EXAMPLE
pwsh -NoProfile -File .\scripts\device.ps1 source-banner -Apply

Applies the canonical QSee.ai banner to owned sources.

.EXAMPLE
pwsh -NoProfile -File .\scripts\device.ps1 settings -Port COM12 -SaveSettings

Persists local script defaults under the ignored scripts/logs folder.

.EXAMPLE
pwsh -NoProfile -File .\scripts\device.ps1 monitor -Port COM12 -StopStaleMonitors

Closes only matching stale monitor processes after explicit opt-in.

.EXAMPLE
pwsh -NoProfile -File .\scripts\device.ps1 capture -Port COM12 -NoReset

Captures the running device without restarting it.
#>

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet(
        'build', 'clean', 'fullclean', 'flash', 'monitor', 'capture', 'erase',
        'reconfigure', 'set-target', 'combo', 'ports', 'recover', 'assets',
        'boot-assets', 'sfx', 'pairing', 'source-banner', 'settings', 'help'
    )]
    [string]$Action,

    [string]$Port,
    [int]$Baud,
    [string]$BuildDir,
    [int]$WaitSeconds,
    [int]$DurationSeconds,
    [string]$OutputPath,
    [int]$Jobs,

    [string]$IdfPath,
    [string]$IdfToolsPath,
    [string]$IdfPythonEnvPath,
    [string]$OpenOcdScripts,
    [string]$EspRomElfDir,
    [string]$AssetPython,

    [string]$ConfigPath,
    [string]$BootSource,
    [string]$BootOutputDirectory,

    [switch]$Apply,
    [switch]$Remove,
    [string]$RepositoryRoot,
    [string[]]$SourceRoot,

    [switch]$SaveSettings,
    [switch]$ResetSettings,
    [switch]$StopStaleMonitors,
    [switch]$NoReset,
    [switch]$NonInteractive,
    [switch]$NoProgress,
    [switch]$NoBanner,
    [Alias('h')][switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$runStart = Get-Date
$scriptName = [System.IO.Path]::GetFileName($PSCommandPath)
$projectRoot = Split-Path -Parent $PSScriptRoot
$modulesRoot = Join-Path $PSScriptRoot 'modules'
$logsRoot = Join-Path $PSScriptRoot 'logs'
$defaultsPath = Join-Path $PSScriptRoot 'settings.defaults.json'
$localSettingsPath = Join-Path $logsRoot 'settings.json'
$explicitParameters = @{}
foreach ($parameter in $PSBoundParameters.GetEnumerator()) {
    $explicitParameters[$parameter.Key] = $parameter.Value
}
$interactive = $PSBoundParameters.Count -eq 0
$loggerInitialized = $false
$footerShown = $false
$cancelled = $false

$runtimeModulePath = Join-Path $modulesRoot 'qsee-runtime.ps1'
if (-not (Test-Path -LiteralPath $runtimeModulePath -PathType Leaf)) {
    throw "Required script module is missing: $runtimeModulePath"
}
. $runtimeModulePath

$requiredModules = @(
    'qsee-device-operations.ps1',
    'qsee-assets.ps1',
    'qsee-source-banner.ps1'
)

function Select-QSeeMainAction {
    [CmdletBinding()]
    param()

    return Select-QSeeMenuItem -Title 'QSee.ai development console' -Prompt 'Action' -Items @(
        [pscustomobject]@{ Key = '1'; Label = 'Build firmware'; Description = 'Incremental ESP-IDF build'; Value = 'build' }
        [pscustomobject]@{ Key = '2'; Label = 'Flash firmware'; Description = 'Flash the selected serial port'; Value = 'flash' }
        [pscustomobject]@{ Key = '3'; Label = 'Monitor serial'; Description = 'Open the ESP-IDF monitor'; Value = 'monitor' }
        [pscustomobject]@{ Key = '4'; Label = 'Capture serial'; Description = 'Save one bounded diagnostic capture'; Value = 'capture' }
        [pscustomobject]@{ Key = '5'; Label = 'Build, flash, monitor'; Description = 'Run the complete device loop'; Value = 'combo' }
        [pscustomobject]@{ Key = '6'; Label = 'List serial ports'; Description = 'Show Windows port state'; Value = 'ports' }
        [pscustomobject]@{ Key = 'A'; Label = 'Generate all assets'; Description = 'Boot graphics and SFX'; Value = 'assets' }
        [pscustomobject]@{ Key = 'B'; Label = 'Generate boot assets'; Description = 'PNG and RGB565 outputs'; Value = 'boot-assets' }
        [pscustomobject]@{ Key = 'C'; Label = 'Generate SFX'; Description = 'Normalized device WAV and manifest'; Value = 'sfx' }
        [pscustomobject]@{ Key = 'D'; Label = 'Verify pairing'; Description = 'Hardware-free pairing checks'; Value = 'pairing' }
        [pscustomobject]@{ Key = 'E'; Label = 'Source banners'; Description = 'Preview or update owned source headers'; Value = 'source-banner' }
        [pscustomobject]@{ Key = 'M'; Label = 'Firmware maintenance'; Description = 'Clean, configure, recover, or erase'; Value = 'maintenance' }
        [pscustomobject]@{ Key = 'S'; Label = 'Settings'; Description = 'Inspect or save local defaults'; Value = 'settings' }
        [pscustomobject]@{ Key = 'H'; Label = 'Help'; Description = 'Show CLI parameters and examples'; Value = 'help' }
    )
}

function Select-QSeeMaintenanceAction {
    [CmdletBinding()]
    param()

    return Select-QSeeMenuItem -Title 'Firmware maintenance' -Prompt 'Action' `
        -QuitKey 'B' -QuitLabel 'Back' -Items @(
            [pscustomobject]@{ Key = '1'; Label = 'Clean'; Description = 'Remove intermediate build outputs'; Value = 'clean' }
            [pscustomobject]@{ Key = '2'; Label = 'Full clean'; Description = 'Reset the ESP-IDF build directory'; Value = 'fullclean' }
            [pscustomobject]@{ Key = '3'; Label = 'Reconfigure'; Description = 'Regenerate ESP-IDF configuration'; Value = 'reconfigure' }
            [pscustomobject]@{ Key = '4'; Label = 'Set ESP32-S3 target'; Description = 'Select the project MCU target'; Value = 'set-target' }
            [pscustomobject]@{ Key = '5'; Label = 'Recover'; Description = 'Build and make one direct flash attempt'; Value = 'recover' }
            [pscustomobject]@{ Key = '6'; Label = 'Erase flash'; Description = 'Destructive; confirmation required'; Value = 'erase' }
        )
}

function Select-QSeeSourceBannerMode {
    [CmdletBinding()]
    param()

    return Select-QSeeMenuItem -Title 'QSee.ai source banners' -Prompt 'Mode' `
        -QuitKey 'B' -QuitLabel 'Back' -Items @(
            [pscustomobject]@{
                Key = '1'; Label = 'Preview additions'; Description = 'No source writes'
                Value = [pscustomobject]@{ Apply = $false; Remove = $false }
            }
            [pscustomobject]@{
                Key = '2'; Label = 'Apply additions'; Description = 'Write missing canonical banners'
                Value = [pscustomobject]@{ Apply = $true; Remove = $false }
            }
            [pscustomobject]@{
                Key = '3'; Label = 'Preview removals'; Description = 'No source writes'
                Value = [pscustomobject]@{ Apply = $false; Remove = $true }
            }
            [pscustomobject]@{
                Key = '4'; Label = 'Apply removals'; Description = 'Remove exact canonical banners'
                Value = [pscustomobject]@{ Apply = $true; Remove = $true }
            }
        )
}

function Read-QSeeIntegerSetting {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Label,
        [Parameter(Mandatory)][int]$Current,
        [int]$Minimum = 0
    )

    while ($true) {
        $inputValue = (Read-Host "  $Label [$Current]").Trim()
        if ([string]::IsNullOrWhiteSpace($inputValue)) {
            return $Current
        }

        $parsed = 0
        if ([int]::TryParse($inputValue, [ref]$parsed) -and $parsed -ge $Minimum) {
            return $parsed
        }
        Write-Host "  Enter an integer greater than or equal to $Minimum." -ForegroundColor Red
    }
}

# Preserve the current value when an advanced setting is left blank.
function Read-QSeeStringSetting {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Label,
        [Parameter(Mandatory)][AllowEmptyString()][string]$Current
    )

    $inputValue = (Read-Host "  $Label [$Current]").Trim()
    if ([string]::IsNullOrWhiteSpace($inputValue)) {
        return $Current
    }
    return $inputValue
}

# Inspect, persist or explicitly reset the local CLI settings override.
function Invoke-QSeeSettingsWorkflow {
    [CmdletBinding()]
    param(
        [AllowNull()][object]$Settings,
        [Parameter(Mandatory)][string]$DefaultsFile,
        [Parameter(Mandatory)][string]$LocalFile,
        [switch]$Interactive,
        [switch]$Save,
        [switch]$Reset
    )

    if ($Save -and $Reset) {
        throw '-SaveSettings and -ResetSettings cannot be used together.'
    }

    if ($Reset) {
        if (Test-Path -LiteralPath $LocalFile -PathType Leaf) {
            Remove-Item -LiteralPath $LocalFile -Force
            Write-QSeeSuccess "Removed local settings: $LocalFile"
        }
        else {
            Write-QSeeInfo 'No local settings override exists.'
        }
        return
    }

    if ($null -eq $Settings) {
        throw 'Settings are required unless the local override is being reset.'
    }

    if ($Save) {
        Save-QSeeSettings -Settings $Settings -LocalPath $LocalFile
        Write-QSeeSuccess "Saved local settings: $LocalFile"
        return
    }

    if (-not $Interactive) {
        Show-QSeeSettings -Settings $Settings -DefaultsPath $DefaultsFile -LocalPath $LocalFile
        return
    }

    while ($true) {
        Show-QSeeSettings -Settings $Settings -DefaultsPath $DefaultsFile -LocalPath $LocalFile
        $selection = Select-QSeeMenuItem -Title 'Settings' -Prompt 'Action' `
            -QuitKey 'B' -QuitLabel 'Back' -Items @(
                [pscustomobject]@{ Key = '1'; Label = 'Update local defaults'; Description = 'Stored under scripts/logs'; Value = 'update' }
                [pscustomobject]@{ Key = '2'; Label = 'Reset local defaults'; Description = 'Return to checked-in defaults'; Value = 'reset' }
            )
        if ($null -eq $selection) {
            return
        }

        if ($selection.Value -eq 'reset') {
            $confirmation = (Read-Host '  Type RESET to remove the local override').Trim()
            if ($confirmation -ceq 'RESET') {
                if (Test-Path -LiteralPath $LocalFile -PathType Leaf) {
                    Remove-Item -LiteralPath $LocalFile -Force
                    Write-QSeeSuccess 'Local settings reset.'
                }
                else {
                    Write-QSeeInfo 'No local settings override exists.'
                }
            }
            else {
                Write-QSeeWarning 'Settings reset cancelled.'
            }
            return
        }

        $portValue = (Read-Host "  Port [$($Settings.Port)]").Trim()
        if (-not [string]::IsNullOrWhiteSpace($portValue)) {
            $Settings.Port = $portValue.ToUpperInvariant()
        }
        $buildValue = (Read-Host "  Build directory [$($Settings.BuildDir)]").Trim()
        if (-not [string]::IsNullOrWhiteSpace($buildValue)) {
            $Settings.BuildDir = $buildValue
        }
        $Settings.Baud = Read-QSeeIntegerSetting -Label 'Baud' `
            -Current $Settings.Baud -Minimum 1
        $Settings.WaitSeconds = Read-QSeeIntegerSetting -Label 'Port wait seconds' `
            -Current $Settings.WaitSeconds -Minimum 1
        $Settings.DurationSeconds = Read-QSeeIntegerSetting -Label 'Capture seconds' `
            -Current $Settings.DurationSeconds -Minimum 1
        $Settings.Jobs = Read-QSeeIntegerSetting -Label 'Build jobs (0 = automatic)' `
            -Current $Settings.Jobs -Minimum 0
        $Settings.IdfPath = Read-QSeeStringSetting -Label 'ESP-IDF path' `
            -Current $Settings.IdfPath
        $Settings.IdfToolsPath = Read-QSeeStringSetting -Label 'ESP-IDF tools path' `
            -Current $Settings.IdfToolsPath
        $Settings.IdfPythonEnvPath = Read-QSeeStringSetting -Label 'ESP-IDF Python environment' `
            -Current $Settings.IdfPythonEnvPath
        $Settings.OpenOcdScripts = Read-QSeeStringSetting -Label 'OpenOCD scripts path' `
            -Current $Settings.OpenOcdScripts
        $Settings.EspRomElfDir = Read-QSeeStringSetting -Label 'ESP ROM ELF directory' `
            -Current $Settings.EspRomElfDir
        $Settings.AssetPython = Read-QSeeStringSetting -Label 'Asset Python command' `
            -Current $Settings.AssetPython
        $Settings.Port = ConvertTo-QSeeSerialPortName -Port $Settings.Port
        Save-QSeeSettings -Settings $Settings -LocalPath $LocalFile
        Write-QSeeSuccess 'Local settings updated.'
        return
    }
}

try {
    Initialize-QSeeRuntime -ClearScreen:$interactive
    Initialize-QSeeLogger -ScriptName $scriptName -LogsDirectory $logsRoot
    $loggerInitialized = $true
    $logPath = Get-QSeeLogPath

    if (-not $NoBanner) {
        Show-QSeeBanner
    }

    if ($Help) {
        $Action = 'help'
    }
    elseif ($interactive) {
        $selection = Select-QSeeMainAction
        if ($null -eq $selection) {
            $cancelled = $true
        }
        else {
            $Action = [string]$selection.Value
            # Returning from maintenance reopens the main menu.
            while ($Action -eq 'maintenance') {
                $maintenanceSelection = Select-QSeeMaintenanceAction
                if ($null -eq $maintenanceSelection) {
                    $selection = Select-QSeeMainAction
                    if ($null -eq $selection) {
                        $cancelled = $true
                        break
                    }
                    $Action = [string]$selection.Value
                }
                else {
                    $Action = [string]$maintenanceSelection.Value
                }
            }
        }
    }
    elseif ([string]::IsNullOrWhiteSpace($Action)) {
        # Preserve the historical parameterized default while reserving no args for the menu.
        $Action = 'build'
    }

    if ($cancelled) {
        Write-QSeeLog -Message 'Interactive session cancelled.' -Level WARN
        Show-QSeeFooter -ScriptName $scriptName -Status 'CANCELLED' `
            -StatusColor Yellow -StartTime $runStart -LogPath $logPath
        $footerShown = $true
        return
    }

    $Action = $Action.ToLowerInvariant()
    if ($interactive -and $Action -eq 'source-banner') {
        $bannerSelection = Select-QSeeSourceBannerMode
        if ($null -eq $bannerSelection) {
            Write-QSeeLog -Message 'Source-banner menu cancelled.' -Level WARN
            Show-QSeeFooter -ScriptName $scriptName -Status 'CANCELLED' `
                -StatusColor Yellow -StartTime $runStart -LogPath $logPath
            $footerShown = $true
            return
        }
        $Apply = [bool]$bannerSelection.Value.Apply
        $Remove = [bool]$bannerSelection.Value.Remove
    }

    # Help and explicit reset remain available even when local settings are invalid.
    if ($Action -eq 'help') {
        Show-QSeeCommandHeader -Action $Action -Settings $null -LogPath $logPath
        Write-QSeeLog -Message "Selected action: $Action"
        Get-Help -Name $PSCommandPath -Full | Out-Host
        Show-QSeeFooter -ScriptName $scriptName -Status 'HELP' `
            -StatusColor Cyan -StartTime $runStart -LogPath $logPath
        $footerShown = $true
        return
    }

    if ($Action -eq 'settings' -and $ResetSettings) {
        Show-QSeeCommandHeader -Action $Action -Settings $null -LogPath $logPath
        Write-QSeeLog -Message 'Selected action: settings reset'
        Invoke-QSeeSettingsWorkflow -Settings $null -DefaultsFile $defaultsPath `
            -LocalFile $localSettingsPath -Save:$SaveSettings -Reset
        Show-QSeeFooter -ScriptName $scriptName -Status 'SETTINGS RESET' `
            -StatusColor Cyan -StartTime $runStart -LogPath $logPath
        $footerShown = $true
        return
    }

    $settings = Get-QSeeSettings -DefaultsPath $defaultsPath -LocalPath $localSettingsPath
    $overrides = @{
        Port = 'Port'
        Baud = 'Baud'
        BuildDir = 'BuildDir'
        WaitSeconds = 'WaitSeconds'
        DurationSeconds = 'DurationSeconds'
        Jobs = 'Jobs'
        IdfPath = 'IdfPath'
        IdfToolsPath = 'IdfToolsPath'
        IdfPythonEnvPath = 'IdfPythonEnvPath'
        OpenOcdScripts = 'OpenOcdScripts'
        EspRomElfDir = 'EspRomElfDir'
        AssetPython = 'AssetPython'
    }
    foreach ($parameterName in $overrides.Keys) {
        if ($explicitParameters.ContainsKey($parameterName)) {
            $settings.($overrides[$parameterName]) = $explicitParameters[$parameterName]
        }
    }

    $settings.Port = ConvertTo-QSeeSerialPortName -Port $settings.Port
    if ([string]::IsNullOrWhiteSpace($settings.BuildDir)) {
        throw 'BuildDir cannot be empty.'
    }
    if ($settings.Baud -le 0 -or $settings.WaitSeconds -le 0 -or $settings.DurationSeconds -le 0) {
        throw 'Baud, WaitSeconds, and DurationSeconds must be positive.'
    }
    if ($settings.Jobs -lt 0) {
        throw 'Jobs cannot be negative.'
    }

    if (-not $explicitParameters.ContainsKey('ConfigPath')) {
        $ConfigPath = Join-Path $projectRoot 'assets\sfx\sfx-config.json'
    }
    $ConfigPath = [System.IO.Path]::GetFullPath($ConfigPath)

    if (-not $explicitParameters.ContainsKey('BootSource')) {
        $BootSource = Join-Path $projectRoot 'assets\branding\qsee\source\qsee-ai-logo.pdf'
    }
    $BootSource = [System.IO.Path]::GetFullPath($BootSource)

    if (-not $explicitParameters.ContainsKey('BootOutputDirectory')) {
        $BootOutputDirectory = Join-Path $projectRoot 'assets\branding\qsee\generated'
    }
    $BootOutputDirectory = [System.IO.Path]::GetFullPath($BootOutputDirectory)

    if (-not $explicitParameters.ContainsKey('RepositoryRoot')) {
        $RepositoryRoot = $projectRoot
    }
    if (-not $explicitParameters.ContainsKey('SourceRoot')) {
        $SourceRoot = @(
            'main',
            'components/qsee_app',
            'components/qsee_boot',
            'components/qsee_logger',
            'components/qsee_platform'
        )
    }

    if ($interactive -and $Action -eq 'erase') {
        $confirmation = (Read-Host "  Type ERASE to erase $($settings.Port)").Trim()
        if ($confirmation -cne 'ERASE') {
            Write-QSeeWarning 'Erase cancelled.'
            Show-QSeeFooter -ScriptName $scriptName -Status 'CANCELLED' `
                -StatusColor Yellow -StartTime $runStart -LogPath $logPath
            $footerShown = $true
            return
        }
    }

    Show-QSeeCommandHeader -Action $Action -Settings $settings -LogPath $logPath
    Write-QSeeLog -Message "Selected action: $Action"

    if ($Action -eq 'settings') {
        Invoke-QSeeSettingsWorkflow -Settings $settings -DefaultsFile $defaultsPath `
            -LocalFile $localSettingsPath -Interactive:$interactive `
            -Save:$SaveSettings -Reset:$ResetSettings
        Show-QSeeFooter -ScriptName $scriptName -Status 'SETTINGS COMPLETE' `
            -StatusColor Cyan -StartTime $runStart -LogPath $logPath
        $footerShown = $true
        return
    }

    foreach ($moduleName in $requiredModules) {
        $modulePath = Join-Path $modulesRoot $moduleName
        if (-not (Test-Path -LiteralPath $modulePath -PathType Leaf)) {
            throw "Required script module is missing: $modulePath"
        }
        . $modulePath
    }

    $firmwareActions = @(
        'build', 'clean', 'fullclean', 'flash', 'monitor', 'erase',
        'reconfigure', 'set-target', 'combo', 'recover'
    )
    if ($Action -in $firmwareActions) {
        Initialize-QSeeIdfEnvironment -Settings $settings
    }

    Invoke-QSeePreflight -Action $Action -Settings $settings `
        -ProjectRoot $projectRoot -ScriptsRoot $PSScriptRoot `
        -ConfigPath $ConfigPath -BootSource $BootSource | Out-Null

    switch ($Action) {
        { $_ -in ($firmwareActions + @('ports', 'capture')) } {
            Invoke-QSeeFirmwareAction -Action $Action -ProjectRoot $projectRoot `
                -ScriptsRoot $PSScriptRoot -Settings $settings -OutputPath $OutputPath `
                -StopStaleMonitors:$StopStaleMonitors -NoReset:$NoReset
            break
        }
        'assets' {
            Invoke-QSeeAllAssetGeneration -ScriptsRoot $PSScriptRoot `
                -PythonCommand $settings.AssetPython -ConfigPath $ConfigPath `
                -BootSource $BootSource -BootOutputDirectory $BootOutputDirectory `
                -NoProgress:$NoProgress | Out-Null
            break
        }
        'boot-assets' {
            Invoke-QSeeBootAssetGeneration -ScriptsRoot $PSScriptRoot `
                -PythonCommand $settings.AssetPython -Source $BootSource `
                -OutputDirectory $BootOutputDirectory
            break
        }
        'sfx' {
            Invoke-QSeeSfxGeneration -ConfigPath $ConfigPath `
                -NoProgress:$NoProgress | Out-Null
            break
        }
        'pairing' {
            $pairingScript = Join-Path $PSScriptRoot 'verify_pairing.py'
            Invoke-QSeeNativeCommand -FilePath $settings.AssetPython `
                -ArgumentList @($pairingScript) `
                -FailureMessage 'Pairing verification failed'
            break
        }
        'source-banner' {
            Invoke-QSeeSourceBanner -RepositoryRoot $RepositoryRoot `
                -SourceRoot $SourceRoot -Apply:$Apply -Remove:$Remove `
                -NoProgress:$NoProgress | Out-Null
            break
        }
        default {
            throw "Unsupported action: $Action"
        }
    }

    Write-QSeeSuccess "Action '$Action' completed."
    Show-QSeeFooter -ScriptName $scriptName -Status 'COMPLETED' `
        -StartTime $runStart -LogPath $logPath
    $footerShown = $true
}
catch {
    if ($loggerInitialized) {
        Write-QSeeError $_.Exception.Message
    }
    if (-not $footerShown -and (Get-Command Show-QSeeErrorFooter -ErrorAction SilentlyContinue)) {
        Show-QSeeErrorFooter -ScriptName $scriptName `
            -ErrorMessage $_.Exception.Message -StartTime $runStart `
            -LogPath $(if ($loggerInitialized) { Get-QSeeLogPath } else { $null })
    }
    throw
}
finally {
    Write-Progress -Id 20 -Activity 'Generating QSee.ai SFX' -Completed
    Write-Progress -Id 30 -Activity 'QSee.ai source banners' -Completed
    if ($loggerInitialized) {
        Close-QSeeLogger
    }
}
