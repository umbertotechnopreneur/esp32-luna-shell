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

<#
.SYNOPSIS
Exercise device-console routing and artifact ownership with an isolated host fixture.
.DESCRIPTION
Run only on an explicit validation request. This check creates and removes its own
fixture below artifacts/checks; it never runs ESP-IDF, a simulator, or a device tool.
Host tools are mocked and a fixture firmware entrypoint fails if a host route reaches it.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ProjectRoot = $sourceRoot
. (Join-Path $sourceRoot 'scripts/common/Artifacts.ps1')
. (Join-Path $sourceRoot 'scripts/common/Commands.ps1')
. (Join-Path $sourceRoot 'scripts/common/SerialPorts.ps1')
. (Join-Path $sourceRoot 'scripts/common/Errors.ps1')
$script:checkCount = 0
$script:hostCalls = [System.Collections.Generic.List[object]]::new()
$script:rejectHostCall = $false

# Condition is the required observable result.
# Message identifies the violated behavior rather than an implementation fragment.
# Throws if the expected result is false.
function Assert-ConsoleCondition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

# Action performs one operation that must reject its input.
# Pattern identifies the expected rejection and prevents unrelated errors from passing.
# Throws if the operation succeeds or fails for a different reason.
function Assert-ConsoleRejection([scriptblock]$Action, [string]$Pattern) {
    $rejected = $false
    try { & $Action | Out-Null } catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw }
        $rejected = $true
    }
    Assert-ConsoleCondition $rejected "Expected rejection matching '$Pattern'."
}

# Name identifies one behavior in the output and any propagated failure.
# Action contains isolated host-only assertions.
# Throws the original failure after printing the failing behavior.
function Invoke-ConsoleCheck([string]$Name, [scriptblock]$Action) {
    try { & $Action } catch {
        Write-Host "FAIL: $Name"
        throw
    }
    $script:checkCount += 1
    Write-Host "PASS: $Name"
}

# Path is a literal file inside the isolated fixture.
# Contents is the exact fixture content; no command text is evaluated here.
# Throws if the fixture file cannot be created.
function New-ConsoleFixtureFile([string]$Path, [string]$Contents = 'fixture') {
    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force
    Set-Content -LiteralPath $Path -Value $Contents -Encoding utf8
}

# Path is the fixture-owned directory link entry, never its target.
# Throws the original OS error when the link entry cannot be unlinked.
function Remove-ConsoleFixtureLink([string]$Path) {
    if ([OperatingSystem]::IsWindows()) {
        [IO.Directory]::Delete($Path)
    } else {
        # Unix directory symlinks are unlinked as files, including dangling links.
        [IO.File]::Delete($Path)
    }
}

# Path is the route-selected script, recorded without invocation.
# Arguments contains literal forwarded tokens, retained in their original order.
# PythonExecutable is the selected interpreter value, retained for routing assertions.
# Throws only when a check explicitly requests the synthetic host failure.
function Invoke-HostTool([string]$Path, [string[]]$Arguments, [string]$PythonExecutable) {
    if ($script:rejectHostCall) { throw 'Synthetic host-tool failure.' }
    $script:hostCalls.Add([pscustomobject]@{
        Path = $Path
        Arguments = @($Arguments)
        PythonExecutable = $PythonExecutable
    })
}

$fixture = Resolve-ArtifactPath '' ("artifacts/checks/device-console-" + [Guid]::NewGuid().ToString('N'))
$junction = $null
$fixtureFailure = $null
try {
    $null = New-Item -ItemType Directory -Path $fixture
    $ProjectRoot = Join-Path $fixture 'project'
    $null = New-Item -ItemType Directory -Path $ProjectRoot
    $fixtureArtifacts = Join-Path $ProjectRoot 'artifacts'
    New-ConsoleFixtureFile (Join-Path $ProjectRoot 'scripts/commands/firmware.ps1') `
        "throw 'A host route invoked the firmware engine.'"
    New-ConsoleFixtureFile (Join-Path $ProjectRoot 'scripts/checks/verify_example.py') '# host fixture'
    New-ConsoleFixtureFile (Join-Path $ProjectRoot 'scripts/generators/generate_example.py') '# host fixture'
    New-ConsoleFixtureFile (Join-Path $ProjectRoot 'scripts/generators/generate_cody_simulator_graphics.py') '# host fixture'
    New-ConsoleFixtureFile (Join-Path $ProjectRoot 'scripts/generators/generate_cody_simulator_audio.py') '# host fixture'
    New-ConsoleFixtureFile (Join-Path $ProjectRoot 'scripts/generators/generate_cody_theme.py') '# host fixture'

    Invoke-ConsoleCheck 'Artifact destinations preserve explicit children and reject escapes' {
        $expected = [IO.Path]::GetFullPath((Join-Path $fixtureArtifacts 'logs/capture.log'))
        Assert-ConsoleCondition ((Resolve-ArtifactPath 'artifacts/logs/capture.log' '') -eq $expected) 'Relative artifact destination changed.'
        Assert-ConsoleCondition ((Resolve-ArtifactPath $expected '') -eq $expected) 'Absolute artifact destination changed.'
        Assert-ConsoleCondition ((Resolve-ArtifactPath '' 'artifacts/logs/capture.log') -eq $expected) 'Default destination changed.'
        foreach ($invalid in @('artifacts', 'artifacts/../source.txt', 'artifacts-other/file.log')) {
            Assert-ConsoleRejection { Resolve-ArtifactPath $invalid '' } 'must be below'
        }
        Assert-ConsoleRejection { Resolve-ArtifactPath 'artifacts/logs/file.log:stream' '' } 'alternate data streams'
        Assert-ConsoleRejection { Resolve-ArtifactPath '' '' } 'requires an explicit path'
    }

    Invoke-ConsoleCheck 'Cleanup retains builds, simulator data, document inputs and protected outputs' {
        $persistentDirectories = @(
            'build-cody-companion', 'sdkconfig', 'board-config', 'feature-config',
            'python', 'models', 'managed_components', 'cody-simulator-state', 'cody-simulator-state-linux',
            'simulator', 'docs', 'custom-build'
        )
        foreach ($directory in $persistentDirectories) {
            New-ConsoleFixtureFile (Join-Path $fixtureArtifacts "$directory/keep.txt")
        }
        New-ConsoleFixtureFile (Join-Path $fixtureArtifacts '.device.lock')
        New-ConsoleFixtureFile (Join-Path $fixtureArtifacts 'logs/remove.txt')
        $savedStudioFiles = @('simulator/studio/baselines/example/capture.json',
            'simulator/studio/reviews/example/review.json', 'simulator/studio/ui-state.json',
            'simulator/studio/captures/example/frame.png', 'simulator/custom/scenario.json',
            'docs/studio/Cody-screen-inventory.pdf')
        foreach ($path in $savedStudioFiles) {
            New-ConsoleFixtureFile (Join-Path $fixtureArtifacts $path) 'retained user data'
        }
        New-ConsoleFixtureFile (Join-Path $fixtureArtifacts 'capture.log')
        Clear-DeviceArtifacts -ProtectedPaths @((Join-Path $fixtureArtifacts 'custom-build/subtree'))
        foreach ($directory in $persistentDirectories) {
            Assert-ConsoleCondition (Test-Path -LiteralPath (Join-Path $fixtureArtifacts "$directory/keep.txt")) "Cleanup removed protected $directory."
        }
        Assert-ConsoleCondition (Test-Path -LiteralPath (Join-Path $fixtureArtifacts '.device.lock')) 'Cleanup removed the operation lock.'
        foreach ($path in $savedStudioFiles) {
            Assert-ConsoleCondition ((Get-Content -LiteralPath (Join-Path $fixtureArtifacts $path) -Raw).Trim() -ceq 'retained user data') "Cleanup changed saved Studio data: $path."
        }
        foreach ($removed in @('logs', 'capture.log')) {
            Assert-ConsoleCondition (-not (Test-Path -LiteralPath (Join-Path $fixtureArtifacts $removed))) "Cleanup retained transient $removed."
        }
    }

    Invoke-ConsoleCheck 'Public tool discovery excludes import-only helpers' {
        New-ConsoleFixtureFile (Join-Path $ProjectRoot 'scripts/checks/cody_ccp_event_vectors.py') 'def event_vectors(): pass'
        New-ConsoleFixtureFile (Join-Path $ProjectRoot 'scripts/checks/cody_studio_atlas_checks.py') 'class Tests: pass'
        New-ConsoleFixtureFile (Join-Path $ProjectRoot 'scripts/generators/cody_host_device_assets.py') 'def helper(): pass'
        New-ConsoleFixtureFile (Join-Path $ProjectRoot 'scripts/generators/minify_json_tree.py') '# public entrypoint'
        New-ConsoleFixtureFile (Join-Path $ProjectRoot 'scripts/generators/package_cody_boot.py') '# public entrypoint'
        Assert-ConsoleCondition ((Find-DeviceTool 'checks' 'example').Name -eq 'example') 'Public verifier disappeared.'
        Assert-ConsoleCondition ((Find-DeviceTool 'generators' 'example').Name -eq 'example') 'Public generator disappeared.'
        Assert-ConsoleCondition ((Find-DeviceTool 'generators' 'minify-json-tree').Name -eq 'minify-json-tree') 'Public minifier disappeared.'
        Assert-ConsoleCondition ((Find-DeviceTool 'generators' 'package-cody-boot').Name -eq 'package-cody-boot') 'Public boot packer disappeared.'
        Assert-ConsoleRejection { Find-DeviceTool 'checks' 'cody-ccp-event-vectors' } 'Unknown or ambiguous'
        Assert-ConsoleRejection { Find-DeviceTool 'checks' 'cody-studio-atlas-checks' } 'Unknown or ambiguous'
        Assert-ConsoleRejection { Find-DeviceTool 'generators' 'cody-host-device-assets' } 'Unknown or ambiguous'
    }

    Invoke-ConsoleCheck 'A live simulator releases the writer but preserves session and log ownership' {
        $lease = @{ Writer = Open-DeviceArtifactLock }
        $session = $log = $writer = $null
        try {
            $session = Open-DeviceArtifactLock -Name simulator
            $destination = Resolve-ArtifactPath '' 'artifacts/logs/simulator.log'
            $log = Open-DeviceOutputLock -Destination $destination
            $script:hostCalls.Clear()
            Invoke-DeviceCommand 'simulator' @{ StudioCase = 'P04' } -BeforeInteractiveLaunch {
                Assert-ConsoleCondition ($script:hostCalls.Count -eq 1) 'Writer released before theme preparation or after launch.'
                Release-DeviceWriter -Lease $lease
            }
            Assert-ConsoleCondition ($null -eq $lease.Writer) 'Interactive Studio retained the build writer.'
            $writer = Open-DeviceArtifactLock
            Assert-ConsoleRejection { Open-DeviceArtifactLock -Name simulator } 'Cannot acquire'
            Assert-ConsoleRejection { Open-DeviceOutputLock -Destination $destination } 'already owned or unavailable'
            $otherLog = Open-DeviceOutputLock -Destination (Resolve-ArtifactPath '' 'artifacts/logs/generate.log')
            $otherLog.Dispose()
        } finally {
            foreach ($item in @($log, $session, $writer, $lease.Writer)) {
                if ($null -ne $item) { $item.Dispose() }
            }
        }
        $nextSession = Open-DeviceArtifactLock -Name simulator
        $nextSession.Dispose()
    }

    Invoke-ConsoleCheck 'Headless Studio retains the writer throughout capture' {
        $state = @{ Called = $false }
        Invoke-DeviceCommand 'simulator' @{ StudioCase = 'P04'; StudioCapture = $true } -BeforeInteractiveLaunch {
            $state.Called = $true
        }
        Assert-ConsoleCondition (-not $state.Called) 'A headless capture released its writer.'
    }

    Invoke-ConsoleCheck 'Unsafe protected paths reject cleanup before any removal' {
        $sentinel = Join-Path $fixtureArtifacts 'reports/sentinel.txt'
        New-ConsoleFixtureFile $sentinel
        Assert-ConsoleRejection { Clear-DeviceArtifacts -ProtectedPaths @((Join-Path $ProjectRoot 'source')) } 'must be below'
        Assert-ConsoleCondition (Test-Path -LiteralPath $sentinel) 'Rejected cleanup changed an artifact.'
    }

    Invoke-ConsoleCheck 'Reparse points block resolution and the entire cleanup preflight' {
        $outside = Join-Path $fixture 'outside-artifacts'
        $sentinel = Join-Path $outside 'keep.txt'
        New-ConsoleFixtureFile $sentinel
        $junction = Join-Path $fixtureArtifacts 'linked-output'
        $linkType = if ([OperatingSystem]::IsWindows()) { 'Junction' } else { 'SymbolicLink' }
        $null = New-Item -ItemType $linkType -Path $junction -Target $outside
        $reparseFailure = $null
        try {
            Assert-ConsoleRejection { Resolve-ArtifactPath (Join-Path $junction 'future.log') '' } 'reparse point'
            Assert-ConsoleRejection { Clear-DeviceArtifacts } 'reparse point'
            Assert-ConsoleCondition (Test-Path -LiteralPath $sentinel) 'Cleanup followed the fixture link target.'
            Assert-ConsoleCondition (Test-Path -LiteralPath (Join-Path $fixtureArtifacts 'reports/sentinel.txt')) 'Cleanup mutated another tree before its full preflight.'
        } catch {
            $reparseFailure = $_
            throw
        } finally {
            try {
                Remove-ConsoleFixtureLink $junction
                $junction = $null
            } catch {
                if ($null -eq $reparseFailure) { throw }
                Write-Warning "Fixture link cleanup failed; preserving the assertion failure: $($_.Exception.Message)" -WarningAction Continue
            }
        }
    }

    Invoke-ConsoleCheck 'Action-specific options reject ignored parameters and conflicting board selection' {
        $contract = Get-DeviceActionParameters
        $fixturePort = if ($IsLinux) { '/dev/ttyACM99' } else { 'COM99' }
        Assert-DeviceActionParameters 'simulator' @{ Action = 'simulator'; Screen = 'rig'; Rotation = 180 } $contract
        Assert-DeviceActionParameters 'simulator' @{ Action = 'simulator'; Ccp = $true } $contract
        Assert-DeviceActionParameters 'capture' @{ Action = 'capture'; Port = $fixturePort; DurationSeconds = 1 } $contract
        Assert-DeviceActionParameters 'build' @{ ActivityClassifier = $true; ActivityCapture = $true } $contract
        Assert-DeviceActionParameters 'codex-audio' @{ EnableAudioQueue = $true; InstallAudioDependencies = $true } $contract
        Assert-ConsoleRejection { Assert-DeviceActionParameters 'flash' @{ EnableAudioQueue = $true } $contract } 'not valid'
        Assert-ConsoleRejection { Assert-DeviceActionParameters 'codex-audio' @{ Port = $fixturePort } $contract } 'not valid'
        Assert-ConsoleRejection { Assert-DeviceActionParameters 'build' @{ ActivityCapture = $true } $contract } 'requires -ActivityClassifier'
        Assert-ConsoleRejection { Assert-DeviceActionParameters 'simulator' @{ Port = $fixturePort } $contract } 'not valid'
        Assert-ConsoleRejection { Assert-DeviceActionParameters 'help' @{ LogPath = 'artifacts/logs/help.log' } $contract } 'not valid'
        Assert-ConsoleRejection { Assert-DeviceActionParameters 'build' @{ BoardConfig = 'board.yaml'; Profile = 'full' } $contract } 'BoardConfig selects'
        Assert-ConsoleRejection { Assert-DeviceActionParameters 'size' @{ DurationSeconds = 1 } $contract } 'not valid'
    }

    Invoke-ConsoleCheck 'Serial selection preserves host identities across reordered inventories' {
        if ($IsLinux) {
            $identities = @('/dev/ttyACM10', '/dev/ttyUSB2', '/dev/serial/by-id/usb-Cody_fixture-if00')
            $ports = @($identities | ForEach-Object { [pscustomobject]@{ DeviceID = $_; Status = 'OK' } })
            $selections = @($identities | ForEach-Object { [pscustomobject]@{ Input = $_; Expected = $_ } })
        } else {
            $ports = @([pscustomobject]@{ DeviceID = 'COM10'; Status = 'OK' },
                       [pscustomobject]@{ DeviceID = 'COM2'; Status = 'OK' })
            $selections = @('10', 'COM10', ' com10 ' | ForEach-Object {
                [pscustomobject]@{ Input = $_; Expected = 'COM10' }
            })
        }
        $reordered = @($ports)
        [array]::Reverse($reordered)
        foreach ($inventory in @($ports, $reordered)) {
            foreach ($selection in $selections) {
                $selectedPort = Resolve-SerialPortSelection -Selection $selection.Input -PresentPorts $inventory
                Assert-ConsoleCondition ($selectedPort -ceq $selection.Expected) 'Serial selection depended on inventory order or changed its host identity.'
                Assert-ConsoleCondition ((Normalize-PortName $selection.Input) -ceq $selection.Expected) 'Explicit Port normalization changed its host identity.'
            }
        }
    }

    Invoke-ConsoleCheck 'Serial selection never substitutes a port after cancellation or invalid device state' {
        $presentIdentity = if ($IsLinux) { '/dev/ttyACM10' } else { 'COM10' }
        $missingIdentity = if ($IsLinux) { '/dev/ttyUSB2' } else { '2' }
        $presentPort = [pscustomobject]@{ DeviceID = $presentIdentity; Status = 'OK' }
        $unhealthyPort = [pscustomobject]@{ DeviceID = $presentIdentity; Status = 'Error' }
        Assert-ConsoleRejection { Resolve-SerialPortSelection '' @($presentPort) } 'cancelled'
        Assert-ConsoleRejection { Resolve-SerialPortSelection $missingIdentity @($presentPort) } 'not in the detected list'
        Assert-ConsoleRejection { Resolve-SerialPortSelection $presentIdentity @() } 'not in the detected list'
        Assert-ConsoleRejection { Resolve-SerialPortSelection $presentIdentity @($presentPort, $presentPort) } 'ambiguous'
        Assert-ConsoleRejection { Resolve-SerialPortSelection $presentIdentity @($unhealthyPort) } "status 'Error'"
        $invalidPorts = if ($IsLinux) {
            @('10', 'COM10', 'ttyUSB0', '/dev/ttyS0', '/dev/ttyacm0', '/dev/serial/by-id/..', ' /dev/ttyACM10 ')
        } else {
            @('0', 'COM0', '-1', 'ttyUSB0', '/dev/ttyACM10')
        }
        foreach ($invalidPort in $invalidPorts) {
            Assert-ConsoleRejection { Normalize-PortName $invalidPort } 'Invalid serial port'
        }
    }

    Invoke-ConsoleCheck 'Shared formatter preserves the primary cause and writes diagnostic chains under the output lock' {
        $failure = $null
        try {
            throw [System.InvalidOperationException]::new(
                'Synthetic tool failed (exit code 23)',
                [System.IO.IOException]::new('Synthetic native cause'))
        } catch {
            $failure = $_
        }
        $diagnosticPath = Join-Path $fixtureArtifacts 'logs/formatter.log'
        $diagnosticLock = Open-DeviceArtifactLock
        try {
            $presentation = Write-DeviceFailure -FailureRecord $failure -Operation 'fixture' `
                -Stage 'running host stub' -LogPath $diagnosticPath -LogDetails 6>&1 | Out-String
        } finally {
            $diagnosticLock.Dispose()
        }
        Assert-ConsoleCondition ($presentation.Contains('Reason: Synthetic tool failed (exit code 23)')) 'Formatter replaced the native failure reason.'
        $diagnostics = Get-Content -LiteralPath $diagnosticPath -Raw
        foreach ($detail in @('Synthetic native cause', 'Invocation:', 'Script stack:', 'Error ID:')) {
            Assert-ConsoleCondition ($diagnostics.Contains($detail)) "Formatter omitted diagnostic detail '$detail'."
        }

        # Failing to create a log must not erase the primary operation failure.
        $blockedDirectory = Join-Path $fixtureArtifacts 'blocked-log-directory'
        New-ConsoleFixtureFile $blockedDirectory
        $diagnosticLock = Open-DeviceArtifactLock
        try {
            $presentation = Write-DeviceFailure -FailureRecord $failure -Operation 'fixture' `
                -Stage 'running host stub' -LogPath (Join-Path $blockedDirectory 'failure.log') -LogDetails 6>&1 | Out-String
        } finally {
            $diagnosticLock.Dispose()
        }
        Assert-ConsoleCondition ($presentation.Contains('Reason: Synthetic tool failed (exit code 23)')) 'Log failure masked the primary cause.'
        Assert-ConsoleCondition ($presentation.Contains('Details not written:')) 'Formatter hid the secondary logging error.'
    }

    Invoke-ConsoleCheck 'Shared formatter does not write without ownership or invent recovery for unknown errors' {
        $failure = $null
        try {
            throw 'Synthetic unclassified failure.'
        } catch {
            $failure = $_
        }
        $unownedPath = Join-Path $fixtureArtifacts 'logs/unowned.log'
        $presentation = Write-DeviceFailure -FailureRecord $failure -Operation 'fixture' `
            -Stage 'preflight' -LogPath $unownedPath 6>&1 | Out-String
        Assert-ConsoleCondition (-not (Test-Path -LiteralPath $unownedPath)) 'Formatter wrote without log ownership.'
        Assert-ConsoleCondition (-not $presentation.Contains('Next:')) 'Formatter invented advice for an unknown error.'
    }

    Invoke-ConsoleCheck 'Cody preview routes stay host-only and retain literal capture arguments' {
        $script:hostCalls.Clear()
        $destination = Resolve-ArtifactPath '' 'artifacts/simulator/a preview.png'
        Invoke-DeviceCommand 'simulator' @{
            Screen = 'boot-waking'; Rotation = 180; AtMs = 700; SafeGuide = $true
            OutputPath = $destination; PythonExecutable = 'host-python'
        }
        Assert-ConsoleCondition ($script:hostCalls.Count -eq 3) 'Preview must synchronize the theme, prepare native graphics, then capture without audio.'
        $themeCall = $script:hostCalls[0]
        Assert-ConsoleCondition ($themeCall.Path -eq (Join-Path $ProjectRoot 'scripts/generators/generate_cody_theme.py')) 'Preview did not synchronize its source theme first.'
        Assert-ConsoleCondition ($themeCall.Arguments.Count -eq 0) 'Theme synchronization received preview arguments.'
        Assert-ConsoleCondition ($themeCall.PythonExecutable -eq 'host-python') 'Theme synchronization lost interpreter selection.'
        $graphicsCall = $script:hostCalls[1]
        Assert-ConsoleCondition ($graphicsCall.Path -eq (Join-Path $ProjectRoot 'scripts/generators/generate_cody_simulator_graphics.py')) 'Preview did not prepare the production native graphics owner after theme synchronization.'
        Assert-ConsoleCondition ($graphicsCall.Arguments.Count -eq 0) 'Graphics preparation received capture arguments.'
        Assert-ConsoleCondition ($graphicsCall.PythonExecutable -eq 'host-python') 'Graphics preparation lost the host interpreter selection.'
        $call = $script:hostCalls[2]
        Assert-ConsoleCondition ($call.Path -eq (Join-Path $ProjectRoot 'cody-device/simulator/windows/cody_simulator.py')) 'Preview dispatched an unexpected script.'
        $expected = @('--screen','boot-waking','--rotation','180','--at-ms','700','--safe-guide','--capture',$destination)
        Assert-ConsoleCondition (($call.Arguments -join "`n") -ceq ($expected -join "`n")) 'Preview altered literal argument boundaries.'
        Assert-ConsoleCondition ($call.PythonExecutable -eq 'host-python') 'Preview lost the host interpreter selection.'
    }

    Invoke-ConsoleCheck 'Explicit legacy interactive preview prepares theme, graphics and audio' {
        $script:hostCalls.Clear()
        Invoke-DeviceCommand 'simulator' @{ Screen = 'rig'; PythonExecutable = 'host-python' }
        Assert-ConsoleCondition ($script:hostCalls.Count -eq 4) 'Legacy interactive preview must synchronize the theme and prepare both native owners before launch.'
        $expectedPaths = @('scripts/generators/generate_cody_theme.py',
            'scripts/generators/generate_cody_simulator_graphics.py',
            'scripts/generators/generate_cody_simulator_audio.py', 'cody-device/simulator/windows/cody_simulator.py')
        for ($index = 0; $index -lt $expectedPaths.Count; $index += 1) {
            Assert-ConsoleCondition ($script:hostCalls[$index].Path -eq (Join-Path $ProjectRoot $expectedPaths[$index])) 'Interactive native owner preparation order changed.'
            Assert-ConsoleCondition ($script:hostCalls[$index].PythonExecutable -eq 'host-python') 'Interactive route lost interpreter selection.'
        }
    }

    Invoke-ConsoleCheck 'Default simulator selects the integrated native runtime with explicit scenario arguments' {
        $defaultScenario = Join-Path $ProjectRoot 'cody-device/simulator/windows/scenarios/cody-native-first-boot.json'
        $customScenario = Join-Path $fixtureArtifacts 'simulator/native scenario.json'
        foreach ($options in @(@{ PythonExecutable = 'host-python' },
                @{ DeviceScenario = $customScenario; PythonExecutable = 'host-python' })) {
            $script:hostCalls.Clear()
            Invoke-DeviceCommand 'simulator' $options
            Assert-ConsoleCondition ($script:hostCalls.Count -eq 3) 'Native simulator must synchronize its theme, prepare device graphics and launch the integrated runtime.'
            $expectedPaths = @('scripts/generators/generate_cody_theme.py',
                'scripts/generators/generate_cody_simulator_graphics.py', 'cody-device/simulator/windows/cody_simulator.py')
            for ($index = 0; $index -lt $expectedPaths.Count; $index += 1) {
                Assert-ConsoleCondition ($script:hostCalls[$index].Path -eq (Join-Path $ProjectRoot $expectedPaths[$index])) 'Native simulator preparation order changed.'
                Assert-ConsoleCondition ($script:hostCalls[$index].PythonExecutable -eq 'host-python') 'Native simulator lost interpreter selection.'
            }
            Assert-ConsoleCondition ($script:hostCalls[0].Arguments.Count -eq 0) 'Theme synchronization received native scenario arguments.'
            Assert-ConsoleCondition (($script:hostCalls[1].Arguments -join "`n") -ceq '--device-runtime') 'Native simulator prepared a legacy graphics driver.'
            $scenario = if ($options.DeviceScenario) { $customScenario } else { $defaultScenario }
            $expected = @('--device-runtime', '--device-scenario', $scenario)
            Assert-ConsoleCondition (($script:hostCalls[2].Arguments -join "`n") -ceq ($expected -join "`n")) 'Native simulator altered the explicit scenario arguments.'
        }
    }

    Invoke-ConsoleCheck 'Controlled Studio dispatches explicitly without an implicit native build or legacy options' {
        $script:hostCalls.Clear()
        $contract = Get-DeviceActionParameters
        $scenario = Join-Path $fixtureArtifacts 'simulator/Studio scenario.json'
        $options = @{ StudioCase = 'P04'; StudioScenario = $scenario; StudioCapture = $true; PythonExecutable = 'host-python' }
        Assert-DeviceActionParameters 'simulator' $options $contract
        Invoke-DeviceCommand 'simulator' $options
        Assert-ConsoleCondition ($script:hostCalls.Count -eq 2) 'Controlled Studio must synchronize its theme and dispatch without rebuilding native graphics.'
        Assert-ConsoleCondition ($script:hostCalls[0].Path -eq (Join-Path $ProjectRoot 'scripts/generators/generate_cody_theme.py')) 'Controlled Studio omitted theme synchronization.'
        Assert-ConsoleCondition ($script:hostCalls[0].Arguments.Count -eq 0) 'Studio arguments reached theme synchronization.'
        $call = $script:hostCalls[1]
        Assert-ConsoleCondition ($call.Path -eq (Join-Path $ProjectRoot 'cody-device/simulator/windows/cody_simulator.py')) 'Controlled Studio dispatched an unexpected script.'
        Assert-ConsoleCondition ($call.PythonExecutable -eq 'host-python') 'Controlled Studio lost interpreter selection.'
        $expected = @('--studio-case', 'P04', '--studio-scenario', $scenario, '--studio-capture')
        Assert-ConsoleCondition (($call.Arguments -join "`n") -ceq ($expected -join "`n")) 'Controlled Studio changed literal scenario arguments.'
        foreach ($legacyOption in @('Screen', 'AtMs', 'OutputPath', 'Rotation', 'SafeGuide', 'Ccp', 'DeviceRuntime', 'DeviceScenario')) {
            $invalid = @{ StudioCase = 'P04' }
            $invalid[$legacyOption] = 'explicit-option'
            Assert-ConsoleRejection { Assert-DeviceActionParameters 'simulator' $invalid $contract } 'cannot be combined with controlled Studio scenarios'
        }
        Assert-ConsoleRejection { Assert-DeviceActionParameters 'simulator' @{ StudioCapture = $true } $contract } 'requires -StudioCase or -StudioScenario'
        Assert-ConsoleCondition ($script:hostCalls.Count -eq 2) 'Rejected Studio options reached a host tool.'
    }

    Invoke-ConsoleCheck 'Boot screenshots dispatch four host captures in fixed stage order' {
        $script:hostCalls.Clear()
        Invoke-DeviceCommand 'boot-screens' @{ SafeGuide = $false; PythonExecutable = 'host-python' }
        Assert-ConsoleCondition ($script:hostCalls.Count -eq 5) 'Boot capture must prepare graphics and dispatch four host jobs.'
        Assert-ConsoleCondition ($script:hostCalls[0].Path -eq (Join-Path $ProjectRoot 'scripts/generators/generate_cody_simulator_graphics.py')) 'Boot capture did not prepare native graphics first.'
        $screens = @('boot-vibeware','boot-sleep','boot-waking','boot-ready')
        for ($index = 0; $index -lt $screens.Count; $index += 1) {
            $call = $script:hostCalls[$index + 1]
            Assert-ConsoleCondition ($call.Arguments[1] -eq $screens[$index]) 'Boot stage order changed.'
            Assert-ConsoleCondition ($call.Arguments[2] -eq '--capture') 'Boot stage capture flag missing.'
            Assert-ConsoleCondition ($call.Arguments[3] -eq (Join-Path $fixtureArtifacts "simulator/$($screens[$index]).png")) 'Boot capture escaped the simulator artifact directory.'
        }
    }

    Invoke-ConsoleCheck 'Named checks/generators and Luna preview never enter the firmware engine' {
        $script:hostCalls.Clear()
        Invoke-DeviceCommand 'check' @{ Check = 'example'; ToolArguments = @('--literal','a value'); PythonExecutable = 'host-python' }
        Invoke-DeviceCommand 'generate' @{ Generator = 'example'; ToolArguments = @('--literal','b value'); PythonExecutable = 'host-python' }
        Invoke-DeviceCommand 'simulator-luna' @{ ToolArguments = @('--screen','home'); PythonExecutable = 'host-python' }
        Invoke-DeviceCommand 'package-simulator' @{}
        Invoke-DeviceCommand 'banner' @{ ToolArguments = @('-?') }
        Assert-ConsoleCondition ($script:hostCalls.Count -eq 7) 'A host route omitted a required theme synchronization or mocked host dispatch.'
        $expectedPaths = @('scripts/generators/generate_cody_theme.py', 'scripts/checks/verify_example.py',
            'scripts/generators/generate_example.py', 'cody-device/simulator/windows/lunashell_simulator.py',
            'scripts/generators/generate_cody_theme.py', 'scripts/commands/package-simulator.ps1',
            'scripts/commands/add-source-banner.ps1')
        for ($index = 0; $index -lt $expectedPaths.Count; $index += 1) {
            Assert-ConsoleCondition ($script:hostCalls[$index].Path -eq (Join-Path $ProjectRoot $expectedPaths[$index])) 'Named host routes changed their theme or dispatch order.'
        }
        Assert-ConsoleCondition ($script:hostCalls[0].Arguments.Count -eq 0 -and $script:hostCalls[4].Arguments.Count -eq 0) 'Theme synchronization received host action arguments.'
        Assert-ConsoleCondition ($script:hostCalls[0].PythonExecutable -eq 'host-python') 'Check theme synchronization lost interpreter selection.'
        Assert-ConsoleCondition (($script:hostCalls[1].Arguments -join "`n") -ceq (@('--literal', 'a value') -join "`n")) 'Check arguments were split or evaluated.'
        Assert-ConsoleCondition (($script:hostCalls[2].Arguments -join "`n") -ceq (@('--literal', 'b value') -join "`n")) 'Generator arguments were split or evaluated.'
        Assert-ConsoleRejection { Find-DeviceTool 'checks' '../firmware' } 'Unknown or ambiguous'
    }

    Invoke-ConsoleCheck 'Host errors propagate to the caller unchanged' {
        $script:rejectHostCall = $true
        try {
            Assert-ConsoleRejection { Invoke-DeviceCommand 'simulator-luna' @{ ToolArguments = @(); PythonExecutable = '' } } '^Synthetic host-tool failure\.$'
        } finally { $script:rejectHostCall = $false }
    }

    Write-Host "Device console host checks passed: $script:checkCount. No firmware, simulator or device operation ran."
} catch {
    $fixtureFailure = $_
    throw
} finally {
    $ProjectRoot = $sourceRoot
    try {
        if ($null -ne $junction -and (Test-Path -LiteralPath $junction)) {
            Remove-ConsoleFixtureLink $junction
        }
        if (Test-Path -LiteralPath $fixture) {
            $ownedFixture = Resolve-ArtifactPath $fixture ''
            Assert-DeviceArtifactTree -Path $ownedFixture
            Remove-Item -LiteralPath $ownedFixture -Recurse -Force -ErrorAction Stop
        }
    } catch {
        if ($null -eq $fixtureFailure) { throw }
        Write-Warning "Fixture cleanup failed; preserving the original check failure: $($_.Exception.Message)" -WarningAction Continue
    }
}
