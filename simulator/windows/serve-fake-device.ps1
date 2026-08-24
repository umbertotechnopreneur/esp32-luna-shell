<#
.SYNOPSIS
Starts the loopback-only LUNASHELL synthetic HTTP device.

.DESCRIPTION
Runs the deterministic fake device server without opening a GUI, serial port,
or hardware connection. The Python transport rejects non-loopback bind
addresses. The API key is fixed and intentionally not a secret:
LUNASHELL-SIMULATOR-ONLY-NOT-A-SECRET.

.PARAMETER BindAddress
Loopback IP address or localhost. Defaults to 127.0.0.1.

.PARAMETER Port
TCP listen port. Defaults to 18080.

.PARAMETER Scenario
Built-in scenario name or local scenario JSON path.

.PARAMETER StartAtMs
Initial deterministic virtual-clock offset in milliseconds.

.EXAMPLE
pwsh -NoProfile -File .\simulator\windows\serve-fake-device.ps1

.EXAMPLE
pwsh -NoProfile -File .\simulator\windows\serve-fake-device.ps1 `
    -Scenario stale-recovery -Port 18081
#>
[CmdletBinding()]
param(
    [string]$BindAddress = '127.0.0.1',

    [ValidateRange(1, 65535)]
    [int]$Port = 18080,

    [string]$Scenario = 'healthy',

    [ValidateRange(0, [long]::MaxValue)]
    [long]$StartAtMs = 0
)

$ErrorActionPreference = 'Stop'

$Server = Join-Path $PSScriptRoot 'fake_device_server.py'
if (-not (Test-Path -LiteralPath $Server -PathType Leaf)) {
    throw "Missing synthetic device server: $Server"
}

$PythonCommand = $null
foreach ($Candidate in @('python', 'python3', 'py')) {
    $Command = Get-Command -Name $Candidate -ErrorAction SilentlyContinue
    if ($null -ne $Command) {
        $PythonCommand = $Command
        break
    }
}
if ($null -eq $PythonCommand) {
    throw 'Python 3 is not available in PATH.'
}

$Arguments = @(
    '-B',
    $Server,
    '--bind', $BindAddress,
    '--port', $Port.ToString([Globalization.CultureInfo]::InvariantCulture),
    '--scenario', $Scenario,
    '--start-at-ms', $StartAtMs.ToString([Globalization.CultureInfo]::InvariantCulture)
)

if ($PythonCommand.Name -eq 'py.exe' -or $PythonCommand.Name -eq 'py') {
    $Arguments = @('-3') + $Arguments
}

& $PythonCommand.Source @Arguments
if ($LASTEXITCODE -ne 0) {
    throw "Synthetic device server exited with code $LASTEXITCODE."
}
