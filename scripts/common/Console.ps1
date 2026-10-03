# Presentation metadata is shared by the menu and help; command routing stays in Commands.ps1.

# No parameters: describe each supported firmware action by its public command identity.
function Get-DeviceFirmwarePresentation {
    return [ordered]@{
        build = @{ Emoji = '🔨'; Description = 'Compile the selected firmware; use -Jobs 8 to limit parallel work.' }
        'set-target' = @{ Emoji = '🎯'; Description = 'Configure the selected ESP32-S3 profile from SDKCONFIG defaults; clear its existing build.' }
        reconfigure = @{ Emoji = '⚙️'; Description = 'Regenerate CMake for an already configured profile; enforce defaults and lock gates.' }
        'update-lock' = @{ Emoji = '🔒'; Description = 'Clear the selected build, resolve dependencies and publish its profile lock; review the changed lock file.' }
        size = @{ Emoji = '📊'; Description = 'Report firmware memory usage and save artifacts/reports/size.<format>.' }
        clean = @{ Emoji = '🧹'; Description = 'Remove compiled output from the selected firmware build; retain its configuration.' }
        fullclean = @{ Emoji = '🗑️'; Description = 'Fully clean the selected firmware build; configure it again with set-target before building.' }
        discovery = @{ Emoji = '🔎'; Description = 'List Windows serial devices and USB identities without loading ESP-IDF.' }
        ports = @{ Emoji = '🔌'; Description = 'List serial ports through the configured ESP-IDF toolchain.' }
        'port-status' = @{ Emoji = '🩺'; Description = 'Probe the selected port and report availability and candidate owner processes.' }
        monitor = @{ Emoji = '📟'; Description = 'Open the live ESP-IDF serial monitor; exit with Ctrl+].' }
        capture = @{ Emoji = '📝'; Description = 'Save serial output for a bounded duration; reset first only with -ResetBeforeCapture.' }
        flash = @{ Emoji = '⚡'; Description = 'Build and write the selected firmware to the device, then reset it.' }
        reset = @{ Emoji = '🔄'; Description = 'Reset the device through serial control lines without writing firmware.' }
        combo = @{ Emoji = '🚀'; Description = 'Build, flash and immediately open the live monitor in this terminal.' }
        recover = @{ Emoji = '🛟'; Description = 'Build and flash using the bounded USB reconnect and port-owner recovery flow.' }
        erase = @{ Emoji = '💥'; Description = 'Erase the entire device flash, including firmware and persisted device settings.' }
    }
}

# Group selects the typed checks or generators catalog namespace.
# No tool is executed: missing editorial metadata remains explicitly visible.
# Throws if the repository tool directory cannot be read.
function Get-DeviceToolCatalog([ValidateSet('checks','generators')][string]$Group) {
    $descriptions = @{
        checks = @{
            'cody-poc' = @{ Category = 'Cody and simulators'; Emoji = '🤖'; Description = 'Cody PoC contracts and optional preview artifacts.' }
            'cody-native' = @{ Category = 'Cody and simulators'; Emoji = '🤖'; Description = 'Compile the portable C++ renderer and compare geometry with Python.' }
            'cody-response' = @{ Category = 'Cody and simulators'; Emoji = '🤖'; Description = 'Deterministic touch, motion and expression response contracts.' }
            'cody-runtime' = @{ Category = 'Cody and simulators'; Emoji = '🤖'; Description = 'Native response, demo, statistics and original boot-cue checks.' }
            simulator = @{ Category = 'Cody and simulators'; Emoji = '🖥️'; Description = 'Windows Luna model and loopback fake-device contracts.' }
            'battery-ui' = @{ Category = 'UI and connectivity'; Emoji = '🔋'; Description = 'Battery-screen layout and navigation contracts.' }
            'fullscreen-layout' = @{ Category = 'UI and connectivity'; Emoji = '📐'; Description = 'Adaptive chrome and full-screen viewport geometry.' }
            'lazy-runtime-contract' = @{ Category = 'UI and connectivity'; Emoji = '📄'; Description = 'Lazy GUI document lifecycle and allocation contracts.' }
            'fft-ui-cache' = @{ Category = 'UI and connectivity'; Emoji = '🎵'; Description = 'FFT binding-cache behavior and allocation contracts.' }
            pairing = @{ Category = 'UI and connectivity'; Emoji = '📡'; Description = 'BLE provisioning and System Core integration contracts.' }
            'http-api-contract' = @{ Category = 'UI and connectivity'; Emoji = '🌐'; Description = 'HTTP API settings and lifecycle contracts.' }
            'hardware-status' = @{ Category = 'Snapshots, hardware and power'; Emoji = '🩺'; Description = 'Hardware health and failure-classification contracts.' }
            'snapshot-integrity' = @{ Category = 'Snapshots, hardware and power'; Emoji = '📸'; Description = 'Canonical DeviceSnapshot data integrity and validity.' }
            'board-services' = @{ Category = 'Snapshots, hardware and power'; Emoji = '🔌'; Description = 'Host regression checks for shell services and build cleanup.' }
            'power-policy' = @{ Category = 'Snapshots, hardware and power'; Emoji = '🔋'; Description = 'Deterministic power-policy models and static source contracts.' }
            'brookesia-scheduler-power' = @{ Category = 'Snapshots, hardware and power'; Emoji = '⏱️'; Description = 'Event-driven Brookesia scheduler and power contracts.' }
            'rtc-contract' = @{ Category = 'Snapshots, hardware and power'; Emoji = '🕒'; Description = 'RTC reset state and recovery from real clock sources.' }
            'storage-logger' = @{ Category = 'Snapshots, hardware and power'; Emoji = '💾'; Description = 'Removable-storage ownership and logger scheduling.' }
            'radio-memory' = @{ Category = 'Snapshots, hardware and power'; Emoji = '📡'; Description = 'Wi-Fi/BLE startup and internal DMA memory contracts.' }
            liveness = @{ Category = 'Snapshots, hardware and power'; Emoji = '💓'; Description = 'Firmware liveness and progress contracts.' }
            'tof-build-gate' = @{ Category = 'Snapshots, hardware and power'; Emoji = '📏'; Description = 'Default-off TOF build gate and generated UI variant.' }
            'tof-distance' = @{ Category = 'Snapshots, hardware and power'; Emoji = '📏'; Description = 'Dual TOF400F precision-distance contracts.' }
            'device-console' = @{ Category = 'Build, console and repository'; Emoji = '🧭'; Description = 'Isolated dispatcher, option-rejection and artifact-ownership checks.' }
            'script-contracts' = @{ Category = 'Build, console and repository'; Emoji = '📜'; Description = 'PowerShell parsing and maintenance-script dispatch contracts.' }
            'build-modes' = @{ Category = 'Build, console and repository'; Emoji = '🐞'; Description = 'Debug/Release selection and build-mode safety gates.' }
            'build-profiles' = @{ Category = 'Build, console and repository'; Emoji = '🎯'; Description = 'Firmware profiles and dependency-closure contracts.' }
            'incremental-build' = @{ Category = 'Build, console and repository'; Emoji = '🔨'; Description = 'Timestamp-driven package and LittleFS generation.' }
            'check-resource-budgets' = @{ Category = 'Build, console and repository'; Emoji = '📊'; Description = 'Check memory and build-graph budgets against existing build artifacts; needs --build-dir.' }
            'test-resource-budgets' = @{ Category = 'Build, console and repository'; Emoji = '📊'; Description = 'Host tests for the resource-budget checker and CI contract.' }
            'changed-secrets' = @{ Category = 'Build, console and repository'; Emoji = '🔐'; Description = 'Inspect changed repository files for common secret literals.' }
            'public-branding' = @{ Category = 'Build, console and repository'; Emoji = '🏷️'; Description = 'Public-release text and branding contracts.' }
        }
        generators = @{
            'cody-rig' = @{ Category = 'Cody assets'; Emoji = '🤖'; Description = 'Compile a selected skin into immutable named firmware rig tables.' }
            'cody-poc' = @{ Category = 'Cody assets'; Emoji = '🔊'; Description = 'Generate PoC settings and the two original synthesized boot cues.' }
            'cody-jelly' = @{ Category = 'Cody assets'; Emoji = '☁️'; Description = 'Package the pearl sprite and simulator bindings as flash-owned data.' }
            'cody-scenes' = @{ Category = 'Cody assets'; Emoji = '🎬'; Description = 'Bake cloud/title scenes and glyphs; requires --font-path.' }
            'package-cody-boot' = @{ Category = 'Cody assets'; Emoji = '🌅'; Description = 'Convert existing boot images into indexed RGB565 firmware resources.' }
            'device-assets' = @{ Category = 'Luna assets'; Emoji = '📦'; Description = 'Generate the canonical boot/SFX package with rollback on failure.' }
            'boot-assets' = @{ Category = 'Luna assets'; Emoji = '🌅'; Description = 'Generate deterministic boot artwork for the 466 px circular display.' }
            sfx = @{ Category = 'Luna assets'; Emoji = '🔊'; Description = 'Generate sound effects with ffmpeg, format checks and rollback.' }
            'mascot-asset' = @{ Category = 'Luna assets'; Emoji = '🙂'; Description = 'Generate the idle head layer from its preserved source PNG.' }
            'LUNASHELL-gui-documents' = @{ Category = 'GUI packaging'; Emoji = '📄'; Description = 'Split canonical GUI content into bootstrap and lazy screen documents.' }
            'minify-json-tree' = @{ Category = 'GUI packaging'; Emoji = '🗜️'; Description = 'Minify staged JSON without rewriting tokens; requires a directory argument.' }
        }
    }

    foreach ($tool in (Get-DeviceTools $Group)) {
        $metadata = $descriptions[$Group][$tool.Name]
        if ($null -eq $metadata) {
            $metadata = @{
                Category = 'Uncatalogued tools'
                Emoji = '📄'
                Description = "Description not registered; see scripts/$Group/$([IO.Path]::GetFileName($tool.Path))."
            }
        }
        [pscustomobject]@{
            Name = $tool.Name
            Path = $tool.Path
            Extension = $tool.Extension
            Category = $metadata.Category
            Emoji = $metadata.Emoji
            Description = $metadata.Description
        }
    }
}

# Text contains one paragraph to wrap without dropping any command or description.
# Width is the available terminal width, including indentation.
# Indent is the leading whitespace for every wrapped line.
# Color selects the paragraph's console color.
# ContinueLine skips the first indent when the command column has already been printed.
# Command keeps wrapped PowerShell examples copyable with explicit line-continuation marks.
function Write-DeviceHelpText([string]$Text, [int]$Width, [string]$Indent = '      ', [ConsoleColor]$Color = 'Gray', [switch]$ContinueLine, [switch]$Command) {
    $continuation = if ($Command) { ' `' } else { '' }
    $availableWidth = [Math]::Max(20, $Width - $Indent.Length - $continuation.Length - 2)
    $line = ''
    $currentIndent = if ($ContinueLine) { '' } else { $Indent }
    foreach ($word in ($Text -split '\s+')) {
        if ($line.Length -gt 0 -and ($line.Length + 1 + $word.Length) -gt $availableWidth) {
            Write-Host "$currentIndent$line$continuation" -ForegroundColor $Color
            $currentIndent = $Indent
            $line = ''
        }
        if ($line.Length -gt 0) {
            $line += ' '
        }
        $line += $word
    }
    if ($line.Length -gt 0) {
        Write-Host "$currentIndent$line" -ForegroundColor $Color
    }
}

# Title names a command category or a subsection within it.
# Icon is the category emoji, kept separate from aligned command columns.
# Width bounds the divider to the current terminal.
# Color selects the category accent.
# Subsection uses a lighter heading instead of a full category divider.
function Write-DeviceHelpSection([string]$Title, [string]$Icon, [int]$Width, [ConsoleColor]$Color = 'Cyan', [switch]$Subsection) {
    Write-Host ''
    if ($Subsection) {
        Write-Host "    $Icon  $Title" -ForegroundColor $Color
        return
    }
    Write-Host ('  ' + ('─' * ($Width - 4))) -ForegroundColor DarkGray
    Write-Host "  $Icon  $Title" -ForegroundColor $Color
    Write-Host ('  ' + ('─' * ($Width - 4))) -ForegroundColor DarkGray
}

# Syntax is the exact public command, tool identifier or option being documented.
# Description explains the operation in plain English.
# Width controls whether descriptions fit beside the syntax or below it.
# Detail adds an indented explanation for commands with less obvious behavior.
function Write-DeviceHelpEntry([string]$Syntax, [string]$Description, [int]$Width, [string]$Detail) {
    # The public examples are directly runnable from the checkout in PowerShell.
    if ($Syntax.StartsWith('device.ps1 ')) {
        $Syntax = '.\scripts\' + $Syntax
    }
    # Wide terminals get aligned columns. Narrow terminals keep complete syntax
    # on its own lines instead of truncating commands or squeezing descriptions.
    $syntaxWidth = 54
    if ($Width -ge 112 -and $Syntax.Length -le $syntaxWidth) {
        Write-Host ('    ' + $Syntax.PadRight($syntaxWidth)) -ForegroundColor White -NoNewline
        Write-Host '  ' -NoNewline
        $descriptionIndent = ' ' * ($syntaxWidth + 6)
        Write-DeviceHelpText -Text $Description -Width $Width -Indent $descriptionIndent -Color Gray -ContinueLine
    } else {
        Write-DeviceHelpText -Text $Syntax -Width $Width -Indent '    ' -Color White -Command:($Syntax.StartsWith('.\scripts\device.ps1'))
        Write-DeviceHelpText -Text $Description -Width $Width
    }
    if ($Detail) {
        Write-DeviceHelpText -Text $Detail -Width $Width -Color DarkCyan
    }
}
