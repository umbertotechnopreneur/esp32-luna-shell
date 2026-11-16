<#
.SYNOPSIS
Exercise public dispatch concurrency with a file-signalled simulator stub.
.DESCRIPTION
No compiler, native runtime or device is started. Fixture output stays in artifacts.
#>
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $ProjectRoot 'scripts/common/Artifacts.ps1')
. (Join-Path $ProjectRoot 'scripts/common/Commands.ps1')
$sourceRoot = $ProjectRoot
$python = Resolve-HostPython ''
$fixture = Resolve-ArtifactPath '' ("artifacts/checks/device-concurrency-" + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $fixture

# Relative is an owned fixture path.
# Content is literal stub source, never evaluated by PowerShell.
# Throws original fixture I/O failures.
function Write-FixtureFile([string]$Relative, [string]$Content) {
    $path = Join-Path $fixture $Relative
    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force
    [IO.File]::WriteAllText($path, $Content)
}

# Arguments are literal public CLI tokens for the isolated checkout.
# Throws original process launch failures; drains both pipes asynchronously.
function Start-FixtureCommand([string[]]$Arguments) {
    $info = [Diagnostics.ProcessStartInfo]::new((Join-Path $PSHOME $(if ($IsWindows) { 'pwsh.exe' } else { 'pwsh' })))
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.WorkingDirectory = $fixture
    foreach ($token in @('-NoProfile', '-File', (Join-Path $fixture 'scripts/device.ps1')) + $Arguments) {
        $info.ArgumentList.Add($token)
    }
    $process = [Diagnostics.Process]::Start($info)
    return @{ Process = $process; Out = $process.StandardOutput.ReadToEndAsync(); Err = $process.StandardError.ReadToEndAsync() }
}

# Job is a fixture-owned child with drained output streams.
# Success selects whether this command must succeed or reject the request.
# Throws on timeout or an unexpected exit, including the original output.
function Complete-FixtureCommand([hashtable]$Job, [bool]$Success) {
    if (-not $Job.Process.WaitForExit(30000)) {
        $Job.Process.Kill($true)
        throw 'Fixture command exceeded its 30-second deadline.'
    }
    $output = $Job.Out.GetAwaiter().GetResult() + $Job.Err.GetAwaiter().GetResult()
    if (($Job.Process.ExitCode -eq 0) -ne $Success) { throw "Unexpected fixture exit: $output" }
    return $output
}

foreach ($relative in @('scripts/device.ps1', 'scripts/common/Artifacts.ps1', 'scripts/common/Commands.ps1',
        'scripts/common/Errors.ps1', 'scripts/common/Console.ps1', 'scripts/common/Help.ps1', 'scripts/common/SerialPorts.ps1')) {
    Write-FixtureFile $relative ([IO.File]::ReadAllText((Join-Path $sourceRoot $relative)))
}
Write-FixtureFile 'scripts/generators/generate_cody_theme.py' '# No authored theme in this fixture.'
Write-FixtureFile 'scripts/generators/generate_fixture.py' @'
from pathlib import Path
Path('artifacts/generator-completed').write_text('complete')
print('fixture generator completed')
'@
Write-FixtureFile 'cody-device/simulator/windows/cody_simulator.py' @'
from pathlib import Path
import time
root = Path('artifacts')
(root / 'ui-ready').write_text('ready')
print('fixture UI ready', flush=True)
deadline = time.monotonic() + 30
while not (root / 'ui-finish').exists():
    if time.monotonic() >= deadline:
        raise RuntimeError('Fixture UI exceeded its deadline')
    time.sleep(0.02)
print('fixture UI completed')
'@
$ui = $null
try {
    $ui = Start-FixtureCommand @('simulator', '-StudioCase', 'P04', '-PythonExecutable', $python)
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while (-not (Test-Path (Join-Path $fixture 'artifacts/ui-ready')) -or
           -not (Test-Path (Join-Path $fixture 'artifacts/logs/simulator.log'))) {
        if ($ui.Process.HasExited) { throw (Complete-FixtureCommand $ui $false) }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Fixture UI did not publish readiness.' }
        Start-Sleep -Milliseconds 20
    }
    $generation = Start-FixtureCommand @('generate', '-Generator', 'fixture', '-PythonExecutable', $python)
    $null = Complete-FixtureCommand $generation $true
    if (-not (Test-Path (Join-Path $fixture 'artifacts/generator-completed'))) { throw 'Concurrent generator did not execute.' }
    $logBefore = [IO.File]::ReadAllBytes((Join-Path $fixture 'artifacts/logs/simulator.log'))
    $conflict = Start-FixtureCommand @('generate', '-Generator', 'fixture', '-LogPath', 'artifacts/logs/simulator.log', '-PythonExecutable', $python)
    $rejection = Complete-FixtureCommand $conflict $false
    if ($rejection -notmatch 'already owned or unavailable') { throw "Wrong log rejection: $rejection" }
    $logAfter = [IO.File]::ReadAllBytes((Join-Path $fixture 'artifacts/logs/simulator.log'))
    if ([Convert]::ToBase64String($logBefore) -cne [Convert]::ToBase64String($logAfter)) { throw 'Rejected writer changed the live simulator log.' }
    $captureConflict = Start-FixtureCommand @('simulator', '-Screen', 'rig', '-OutputPath', 'artifacts/logs/simulator.log', '-LogPath', 'artifacts/logs/capture.log', '-PythonExecutable', $python)
    $rejection = Complete-FixtureCommand $captureConflict $false
    if ($rejection -notmatch 'already owned or unavailable') { throw "Wrong data-output rejection: $rejection" }
    $logAfter = [IO.File]::ReadAllBytes((Join-Path $fixture 'artifacts/logs/simulator.log'))
    if ([Convert]::ToBase64String($logBefore) -cne [Convert]::ToBase64String($logAfter)) { throw 'Rejected capture changed the live simulator log.' }
    $cleanup = Start-FixtureCommand @('clean-artifacts')
    $rejection = Complete-FixtureCommand $cleanup $false
    if ($rejection -notmatch 'simulator.lock') { throw "Wrong cleanup rejection: $rejection" }
    if (-not (Test-Path (Join-Path $fixture 'artifacts/ui-ready'))) { throw 'Cleanup modified active outputs.' }
    Write-FixtureFile 'artifacts/ui-finish' 'finish'
    $null = Complete-FixtureCommand $ui $true
    $after = Start-FixtureCommand @('generate', '-Generator', 'fixture', '-PythonExecutable', $python)
    $null = Complete-FixtureCommand $after $true
    Write-Host 'PASS: public generation coexists with Studio; logs and cleanup remain protected; exit releases ownership.'
} finally {
    if ($null -ne $ui -and -not $ui.Process.HasExited) {
        Write-FixtureFile 'artifacts/ui-finish' 'finish'
        if (-not $ui.Process.WaitForExit(5000)) { $ui.Process.Kill($true) }
    }
}
