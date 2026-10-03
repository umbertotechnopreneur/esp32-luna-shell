# The same owned MenuManager used by the Luna launcher, bundled in this checkout.

# Title labels the current menu page.
# Choices maps short selection keys to English descriptions and literal command values.
# IsMainMenu shows Exit for the root page and Back for submenus.
# Throws when the console cannot read input; choosing zero or Escape returns no selection.
function Select-DeviceMenuItem([string]$Title, [System.Collections.IDictionary]$Choices, [switch]$IsMainMenu) {
    $items = foreach ($key in $Choices.Keys) {
        [pscustomobject]@{
            Number = $key
            Emoji = $Choices[$key].Emoji
            Label = $Choices[$key].Label
            Desc = $Choices[$key].Description
        }
    }
    Write-MenuPage -Title $Title -Items $items -ShowExit:$IsMainMenu -ShowBack:(-not $IsMainMenu)
    while ($true) {
        $choice = Read-MenuChoice -Prompt 'Select' -AllowedChoices (@($Choices.Keys) + @('0')) -IgnoreEmptyInput
        if ($null -eq $choice -or $choice -eq '0') { return $null }
        if ($Choices.Contains($choice)) { return $Choices[$choice].Value }
        Write-Host "Unknown selection '$choice'. Choose one of the displayed numbers." -ForegroundColor Yellow
    }
}

# Settings contains the current session's explicit connection/preview options.
# Throws for an invalid edited value; the caller keeps the previous setting.
function Edit-DeviceMenuSettings([hashtable]$Settings) {
    $portDescription = if ($Settings.Port) {
        $Settings.Port
    } else {
        'Choose from detected ports when a device action starts'
    }
    $options = [ordered]@{
        '1' = @{ Emoji = '🔌'; Label = 'Serial port'; Description = $portDescription; Value = 'Port' }
        '2' = @{ Emoji = '🔄'; Label = 'Screen rotation'; Description = [string]$Settings.Rotation; Value = 'Rotation' }
        '3' = @{ Emoji = '🐍'; Label = 'Host Python executable'; Description = $Settings.PythonExecutable; Value = 'PythonExecutable' }
        '4' = @{ Emoji = '🎯'; Label = 'Firmware profile'; Description = $Settings.Profile; Value = 'Profile' }
        '5' = @{ Emoji = '📋'; Label = 'Board YAML'; Description = $Settings.BoardConfig; Value = 'BoardConfig' }
        '6' = @{ Emoji = '⚙️'; Label = 'Firmware build mode'; Description = $Settings.BuildMode; Value = 'BuildMode' }
    }
    $key = Select-DeviceMenuItem 'SESSION SETTINGS' $options
    if (-not $key) { return }
    if ($key -eq 'BuildMode') {
        $modes = [ordered]@{
            '1' = @{ Emoji = '🐞'; Label = 'Debug'; Description = 'Diagnostic logs; automatic Light-sleep disabled for USB debugging'; Value = 'Debug' }
            '2' = @{ Emoji = '🚀'; Label = 'Release'; Description = 'Warnings/errors; rendering diagnostics off; automatic Light-sleep allowed'; Value = 'Release' }
        }
        $mode = Select-DeviceMenuItem 'FIRMWARE BUILD MODE (EXPLICIT SELECTION)' $modes
        if ($mode) { $Settings.BuildMode = $mode }
        return
    }
    $prompt = if ($key -eq 'Port') {
        '  Port (COM10 or 10; ask restores port selection; blank keeps the current value)'
    } else {
        "  $key (blank keeps the current value)"
    }
    $value = Read-Host $prompt
    if ([string]::IsNullOrWhiteSpace($value)) { return }
    switch ($key) {
        Port {
            if ($value.Trim() -eq 'ask') {
                $Settings.Remove('Port')
                return
            }
            . (Join-Path $ProjectRoot 'scripts/common/SerialPorts.ps1')
            $value = Normalize-PortName $value
        }
        Rotation { if ($value -notin @('0','90','180','270')) { throw 'Rotation must be 0, 90, 180 or 270.' }; $value = [int]$value }
        Profile {
            if ($value -notin @('full','connected','ui-minimal','display-touch','headless','leg-edition','cody-companion')) { throw 'Unknown firmware profile.' }
            $Settings.Remove('BoardConfig')
        }
        BoardConfig {
            $board = if ([IO.Path]::IsPathRooted($value)) { $value } else { Join-Path $ProjectRoot $value }
            if (-not (Test-Path -LiteralPath $board -PathType Leaf)) { throw "Board YAML not found: $board" }
            $Settings.Remove('Profile'); $Settings.Remove('Feature')
        }
        PythonExecutable { $value = Resolve-HostPython $value }
    }
    $Settings[$key] = $value
}

# InitialSettings contains explicitly supplied startup preferences for this menu session.
# Throws for a missing shared menu module; individual command failures stay visible in the menu.
function Show-DeviceMenu([System.Collections.IDictionary]$InitialSettings) {
    . (Join-Path $ProjectRoot 'scripts/common/Errors.ps1')
    if ([Console]::IsInputRedirected) { throw 'The interactive menu requires a terminal. Use device.ps1 help or an explicit CLI action.' }
    Import-Module (Join-Path $ProjectRoot 'scripts/common/MenuManager/MenuManager.psd1') -Force -ErrorAction Stop
    $settings = @{ Rotation = 0; Jobs = 8; BuildMode = 'Debug' }
    foreach ($key in $InitialSettings.Keys) {
        if ($key -notin @('Action')) { $settings[$key] = $InitialSettings[$key] }
    }
    $actions = Get-DeviceActionParameters
    while ($true) {
        $selector = if ($settings.BoardConfig) { $settings.BoardConfig } elseif ($settings.Profile) { $settings.Profile } else { 'cody-companion' }
        $portDescription = if ($settings.Port) {
            $settings.Port
        } else {
            'Choose when needed'
        }
        Write-Host "`n  CODY COMPANION  /  DEVELOPMENT CONSOLE" -ForegroundColor Cyan
        Write-Host "  Firmware: $selector    Build mode: $($settings.BuildMode)    Port: $portDescription    Rotation: $($settings.Rotation) degrees" -ForegroundColor Gray
        Write-Host '  Output: artifacts/    Opening the menu does not access hardware.' -ForegroundColor DarkGray
        $main = [ordered]@{
            '1' = @{ Emoji = '🤖'; Label = 'Cody simulator'; Description = 'Rig, face, contour and debug overlay'; Value = 'simulator' }
            '2' = @{ Emoji = '🌅'; Label = 'Boot previews'; Description = 'Sleep, waking, ready or PNG captures'; Value = 'boot' }
            '3' = @{ Emoji = '🔧'; Label = 'Firmware and device'; Description = 'Configure, build, flash, serial and size'; Value = 'firmware' }
            '4' = @{ Emoji = '🧪'; Label = 'Checks'; Description = 'Run one explicitly selected check'; Value = 'check' }
            '5' = @{ Emoji = '🎨'; Label = 'Asset generators'; Description = 'Regenerate canonical source assets'; Value = 'generate' }
            '6' = @{ Emoji = '🧹'; Label = 'Clean artifacts'; Description = 'Remove transient output; keep build/config/Python caches'; Value = 'clean-artifacts' }
            '7' = @{ Emoji = '⚙️'; Label = 'Session settings'; Description = 'Port, rotation, Python and board/profile'; Value = 'settings' }
            '8' = @{ Emoji = '🌙'; Label = 'Luna reference simulator'; Description = 'Existing reference UI'; Value = 'simulator-luna' }
            '9' = @{ Emoji = '📖'; Label = 'Command help'; Description = 'Categorized CLI guide, examples and tool descriptions'; Value = 'help' }
            'm' = @{ Emoji = '🧰'; Label = 'Maintenance'; Description = 'Package the Luna EXE or preview source banners'; Value = 'maintenance' }
        }
        $menuStage = 'selecting an operation'
        try {
            $selected = Select-DeviceMenuItem 'MAIN MENU' $main -IsMainMenu
            if (-not $selected) {
                return
            }
            if ($selected -eq 'settings') {
                $menuStage = 'editing session settings'
                Edit-DeviceMenuSettings $settings
                continue
            }
            $menuStage = "preparing $selected"
            $extra = @{}
            if ($selected -eq 'maintenance') {
                $maintenance = [ordered]@{
                    '1' = @{ Emoji = '📦'; Label = 'Package Luna simulator'; Description = 'Prepare Python dependencies and build the reference EXE'; Value = 'package-simulator' }
                    '2' = @{ Emoji = '🏷️'; Label = 'Preview source banners'; Description = 'Read-only banner maintenance preview'; Value = 'banner' }
                }
                $selected = Select-DeviceMenuItem 'MAINTENANCE' $maintenance
                if (-not $selected) { continue }
            }
            if ($selected -eq 'boot') {
                $boot = [ordered]@{
                    '1' = @{ Emoji = '💤'; Label = 'Sleeping'; Description = 'Start of boot'; Value = 'boot-sleep' }
                    '2' = @{ Emoji = '🌤️'; Label = 'Waking'; Description = 'Intermediate boot stage'; Value = 'boot-waking' }
                    '3' = @{ Emoji = '✅'; Label = 'Ready'; Description = 'Boot complete'; Value = 'boot-ready' }
                    '4' = @{ Emoji = '📸'; Label = 'Capture all three'; Description = 'artifacts/simulator/*.png'; Value = 'boot-screens' }
                }
                $screenChoice = Select-DeviceMenuItem 'BOOT PREVIEWS' $boot
                if (-not $screenChoice) { continue }
                if ($screenChoice -eq 'boot-screens') { $selected = 'boot-screens' }
                else { $selected = 'simulator'; $extra.Screen = $screenChoice }
            }
            if ($selected -eq 'firmware') {
                $firmware = [ordered]@{}
                $index = 0
                $presentation = Get-DeviceFirmwarePresentation
                foreach ($name in (Get-FirmwareActions)) {
                    $index++
                    $firmware[[string]$index] = @{
                        Emoji = $presentation[$name].Emoji
                        Label = $name
                        Description = $presentation[$name].Description
                        Value = $name
                    }
                }
                $selected = Select-DeviceMenuItem 'FIRMWARE AND DEVICE' $firmware
                if (-not $selected) { continue }
            }
            if ($selected -in @('check','generate')) {
                $group = if ($selected -eq 'check') { 'checks' } else { 'generators' }
                $catalog = [ordered]@{}
                $index = 0
                foreach ($tool in (Get-DeviceToolCatalog $group)) {
                    $index++
                    $catalog[[string]$index] = @{
                        Emoji = $tool.Emoji
                        Label = $tool.Name
                        Description = $tool.Description
                        Value = $tool.Name
                    }
                }
                $toolName = Select-DeviceMenuItem ($group.ToUpperInvariant()) $catalog
                if (-not $toolName) { continue }
                $key = if ($selected -eq 'check') { 'Check' } else { 'Generator' }
                $extra[$key] = $toolName
                # A JSON token array supports spaces and switches without Invoke-Expression.
                $rawArguments = Read-Host '  Extra arguments as JSON array, or Enter for defaults'
                if ($rawArguments) {
                    $tokens = ConvertFrom-Json -InputObject $rawArguments -NoEnumerate
                    if ($tokens -isnot [array] -or @($tokens | Where-Object { $_ -isnot [string] }).Count) {
                        throw 'Expected an array of strings, for example ["--help"].'
                    }
                    $extra.ToolArguments = [string[]]$tokens
                }
            }
            $parameters = @{ Action = $selected }
            foreach ($key in $settings.Keys) {
                if ($key -eq 'PythonExecutable' -and $selected -in @('check','generate') -and
                    (Find-DeviceTool $group $toolName).Extension -ne '.py') { continue }
                if ($key -in $actions[$selected]) { $parameters[$key] = $settings[$key] }
            }
            foreach ($key in $extra.Keys) { $parameters[$key] = $extra[$key] }
            $modeDescription = if ($parameters.ContainsKey('BuildMode')) { " -BuildMode $($parameters.BuildMode)" } else { '' }
            Write-Host "  Running: device.ps1 $selected$modeDescription" -ForegroundColor Cyan
            $menuStage = "starting $selected"
            & (Join-Path $ProjectRoot 'scripts/device.ps1') @parameters
            if ($LASTEXITCODE -ne 0) {
                # The public command already presented and logged its failure.
                # Keep the menu open without printing the same error twice.
                Wait-ForEnter
                continue
            }
        } catch {
            Write-DeviceFailure -FailureRecord $_ -Operation 'menu' -Stage $menuStage `
                -LogUnavailableReason 'Menu selection failed before an operation log was opened.'
        }
        Wait-ForEnter
    }
}
