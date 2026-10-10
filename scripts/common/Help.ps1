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

# Read-only command discovery. Only the selected operation may load tools or touch hardware.

# No parameters: print categorized commands and current catalogs without running any operation.
# Throws if a repository tool catalog cannot be read.
function Show-DeviceHelp {
    Import-Module (Join-Path $PSScriptRoot 'MenuManager/MenuManager.psd1') -ErrorAction Stop
    $width = Resolve-MenuConsoleWidth -Minimum 48 -Maximum 120 -Fallback 100
    $firmware = Get-DeviceFirmwarePresentation

    Write-Host ''
    Write-Host ('  ' + ('═' * ($width - 4))) -ForegroundColor DarkCyan
    Write-Host '  🌙  CODY COMPANION / DEVICE CONSOLE' -ForegroundColor Cyan
    Write-DeviceHelpText -Text 'Simulators · Firmware · Device · Checks · Assets' -Width $width
    Write-Host ('  ' + ('═' * ($width - 4))) -ForegroundColor DarkCyan
    Write-DeviceHelpText -Text 'Usage: Device.ps1 <command> [options]' -Width $width -Indent '  '
    Write-DeviceHelpText -Text 'Examples use the script name only. Run it from the scripts folder; in PowerShell use .\Device.ps1.' -Width $width -Indent '  '

    Write-DeviceHelpSection -Title 'START HERE' -Icon '🧭' -Width $width
    Write-DeviceHelpEntry -Syntax 'Device.ps1' -Description 'Open the interactive menu; select an operation or edit session settings.' -Width $width
    Write-DeviceHelpEntry -Syntax 'Device.ps1 help' -Description 'Show this guide, with examples and command descriptions.' -Width $width
    Write-DeviceHelpEntry -Syntax 'Device.ps1 list' -Description 'Show the same guide, including the current check/generator catalogs.' -Width $width

    Write-DeviceHelpSection -Title 'SIMULATORS AND PREVIEWS' -Icon '🖥️' -Width $width -Color Magenta
    Write-DeviceHelpSection -Title 'Interactive Cody and boot screens' -Icon '🤖' -Width $width -Color Magenta -Subsection
    Write-DeviceHelpEntry -Syntax 'Device.ps1 simulator' -Description 'Open Cody with rig, face, contour and debug controls.' -Width $width
    Write-DeviceHelpEntry -Syntax 'Device.ps1 simulator -Ccp' -Description 'Open the SIMULATED CCP1 endpoint and visible pairing panel on loopback.' -Detail 'Uses a separate persistent identity. In Codex explicitly request SIMULATED discovery; the physical device remains separate.' -Width $width
    Write-DeviceHelpEntry -Syntax 'Device.ps1 simulator -Ccp -ActivityPreview -AtMs 6500' -Description 'Open the shared native activity renderer with visibly synthetic project, branch and long-description metadata.' -Detail 'No pairing is required. Save one annotated native frame after AtMs (default 1000) under artifacts/simulator; the visible panel can replay phases and save more frames.' -Width $width
    Write-DeviceHelpEntry -Syntax 'Device.ps1 simulator -Skin assets/characters/example.yaml' -Description 'Preview an explicitly selected character skin YAML.' -Width $width
    Write-DeviceHelpEntry -Syntax 'Device.ps1 simulator -Screen boot-ready -Rotation 180' -Description 'Preview a boot stage in the selected display orientation.' -Width $width
    Write-DeviceHelpEntry -Syntax 'Device.ps1 simulator -Screen greeting' -Description 'Open the greeting scene with visibly synthetic host stimuli.' -Width $width
    Write-DeviceHelpEntry -Syntax 'Device.ps1 boot-screens' -Description 'Save sleeping, waking and ready PNG previews in artifacts/simulator/.' -Width $width
    Write-DeviceHelpEntry -Syntax 'Device.ps1 simulator -OutputPath artifacts/simulator/rig.png' -Description 'Save a still preview instead of opening the interactive simulator.' -Width $width

    Write-DeviceHelpSection -Title 'Cloud material studies' -Icon '☁️' -Width $width -Color Magenta -Subsection
    Write-DeviceHelpEntry -Syntax 'Device.ps1 simulator -CloudMaterialStudy -OutputPath artifacts/simulator/cloud-study.png' -Description 'Render the cloud-volume material study to a still image.' -Width $width
    Write-DeviceHelpEntry -Syntax 'Device.ps1 simulator -CloudPngMeshStudy -OutputPath artifacts/simulator/cloud-mesh.gif' -Description 'Capture the PNG mesh study as a synthetic motion loop; use .png for a still.' -Detail 'Add -MeshOverlay to show the triangle wireframe. This study requires -OutputPath and the rig screen.' -Width $width
    Write-DeviceHelpEntry -Syntax 'Device.ps1 simulator -CloudJellyStudy' -Description 'Open the interactive pearlescent scanline study.' -Detail 'Add -OutputPath artifacts/simulator/cloud-jelly.gif to export a loop, or use .png for a still. Select only one cloud study at a time.' -Width $width

    Write-DeviceHelpSection -Title 'Luna reference tools' -Icon '🌙' -Width $width -Color Magenta -Subsection
    Write-DeviceHelpEntry -Syntax 'Device.ps1 simulator-luna' -Description 'Open the existing Luna reference UI simulator.' -Width $width
    Write-DeviceHelpEntry -Syntax 'Device.ps1 package-simulator' -Description 'Prepare local Python dependencies and package the Luna simulator EXE.' -Detail 'This packages Luna; the Cody simulator remains source-based.' -Width $width

    Write-DeviceHelpSection -Title 'FIRMWARE' -Icon '🔨' -Width $width -Color Cyan
    Write-DeviceHelpSection -Title 'Build and configuration' -Icon '🎯' -Width $width -Subsection
    foreach ($name in @('set-target','build','reconfigure','update-lock','size')) {
        $syntax = switch ($name) {
            build { 'Device.ps1 build -Jobs 8' }
            size { 'Device.ps1 size -SizeOutputFormat json2' }
            default { "Device.ps1 $name" }
        }
        Write-DeviceHelpEntry -Syntax $syntax -Description $firmware[$name].Description -Width $width
    }
    Write-DeviceHelpEntry -Syntax 'Device.ps1 build -BuildMode Release -Jobs 8' -Description 'Compile Release explicitly; ordinary CLI calls default to Debug.' -Detail 'Use -BuildMode Release again for Release set-target, flash and size. Debug/Release keep separate generated configurations and build directories; menu mode selection lasts for that session.' -Width $width

    Write-DeviceHelpSection -Title 'Cleanup' -Icon '🧹' -Width $width -Subsection
    foreach ($name in @('clean','fullclean')) {
        Write-DeviceHelpEntry -Syntax "Device.ps1 $name" -Description $firmware[$name].Description -Width $width
    }
    Write-DeviceHelpEntry -Syntax 'Device.ps1 clean-artifacts' -Description 'Remove transient logs, captures, reports and previews; retain firmware builds, configuration and Python caches.' -Detail 'The same transient cleanup runs before build, flash, combo and recover.' -Width $width

    Write-DeviceHelpSection -Title 'DEVICE AND SERIAL' -Icon '🔌' -Width $width -Color Yellow
    Write-DeviceHelpSection -Title 'Connection and serial output' -Icon '📟' -Width $width -Color Yellow -Subsection
    foreach ($name in @('discovery','ports','port-status','monitor','capture','sd-log')) {
        $syntax = switch ($name) {
            { $_ -in @('discovery','ports') } { "Device.ps1 $name" }
            monitor { 'Device.ps1 monitor' }
            capture { 'Device.ps1 capture -Port COM8 -DurationSeconds 30' }
            'sd-log' { 'Device.ps1 sd-log -Port COM10 -StoredLog current' }
            default { "Device.ps1 $name -Port COM8" }
        }
        Write-DeviceHelpEntry -Syntax $syntax -Description $firmware[$name].Description -Width $width
    }
    Write-DeviceHelpEntry -Syntax 'Device.ps1 monitor -Port COM10 -Baud 115200 -ResetBeforeMonitor' -Description 'Explicitly reset the device when the monitor starts so the new boot is visible.' -Detail 'Ordinary monitor attach uses --no-reset and opens the port only through ESP-IDF; it does not stop an existing monitor. Close the application using that port yourself before retrying a busy-port failure.' -Width $width
    Write-DeviceHelpEntry -Syntax 'Device.ps1 sd-log -Port COM10 -StoredLog previous' -Description 'Download the retained previous SD log over native USB into artifacts/serial/sd-log-previous.log.' -Detail 'Requires the updated firmware, an awake device and a mounted SD. Close the monitor first. No reset or format is requested; each block is checked before replacing the local output file.' -Width $width
    Write-DeviceHelpSection -Title 'Programming and recovery' -Icon '⚡' -Width $width -Color Yellow -Subsection
    foreach ($name in @('flash','reset','combo','recover','erase')) {
        Write-DeviceHelpEntry -Syntax "Device.ps1 $name -Port COM8" -Description $firmware[$name].Description -Width $width
    }

    Write-DeviceHelpSection -Title 'CHECKS' -Icon '🧪' -Width $width -Color Green
    Write-DeviceHelpEntry -Syntax 'Device.ps1 check -Check cody-alpha' -Description 'Run exactly the selected check. Default identifier: cody-alpha.' -Width $width
    Write-DeviceHelpEntry -Syntax "Device.ps1 check -Check cody-alpha -ToolArguments @('--previews')" -Description 'Pass literal tool-specific tokens from a PowerShell session.' -Width $width
    foreach ($category in (Get-DeviceToolCatalog 'checks' | Group-Object Category)) {
        Write-DeviceHelpSection -Title $category.Name -Icon '🧪' -Width $width -Color Green -Subsection
        foreach ($tool in $category.Group) {
            Write-DeviceHelpEntry -Syntax $tool.Name -Description $tool.Description -Width $width
        }
    }

    Write-DeviceHelpSection -Title 'ASSET GENERATORS' -Icon '🎨' -Width $width -Color Magenta
    Write-DeviceHelpEntry -Syntax 'Device.ps1 generate -Generator cody-rig' -Description 'Regenerate the selected canonical source assets. Default identifier: cody-rig.' -Width $width
    foreach ($category in (Get-DeviceToolCatalog 'generators' | Group-Object Category)) {
        Write-DeviceHelpSection -Title $category.Name -Icon '🎨' -Width $width -Color Magenta -Subsection
        foreach ($tool in $category.Group) {
            Write-DeviceHelpEntry -Syntax $tool.Name -Description $tool.Description -Width $width
        }
    }
    Write-DeviceHelpEntry -Syntax 'Device.ps1 banner' -Description 'Preview source-banner maintenance; forward tool options through -ToolArguments.' -Width $width

    Write-DeviceHelpSection -Title 'OPTIONS AND DEFAULTS' -Icon '⚙️' -Width $width
    Write-DeviceHelpSection -Title 'Firmware selection' -Icon '🎯' -Width $width -Subsection
    Write-DeviceHelpEntry -Syntax '-Profile <name> / -BoardConfig <path.yaml>' -Description 'Default profile: cody-companion. BoardConfig chooses its own profile and feature; do not combine it with -Profile or -Feature.' -Width $width
    Write-DeviceHelpText -Text 'Profiles: cody-companion, full, connected, ui-minimal, display-touch, headless, leg-edition. Features: none (default), distance, wake-word, tts-sd or tts-flash. TTS is supported on cody-companion and full only. tts-sd enables the Italian boot test; tts-flash starts silently. Synthesis uses core 0 and the renderer core 1.' -Width $width
    Write-DeviceHelpEntry -Syntax '-BuildMode Debug|Release / -Jobs 0..8' -Description 'Default mode: Debug. Jobs defaults to 0 (automatic, capped at 8).' -Width $width
    Write-DeviceHelpEntry -Syntax '-BuildDir / -IdfPath / -ToolsPath' -Description 'Override the selected build directory or the ESP-IDF installation/tool paths.' -Width $width

    Write-DeviceHelpSection -Title 'Preview and connection options' -Icon '📐' -Width $width -Subsection
    Write-DeviceHelpEntry -Syntax '-Screen / -Rotation / -AtMs / -SafeGuide' -Description 'Screens: rig, greeting, boot-vibeware, boot-sleep, boot-waking, boot-ready. Rotation: 0/90/180/270; AtMs: 1000 by default; SafeGuide shows the circular layout guide.' -Width $width
    Write-DeviceHelpEntry -Syntax '-Port / -Baud / -WaitSeconds' -Description 'Without Port, device actions ask you to choose a detected COM port. COM10 and 10 select the same port; explicit Port skips the prompt. Non-interactive calls require Port. Defaults: 115200 baud and a 30-second port wait.' -Width $width
    Write-DeviceHelpEntry -Syntax '-ResetBeforeMonitor' -Description 'For monitor and combo only: opt in to the monitor startup reset. By default, monitor attachment uses --no-reset; Baud is passed explicitly through --monitor-baud.' -Width $width
    Write-DeviceHelpEntry -Syntax '-DurationSeconds / -ResetBeforeCapture' -Description 'Capture defaults to 30 seconds; opt in to a reset before capture.' -Width $width
    Write-DeviceHelpEntry -Syntax '-StoredLog current|previous' -Description 'sd-log defaults to current; OutputPath overrides artifacts/serial/sd-log-<name>.log and WaitSeconds bounds each USB response.' -Width $width

    Write-DeviceHelpSection -Title 'Output, Python and tool arguments' -Icon '📁' -Width $width -Subsection
    Write-DeviceHelpEntry -Syntax '-OutputPath / -LogPath / -SizeOutputFile' -Description 'Output, logs and build paths must stay inside artifacts/; relative paths start at the repository root.' -Width $width
    Write-DeviceHelpText -Text 'Defaults: artifacts/logs/<command>.log, artifacts/serial/capture.log, artifacts/reports/size.<format>. Size formats: default, text, csv, json2, tree, raw.' -Width $width
    Write-DeviceHelpEntry -Syntax '-PythonExecutable / -PythonExe' -Description 'PythonExecutable selects host-tool Python; PythonExe selects the ESP-IDF interpreter.' -Width $width
    Write-DeviceHelpEntry -Syntax '-ToolArguments <string[]>' -Description 'Literal tokens for checks, generators, Luna simulator and banner tools. The menu accepts a JSON string array such as ["--previews"].' -Detail "Tools with their own help accept '--help' (Python) or '-?' (PowerShell). Options are action-specific; invalid combinations are rejected." -Width $width

    Write-Host ''
    Write-Host ('  ' + ('═' * ($width - 4))) -ForegroundColor DarkCyan
    Write-DeviceHelpText -Text '💡  Tip: use the menu for everyday work; copy CLI examples for scripts.' -Width $width -Indent '  ' -Color Cyan
    Write-Host ''
}
