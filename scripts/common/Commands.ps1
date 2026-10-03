# Private command routing. The firmware engine keeps the existing profile/lock gates.

# No parameters: return the explicitly supported firmware/device operations.
function Get-FirmwareActions {
    return @('build','clean','fullclean','flash','reset','monitor','capture','erase',
        'reconfigure','set-target','update-lock','size','combo','ports','discovery','port-status','recover')
}

# No parameters: describe public options once for CLI rejection and menu forwarding.
function Get-DeviceActionParameters {
    $idf = @('Profile','Feature','BoardConfig','BuildDir','IdfPath','ToolsPath','PythonExe')
    $serial = @('Port','Baud','WaitSeconds')
    $preview = @('Screen','Rotation','AtMs','SafeGuide','CloudMaterialStudy','CloudPngMeshStudy','CloudJellyStudy','MeshOverlay','PythonExecutable','Skin')
    return @{
        menu = $idf + $serial + @('Jobs','PythonExecutable','Rotation')
        help = @(); list = @(); 'clean-artifacts' = @('BuildDir')
        simulator = $preview + @('OutputPath')
        'boot-screens' = @('Rotation','AtMs','SafeGuide','PythonExecutable')
        'simulator-luna' = @('PythonExecutable','ToolArguments')
        'package-simulator' = @()
        check = @('Check','PythonExecutable','ToolArguments')
        generate = @('Generator','PythonExecutable','ToolArguments')
        banner = @('ToolArguments')
        build = $idf + @('Jobs'); clean = $idf; fullclean = $idf
        flash = $idf + $serial + @('Jobs'); reset = $idf + $serial
        monitor = $idf + $serial; capture = $idf + $serial + @('DurationSeconds','OutputPath','ResetBeforeCapture')
        erase = $idf + $serial; reconfigure = $idf; 'set-target' = $idf; 'update-lock' = $idf
        size = $idf + @('SizeOutputFormat','SizeOutputFile')
        combo = $idf + $serial + @('Jobs'); recover = $idf + $serial + @('Jobs')
        ports = $idf; discovery = @(); 'port-status' = $idf + @('Port')
    }
}

# RequestedAction names the selected public route.
# BoundParameters contains only explicitly supplied caller options.
# Contract contains the allowed options for each action.
# Throws for ignored, contradictory or action-inappropriate options before any side effect.
function Assert-DeviceActionParameters($RequestedAction, $BoundParameters, $Contract) {
    $common = @('Action','Verbose','Debug','ErrorAction','WarningAction','InformationAction',
        'ProgressAction','ErrorVariable','WarningVariable','InformationVariable','OutVariable','OutBuffer','PipelineVariable')
    if ($RequestedAction -notin @('menu','help','list')) { $common += 'LogPath' }
    foreach ($key in $BoundParameters.Keys) {
        if ($key -notin ($common + $Contract[$RequestedAction])) {
            throw "-$key is not valid for '$RequestedAction'. Use device.ps1 help."
        }
    }
    if ($BoundParameters.ContainsKey('BoardConfig') -and
        ($BoundParameters.ContainsKey('Profile') -or $BoundParameters.ContainsKey('Feature'))) {
        throw 'BoardConfig selects its own Profile and Feature; do not combine these options.'
    }
}

# Group selects the checks or generators directory, never a caller-provided path.
function Get-DeviceTools([ValidateSet('checks','generators')][string]$Group) {
    Get-ChildItem -LiteralPath (Join-Path $ProjectRoot "scripts/$Group") -File |
        Where-Object { $_.Extension -in @('.py','.ps1') -and $_.BaseName -ne '__init__' } |
        ForEach-Object {
            $name = $_.BaseName -replace '^(verify_|generate_|generate-)', '' -replace '_','-'
            [pscustomobject]@{ Name = $name; Path = $_.FullName; Extension = $_.Extension }
        } | Sort-Object Name
}

# Group selects one of the two owned tool directories.
# Name is an exact catalog identifier, not executable text or a path.
# Throws when a tool is missing or its normalized identifier is ambiguous.
function Find-DeviceTool([string]$Group, [string]$Name) {
    $matches = @(Get-DeviceTools $Group | Where-Object Name -eq $Name)
    if ($matches.Count -ne 1) { throw "Unknown or ambiguous $Group tool '$Name'. Use device.ps1 list." }
    return $matches[0]
}

# RequestedExecutable optionally selects an existing host Python environment.
# Throws if the explicit executable or every documented host Python candidate is unavailable.
function Resolve-HostPython([string]$RequestedExecutable) {
    if ($RequestedExecutable) {
        return (Get-Command $RequestedExecutable -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    }
    $localPython = Resolve-ArtifactPath '' 'artifacts/python/simulator/Scripts/python.exe'
    if (Test-Path -LiteralPath $localPython -PathType Leaf) { return $localPython }
    foreach ($candidate in @('python','python3','py')) {
        $command = Get-Command $candidate -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($command) { return $command.Source }
    }
    throw 'Host Python is unavailable. Supply -PythonExecutable with Pillow and PyYAML installed.'
}

# Path is a repository-owned script already resolved by the dispatcher.
# Arguments contains literal CLI tokens; no string is evaluated as PowerShell code.
# PythonExecutable selects host Python for Python tools only.
# Throws when the invoked tool reports a failure.
function Invoke-HostTool([string]$Path, [string[]]$Arguments, [string]$PythonExecutable) {
    if ([IO.Path]::GetExtension($Path) -eq '.py') {
        $python = Resolve-HostPython $PythonExecutable
        & $python -B $Path @Arguments
    } else {
        & (Join-Path $PSHOME 'pwsh.exe') -NoProfile -File $Path @Arguments
    }
    if ($LASTEXITCODE -ne 0) { throw "Tool '$Path' failed with exit code $LASTEXITCODE." }
}

# RequestedAction names the already-validated public route.
# Options carries explicit parameters plus resolved output paths.
# Throws the underlying command error; a failed operation never becomes success.
function Invoke-DeviceCommand([string]$RequestedAction, [hashtable]$Options) {
    if ($RequestedAction -in (Get-FirmwareActions)) {
        $firmwareOptions = @{ Action = $RequestedAction }
        foreach ($key in (Get-DeviceActionParameters)[$RequestedAction]) {
            if ($Options.ContainsKey($key)) { $firmwareOptions[$key] = $Options[$key] }
        }
        & (Join-Path $ProjectRoot 'scripts/commands/firmware.ps1') @firmwareOptions
        return
    }
    switch ($RequestedAction) {
        { $_ -in @('simulator','boot-screens') } {
            $screens = if ($RequestedAction -eq 'boot-screens') { @('boot-sleep','boot-waking','boot-ready') }
                elseif ($Options.ContainsKey('Screen')) { @($Options.Screen) } else { @('rig') }
            foreach ($screenName in $screens) {
                $arguments = @('--screen',$screenName)
                if ($Options.ContainsKey('Skin')) { $arguments += @('--skin',$Options.Skin) }
                if ($Options.ContainsKey('Rotation')) { $arguments += @('--rotation',[string]$Options.Rotation) }
                if ($Options.ContainsKey('AtMs')) { $arguments += @('--at-ms',[string]$Options.AtMs) }
                if ($Options.SafeGuide) { $arguments += '--safe-guide' }
                if ($Options.CloudMaterialStudy) { $arguments += '--cloud-material-study' }
                if ($Options.CloudPngMeshStudy) { $arguments += '--cloud-png-mesh-study' }
                if ($Options.CloudJellyStudy) { $arguments += '--cloud-jelly-study' }
                if ($Options.MeshOverlay) { $arguments += '--mesh-overlay' }
                $capture = if ($RequestedAction -eq 'boot-screens') {
                    Resolve-ArtifactPath '' "artifacts/simulator/$screenName.png"
                } else { $Options.OutputPath }
                if ($capture) { $arguments += @('--capture',$capture) }
                Invoke-HostTool (Join-Path $ProjectRoot 'simulator/windows/cody_simulator.py') $arguments $Options.PythonExecutable
            }
        }
        'simulator-luna' { Invoke-HostTool (Join-Path $ProjectRoot 'simulator/windows/lunashell_simulator.py') $Options.ToolArguments $Options.PythonExecutable }
        'package-simulator' { Invoke-HostTool (Join-Path $ProjectRoot 'scripts/commands/package-simulator.ps1') @() '' }
        'check' {
            $name = if ($Options.Check) { $Options.Check } else { 'cody-poc' }
            $tool = Find-DeviceTool 'checks' $name
            Invoke-HostTool $tool.Path $Options.ToolArguments $Options.PythonExecutable
        }
        'generate' {
            $name = if ($Options.Generator) { $Options.Generator } else { 'cody-rig' }
            $tool = Find-DeviceTool 'generators' $name
            Invoke-HostTool $tool.Path $Options.ToolArguments $Options.PythonExecutable
        }
        'banner' { Invoke-HostTool (Join-Path $ProjectRoot 'scripts/commands/add-source-banner.ps1') $Options.ToolArguments '' }
        'clean-artifacts' { Write-Host 'Transient artifacts removed; firmware, configuration and Python caches retained.' }
        default { throw "No command implementation for '$RequestedAction'." }
    }
}

# No parameters: show command discovery without toolchain imports or device probes.
function Show-DeviceHelp {
    Write-Host @'
CODY COMPANION / DEVICE CONSOLE
  device.ps1                              Interactive menu
  device.ps1 simulator                    Cody rig and debug controls
  device.ps1 simulator -Skin assets/characters/example.yaml
  device.ps1 simulator -Screen boot-ready -Rotation 180
  device.ps1 simulator -OutputPath artifacts/simulator/rig.png
  device.ps1 simulator -CloudMaterialStudy -OutputPath artifacts/simulator/cloud-study.png
  device.ps1 simulator -CloudPngMeshStudy -OutputPath artifacts/simulator/cloud-mesh.png
  device.ps1 simulator -CloudPngMeshStudy -OutputPath artifacts/simulator/cloud-mesh.gif
  device.ps1 simulator -CloudPngMeshStudy -MeshOverlay -OutputPath artifacts/simulator/cloud-mesh-wire.png
  device.ps1 simulator -CloudJellyStudy      Interactive pearlescent scanline study
  device.ps1 simulator -CloudJellyStudy -OutputPath artifacts/simulator/cloud-jelly.gif
  device.ps1 boot-screens                  Capture all three boot stages
  device.ps1 check -Check cody-poc          Explicit host checks
  device.ps1 generate -Generator cody-rig  Regenerate authored firmware tables
  device.ps1 build -Jobs 8                 Firmware compilation
  device.ps1 flash -Port COM8              Build/flash selected firmware
  device.ps1 capture -Port COM8 -DurationSeconds 30
  device.ps1 size                          Save size report
  device.ps1 clean-artifacts               Remove transient outputs, retain caches

Firmware: build, clean, fullclean, set-target, reconfigure, update-lock, size.
Device: flash, reset, monitor, capture, erase, combo, recover, ports, discovery, port-status.
Other: simulator-luna, package-simulator (Luna EXE), banner.
Host Python: -PythonExecutable; ESP-IDF Python: -PythonExe.
Tools accept literal -ToolArguments tokens; use '--help' or '-?' for their own options.
Output/log/build paths must be inside artifacts/. No action runs on opening this help/menu.
Before build/flash/combo/recover, old transient artifacts are removed.
Build/config/Python caches remain; fullclean explicitly cleans the selected firmware build.
'@
    foreach ($group in @('checks','generators')) {
        Write-Host "`n$($group.ToUpperInvariant()):"
        foreach ($tool in (Get-DeviceTools $group)) { Write-Host "  $($tool.Name)" }
    }
}
