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
try {
    $null = New-Item -ItemType Directory -Path $fixture
    $ProjectRoot = Join-Path $fixture 'project'
    $null = New-Item -ItemType Directory -Path $ProjectRoot
    $fixtureArtifacts = Join-Path $ProjectRoot 'artifacts'
    New-ConsoleFixtureFile (Join-Path $ProjectRoot 'scripts/commands/firmware.ps1') `
        "throw 'A host route invoked the firmware engine.'"
    New-ConsoleFixtureFile (Join-Path $ProjectRoot 'scripts/checks/verify_example.py') '# host fixture'
    New-ConsoleFixtureFile (Join-Path $ProjectRoot 'scripts/generators/generate_example.py') '# host fixture'

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

    Invoke-ConsoleCheck 'Cleanup retains build/config/Python caches, simulator identity and protected outputs' {
        foreach ($directory in @('build-cody-companion', 'sdkconfig', 'board-config', 'python', 'managed_components', 'cody-simulator-state', 'custom-build')) {
            New-ConsoleFixtureFile (Join-Path $fixtureArtifacts "$directory/keep.txt")
        }
        New-ConsoleFixtureFile (Join-Path $fixtureArtifacts '.device.lock')
        New-ConsoleFixtureFile (Join-Path $fixtureArtifacts 'logs/remove.txt')
        New-ConsoleFixtureFile (Join-Path $fixtureArtifacts 'simulator/remove.png')
        New-ConsoleFixtureFile (Join-Path $fixtureArtifacts 'capture.log')
        Clear-DeviceArtifacts -ProtectedPaths @((Join-Path $fixtureArtifacts 'custom-build/subtree'))
        foreach ($directory in @('build-cody-companion', 'sdkconfig', 'board-config', 'python', 'managed_components', 'cody-simulator-state', 'custom-build')) {
            Assert-ConsoleCondition (Test-Path -LiteralPath (Join-Path $fixtureArtifacts "$directory/keep.txt")) "Cleanup removed protected $directory."
        }
        Assert-ConsoleCondition (Test-Path -LiteralPath (Join-Path $fixtureArtifacts '.device.lock')) 'Cleanup removed the operation lock.'
        foreach ($removed in @('logs', 'simulator', 'capture.log')) {
            Assert-ConsoleCondition (-not (Test-Path -LiteralPath (Join-Path $fixtureArtifacts $removed))) "Cleanup retained transient $removed."
        }
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
        $null = New-Item -ItemType Junction -Path $junction -Target $outside
        try {
            Assert-ConsoleRejection { Resolve-ArtifactPath (Join-Path $junction 'future.log') '' } 'reparse point'
            Assert-ConsoleRejection { Clear-DeviceArtifacts } 'reparse point'
            Assert-ConsoleCondition (Test-Path -LiteralPath $sentinel) 'Cleanup followed the junction target.'
            Assert-ConsoleCondition (Test-Path -LiteralPath (Join-Path $fixtureArtifacts 'reports/sentinel.txt')) 'Cleanup mutated another tree before its full preflight.'
        } finally {
            # Delete only the junction entry; never recurse into its target.
            [IO.Directory]::Delete($junction)
            $junction = $null
        }
    }

    Invoke-ConsoleCheck 'Action-specific options reject ignored parameters and conflicting board selection' {
        $contract = Get-DeviceActionParameters
        Assert-DeviceActionParameters 'simulator' @{ Action = 'simulator'; Screen = 'rig'; Rotation = 180 } $contract
        Assert-DeviceActionParameters 'simulator' @{ Action = 'simulator'; Ccp = $true } $contract
        Assert-DeviceActionParameters 'capture' @{ Action = 'capture'; Port = 'COM99'; DurationSeconds = 1 } $contract
        Assert-ConsoleRejection { Assert-DeviceActionParameters 'simulator' @{ Port = 'COM99' } $contract } 'not valid'
        Assert-ConsoleRejection { Assert-DeviceActionParameters 'help' @{ LogPath = 'artifacts/logs/help.log' } $contract } 'not valid'
        Assert-ConsoleRejection { Assert-DeviceActionParameters 'build' @{ BoardConfig = 'board.yaml'; Profile = 'full' } $contract } 'BoardConfig selects'
        Assert-ConsoleRejection { Assert-DeviceActionParameters 'size' @{ DurationSeconds = 1 } $contract } 'not valid'
    }

    Invoke-ConsoleCheck 'Serial selection uses COM identity across input forms and reordered inventories' {
        $portTen = [pscustomobject]@{ DeviceID = 'COM10'; Status = 'OK' }
        $portTwo = [pscustomobject]@{ DeviceID = 'COM2'; Status = 'OK' }
        foreach ($inventory in @(@($portTen, $portTwo), @($portTwo, $portTen))) {
            foreach ($selection in @('10', 'COM10', ' com10 ')) {
                $selectedPort = Resolve-SerialPortSelection -Selection $selection -PresentPorts $inventory
                Assert-ConsoleCondition ($selectedPort -eq 'COM10') 'Serial selection depended on inventory order or input notation.'
            }
        }
        Assert-ConsoleCondition ((Normalize-PortName '10') -eq 'COM10') 'Explicit numeric Port was not normalized.'
    }

    Invoke-ConsoleCheck 'Serial selection never substitutes a port after cancellation or invalid device state' {
        $portTen = [pscustomobject]@{ DeviceID = 'COM10'; Status = 'OK' }
        $unhealthyPort = [pscustomobject]@{ DeviceID = 'COM10'; Status = 'Error' }
        Assert-ConsoleRejection { Resolve-SerialPortSelection '' @($portTen) } 'cancelled'
        Assert-ConsoleRejection { Resolve-SerialPortSelection '2' @($portTen) } 'not in the detected list'
        Assert-ConsoleRejection { Resolve-SerialPortSelection '10' @() } 'not in the detected list'
        Assert-ConsoleRejection { Resolve-SerialPortSelection '10' @($portTen, $portTen) } 'ambiguous'
        Assert-ConsoleRejection { Resolve-SerialPortSelection '10' @($unhealthyPort) } "status 'Error'"
        foreach ($invalidPort in @('0', 'COM0', '-1', 'ttyUSB0')) {
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
        Assert-ConsoleCondition ($script:hostCalls.Count -eq 1) 'Preview did not dispatch exactly one host tool.'
        $call = $script:hostCalls[0]
        Assert-ConsoleCondition ($call.Path -eq (Join-Path $ProjectRoot 'simulator/windows/cody_simulator.py')) 'Preview dispatched an unexpected script.'
        $expected = @('--screen','boot-waking','--rotation','180','--at-ms','700','--safe-guide','--capture',$destination)
        Assert-ConsoleCondition (($call.Arguments -join "`n") -ceq ($expected -join "`n")) 'Preview altered literal argument boundaries.'
        Assert-ConsoleCondition ($call.PythonExecutable -eq 'host-python') 'Preview lost the host interpreter selection.'
    }

    Invoke-ConsoleCheck 'Boot screenshots dispatch four host captures in fixed stage order' {
        $script:hostCalls.Clear()
        Invoke-DeviceCommand 'boot-screens' @{ SafeGuide = $false; PythonExecutable = 'host-python' }
        Assert-ConsoleCondition ($script:hostCalls.Count -eq 4) 'Boot capture did not dispatch four host jobs.'
        $screens = @('boot-vibeware','boot-sleep','boot-waking','boot-ready')
        for ($index = 0; $index -lt $screens.Count; $index += 1) {
            $call = $script:hostCalls[$index]
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
        Assert-ConsoleCondition ($script:hostCalls.Count -eq 5) 'A host route failed to dispatch through the mocked host owner.'
        Assert-ConsoleCondition ($script:hostCalls[0].Arguments[1] -eq 'a value') 'Check arguments were split or evaluated.'
        Assert-ConsoleCondition ($script:hostCalls[1].Arguments[1] -eq 'b value') 'Generator arguments were split or evaluated.'
        Assert-ConsoleRejection { Find-DeviceTool 'checks' '../firmware' } 'Unknown or ambiguous'
    }

    Invoke-ConsoleCheck 'Host errors propagate to the caller unchanged' {
        $script:rejectHostCall = $true
        try {
            Assert-ConsoleRejection { Invoke-DeviceCommand 'simulator-luna' @{ ToolArguments = @(); PythonExecutable = '' } } '^Synthetic host-tool failure\.$'
        } finally { $script:rejectHostCall = $false }
    }

    Write-Host "Device console host checks passed: $script:checkCount. No firmware, simulator or device operation ran."
} finally {
    $ProjectRoot = $sourceRoot
    if ($null -ne $junction -and (Test-Path -LiteralPath $junction)) {
        [IO.Directory]::Delete($junction)
    }
    if (Test-Path -LiteralPath $fixture) {
        $ownedFixture = Resolve-ArtifactPath $fixture ''
        Assert-DeviceArtifactTree -Path $ownedFixture
        Remove-Item -LiteralPath $ownedFixture -Recurse -Force -ErrorAction Stop
    }
}
