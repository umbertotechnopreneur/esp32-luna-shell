# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested VibeWare branding.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

# Presentation metadata is shared by the menu and help; command routing stays in Commands.ps1.

# No parameters: describe firmware/device actions by their public command identity.
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
        monitor = @{ Emoji = '📟'; Description = 'Attach the live ESP-IDF monitor without a startup reset; exit with Ctrl+].' }
        capture = @{ Emoji = '📝'; Description = 'Save serial output for a bounded duration; reset first only with -ResetBeforeCapture.' }
        'sd-log' = @{ Emoji = '📥'; Description = 'Download a stored SD log over USB with checked blocks; close the monitor first.' }
        'endurance-read' = @{ Emoji = '📥'; Description = 'Read canonical current/previous endurance records over USB, including Release builds; no reset.' }
        flash = @{ Emoji = '⚡'; Description = 'Build and write the selected firmware to the device, then reset it.' }
        reset = @{ Emoji = '🔄'; Description = 'Reset the device through serial control lines without writing firmware.' }
        combo = @{ Emoji = '🚀'; Description = 'Build, flash and attach the live monitor without an additional startup reset.' }
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
            'cody-alpha' = @{ Category = 'Cody and simulators'; Emoji = '🤖'; Description = 'Cody Alpha contracts and optional preview artifacts.' }
            'cody-native' = @{ Category = 'Cody and simulators'; Emoji = '🤖'; Description = 'Compile the portable C++ renderer and compare geometry with Python.' }
            'cody-personality' = @{ Category = 'Cody and simulators'; Emoji = '✨'; Description = 'Native symbol timing, bounded follow, layout, dirty raster and simulator parity contracts.' }
            'cody-preview-raster' = @{ Category = 'Cody and simulators'; Emoji = '🎨'; Description = 'Pure host sparse-coverage RGB565 regression checks; no GUI, device or benchmark.' }
            'cody-ccp-simulator' = @{ Category = 'Cody and simulators'; Emoji = '🔗'; Description = 'Isolated CCP1 pairing, durable recovery and ephemeral loopback UDP; no physical device or live hooks.' }
            'cody-raster-benchmark' = @{ Category = 'Cody and simulators'; Emoji = '🤖'; Description = 'Check finite benchmark windows and native profiled/fused pixel parity.' }
            'display-dma' = @{ Category = 'Snapshots, hardware and power'; Emoji = '🖥️'; Description = 'Execute native BSP slot, DMA completion, timeout and quarantine contracts on the host.' }
            'cody-response' = @{ Category = 'Cody and simulators'; Emoji = '🤖'; Description = 'Deterministic touch, motion and expression response contracts.' }
            'cody-runtime' = @{ Category = 'Cody and simulators'; Emoji = '🤖'; Description = 'Native response, demo, statistics and original boot-cue checks.' }
            'cody-voice-ui' = @{ Category = 'Cody and simulators'; Emoji = '🤖'; Description = 'Production recording gestures, native ribbon raster and synthetic preview.' }
            'cody-voice-input' = @{ Category = 'Cody and simulators'; Emoji = '🎵'; Description = 'Production voice owner and authenticated receiver contracts on the host.' }
            'cody-audio-setup' = @{ Category = 'Cody and simulators'; Emoji = '🎵'; Description = 'Isolated native audio installation, persistence and failed-probe boundaries.' }
            'cody-public-audio-queue' = @{ Category = 'Cody and simulators'; Emoji = '🎵'; Description = 'Explicit live experimental queue probe or single-use WAV pilot; requires native CLI and genuine chat ID.' }
            'voice-afe-lifecycle' = @{ Category = 'Snapshots, hardware and power'; Emoji = '🎵'; Description = 'Production AFE notification, startup and rollback helpers with host primitives.' }
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
            'test-resource-budgets' = @{ Category = 'Build, console and repository'; Emoji = '📊'; Description = 'Host tests for the current firmware resource-budget checker.' }
            'changed-secrets' = @{ Category = 'Build, console and repository'; Emoji = '🔐'; Description = 'Inspect changed repository files for common secret literals.' }
            'public-branding' = @{ Category = 'Build, console and repository'; Emoji = '🏷️'; Description = 'Public-release text and branding contracts.' }
        }
        generators = @{
            'cody-audio-queue-fixture' = @{ Category = 'Cody and simulators'; Emoji = '🎵'; Description = 'Windows SAPI synthetic speech WAV for the explicit public audio queue pilot; no speaker playback.' }
            'cody-rig' = @{ Category = 'Cody assets'; Emoji = '🤖'; Description = 'Compile a selected skin into immutable named firmware rig tables.' }
            'cody-runtime-assets' = @{ Category = 'Cody assets'; Emoji = '🔊'; Description = 'Generate runtime settings and the two original synthesized boot cues.' }
            'cody-jelly' = @{ Category = 'Cody assets'; Emoji = '☁️'; Description = 'Package the pearl sprite and simulator bindings as flash-owned data.' }
            'cody-personality-icons' = @{ Category = 'Cody assets'; Emoji = '✨'; Description = 'Isolate 14 manga symbols into fixed-palette indexed and separate coverage flash sprites.' }
            'tts-assets' = @{ Category = 'Luna assets'; Emoji = '🔊'; Description = 'Package optional pinned speech data: SD four languages or English-only flash.' }
            'cody-personality-audio' = @{ Category = 'Cody assets'; Emoji = '🎵'; Description = 'Synthesize 14 original short symbol cues, WAV previews and immutable PCM flash assets.' }
            'cody-scenes' = @{ Category = 'Cody assets'; Emoji = '🎬'; Description = 'Bake cloud/title scenes and glyphs; requires --font-path.' }
            'cody-home-dashboard-fonts' = @{ Category = 'Cody assets'; Emoji = '🔤'; Description = 'Bake independent Home sans fonts from the vendored OFL source.' }
            'cody-model-cost-audio' = @{ Category = 'Cody assets'; Emoji = '🔊'; Description = 'Prepare the pinned CC0 model-price plink and five-note preview.' }
            'cody-question-flow-mockups' = @{ Category = 'Cody documents'; Emoji = '🎨'; Description = 'Render eight question-flow design screens and an offline mouse/touch prototype.' }
            'package-cody-boot' = @{ Category = 'Cody assets'; Emoji = '🌅'; Description = 'Convert existing boot images into indexed RGB565 firmware resources.' }
            'vibeware-boot' = @{ Category = 'Cody assets'; Emoji = '💾'; Description = 'Package the approved transparent floppy GIF for native and LVGL boot.' }
            'vibeware-boot-revision' = @{ Category = 'Cody documents'; Emoji = '📄'; Description = 'Append and visually review the VibeWare boot identity revision in both existing PDF atlases.' }
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

# Text is one already wrapped line, with optional single- or double-dash parameters.
# Color selects the base text color while parameters use WhiteSmoke.
# Heading renders the complete command or option in bold WhiteSmoke.
function Write-DeviceHelpLine([string]$Text, [ConsoleColor]$Color, [switch]$Heading) {
    if ($Heading) {
        if ($Host.UI.SupportsVirtualTerminal) {
            $coloredHeading = '{0}[1;38;2;245;245;245m{1}{0}[0m' -f [char]27, $Text
            Write-Host $coloredHeading
        } else {
            Write-Host $Text -ForegroundColor White
        }
        return
    }
    $parameters = [regex]::Matches($Text, '(?<![\w-])--?[A-Za-z?][A-Za-z0-9-]*')
    $position = 0
    foreach ($parameter in $parameters) {
        if ($parameter.Index -gt $position) {
            Write-Host $Text.Substring($position, $parameter.Index - $position) -ForegroundColor $Color -NoNewline
        }
        if ($Host.UI.SupportsVirtualTerminal) {
            # ConsoleColor is theme-dependent; true color preserves the requested #F5F5F5.
            $coloredParameter = '{0}[1;38;2;245;245;245m{1}{0}[0m' -f [char]27, $parameter.Value
            Write-Host $coloredParameter -NoNewline
        } else {
            Write-Host $parameter.Value -ForegroundColor White -NoNewline
        }
        $position = $parameter.Index + $parameter.Length
    }
    if ($position -lt $Text.Length) {
        Write-Host $Text.Substring($position) -ForegroundColor $Color -NoNewline
    }
    Write-Host ''
}

# Text contains one paragraph to wrap without dropping any command or description.
# Width is the available terminal width, including indentation.
# Indent is the leading whitespace for every wrapped line.
# Color selects the paragraph's console color.
# Command keeps wrapped PowerShell examples copyable with explicit line-continuation marks.
# Heading gives wrapped command lines the same bold WhiteSmoke style.
function Write-DeviceHelpText([string]$Text, [int]$Width, [string]$Indent = '      ', [ConsoleColor]$Color = 'Gray', [switch]$Command, [switch]$Heading) {
    $continuation = if ($Command) { ' `' } else { '' }
    $availableWidth = [Math]::Max(20, $Width - $Indent.Length - $continuation.Length - 2)
    $line = ''
    foreach ($word in ($Text -split '\s+')) {
        if ($line.Length -gt 0 -and ($line.Length + 1 + $word.Length) -gt $availableWidth) {
            Write-DeviceHelpLine -Text "$Indent$line$continuation" -Color $Color -Heading:$Heading
            $line = ''
        }
        if ($line.Length -gt 0) {
            $line += ' '
        }
        $line += $word
    }
    if ($line.Length -gt 0) {
        Write-DeviceHelpLine -Text "$Indent$line" -Color $Color -Heading:$Heading
    }
}

# Length is the number of terminal cells available after the section title.
# Color identifies the category accent used at the first gradient point.
# Throws when no gradient palette is defined for the requested category color.
function Write-DeviceHelpGradient([int]$Length, [ConsoleColor]$Color) {
    $start = switch ($Color) {
        Cyan { [pscustomobject]@{ Red = 64; Green = 190; Blue = 200 } }
        Magenta { [pscustomobject]@{ Red = 184; Green = 90; Blue = 196 } }
        Yellow { [pscustomobject]@{ Red = 208; Green = 176; Blue = 70 } }
        Green { [pscustomobject]@{ Red = 76; Green = 174; Blue = 112 } }
        default { throw "Device help gradient: no palette for category color '$Color'." }
    }
    if (-not $Host.UI.SupportsVirtualTerminal) {
        Write-Host ('─' * $Length) -ForegroundColor DarkGray
        return
    }

    # Three named points keep the category tint at the start, soften it halfway,
    # then fade into a dark neutral tone suited to the development terminal.
    $end = [pscustomobject]@{ Red = 32; Green = 32; Blue = 32 }
    $middle = [pscustomobject]@{
        Red = [int][Math]::Round($end.Red + ($start.Red - $end.Red) * 0.42)
        Green = [int][Math]::Round($end.Green + ($start.Green - $end.Green) * 0.42)
        Blue = [int][Math]::Round($end.Blue + ($start.Blue - $end.Blue) * 0.42)
    }
    $line = [Text.StringBuilder]::new()
    for ($column = 0; $column -lt $Length; $column++) {
        $progress = $column / [Math]::Max(1, $Length - 1)
        if ($progress -le 0.5) {
            $from = $start
            $to = $middle
            $blend = $progress * 2
        } else {
            $from = $middle
            $to = $end
            $blend = ($progress - 0.5) * 2
        }
        $red = [int][Math]::Round($from.Red + ($to.Red - $from.Red) * $blend)
        $green = [int][Math]::Round($from.Green + ($to.Green - $from.Green) * $blend)
        $blue = [int][Math]::Round($from.Blue + ($to.Blue - $from.Blue) * $blend)
        [void]$line.Append(('{0}[22;38;2;{1};{2};{3}m─' -f [char]27, $red, $green, $blue))
    }
    [void]$line.Append(('{0}[0m' -f [char]27))
    Write-Host $line.ToString()
}

# Title names a command category or a subsection within it.
# Icon is the category emoji, rendered in two terminal cells.
# Width bounds the divider to the current terminal.
# Color selects the category accent.
# Subsection indents nested headings to preserve the category hierarchy.
# Throws when the category accent has no defined gradient palette.
function Write-DeviceHelpSection([string]$Title, [string]$Icon, [int]$Width, [ConsoleColor]$Color = 'Cyan', [switch]$Subsection) {
    Write-Host ''
    $indent = if ($Subsection) { '    ' } else { '  ' }
    $heading = "$Icon  $Title"
    $iconWidth = if ($Icon) { 2 } else { 0 }
    $headingWidth = $indent.Length + $iconWidth + 2 + $Title.Length
    $ruleLength = $Width - $headingWidth - 3
    if ($ruleLength -lt 3) {
        # Keep the complete title when a very narrow terminal leaves no rule room.
        Write-DeviceHelpText -Text $heading -Width $Width -Indent $indent -Color $Color
        return
    }
    Write-Host "$indent$heading " -ForegroundColor $Color -NoNewline
    Write-DeviceHelpGradient -Length $ruleLength -Color $Color
}

# Syntax is the exact public command, tool identifier or option being documented.
# Description explains the operation in plain English.
# Width wraps command headings and their descriptions to the available terminal space.
# Detail extends the description for commands with less obvious behavior.
function Write-DeviceHelpEntry([string]$Syntax, [string]$Description, [int]$Width, [string]$Detail) {
    # Each entry owns one blank line. Descriptions use the full available width
    # rather than reserving half of the terminal for a mostly empty column.
    Write-Host ''
    Write-DeviceHelpText -Text $Syntax -Width $Width -Indent '    ' -Color White -Command:($Syntax.StartsWith('Device.ps1')) -Heading
    $extendedDescription = $Description
    if ($Detail) {
        $extendedDescription += ' ' + $Detail
    }
    Write-DeviceHelpText -Text $extendedDescription -Width $Width
}
