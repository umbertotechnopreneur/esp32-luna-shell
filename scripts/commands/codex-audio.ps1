<#
.SYNOPSIS
Prepare optional Cody audio and configure an existing Codex Desktop listener.
#>
[CmdletBinding()]
param(
    [string]$CodexAudioEndpoint,
    [string]$CodexAudioTokenFile,
    [switch]$InstallAudioDependencies,
    [string]$PythonExecutable
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$plugin = Join-Path $repository 'plugins/cody-codex-plugin'

# Endpoint: Explicit URL supplied by the operator; never inferred from other listeners.
# Throws when no matching listener belongs to the app-server child of Codex Desktop.
function Assert-CodexDesktopAudioEndpoint([string]$Endpoint) {
    if (-not $IsWindows) { throw 'Codex Desktop audio setup currently requires Windows.' }
    $processes = @(Get-CimInstance Win32_Process -ErrorAction Stop)
    $servers = @($processes | Where-Object { $_.Name -eq 'codex.exe' -and $_.CommandLine -match '\bapp-server\b' })
    $desktopServers = @($servers | Where-Object {
        $parentId = $_.ParentProcessId
        $parent = @($processes | Where-Object ProcessId -eq $parentId)
        $parent.Count -eq 1 -and $parent[0].Name -in @('ChatGPT.exe','Codex.exe') -and
            $parent[0].ExecutablePath -match '\\WindowsApps\\OpenAI\.(Codex|ChatGPT)_[^\\]+\\app\\(ChatGPT|Codex)\.exe$'
    })
    if (-not $Endpoint) {
        Write-Host "Codex Desktop app-server processes: $($desktopServers.Count)."
        $listeners = @(Get-NetTCPConnection -State Listen -ErrorAction Stop |
            Where-Object OwningProcess -in $desktopServers.ProcessId)
        Write-Host "TCP listeners owned by those processes: $($listeners.Count)."
        throw 'No explicit native audio endpoint. This setup never launches another Codex server. Configuration was not changed.'
    }
    $target = $null
    $address = $null
    if ($Endpoint.Length -gt 512 -or -not [uri]::TryCreate($Endpoint, [UriKind]::Absolute, [ref]$target) -or
        $target.Scheme -ne 'ws' -or $target.Port -lt 1 -or $target.UserInfo -or $target.Query -or $target.Fragment -or
        -not [Net.IPAddress]::TryParse($target.DnsSafeHost, [ref]$address) -or -not [Net.IPAddress]::IsLoopback($address)) {
        throw 'Supply a literal loopback WebSocket endpoint with an explicit port and no credentials.'
    }
    $listeners = @(Get-NetTCPConnection -State Listen -LocalPort $target.Port -ErrorAction Stop |
        Where-Object { $_.OwningProcess -in $desktopServers.ProcessId -and
            $_.LocalAddress -eq $address.ToString() })
    if ($listeners.Count -ne 1) {
        throw 'The endpoint is not a unique loopback listener of the running Codex Desktop app-server. Configuration was not changed.'
    }
}

$pythonName = if ($PythonExecutable) { $PythonExecutable } else { 'python' }
$python = (Get-Command $pythonName -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
if ($InstallAudioDependencies) {
    # Select the same interpreter as MCP; the simulator environment is unrelated.
    & $python -m pip install --disable-pip-version-check -r (Join-Path $plugin 'mcp/requirements-audio.txt')
    if ($LASTEXITCODE -ne 0) { throw "Cody audio dependency installation failed (exit code $LASTEXITCODE)." }
}
Assert-CodexDesktopAudioEndpoint $CodexAudioEndpoint
$arguments = @('--endpoint', $CodexAudioEndpoint)
if ($CodexAudioTokenFile) { $arguments += @('--token-file', $CodexAudioTokenFile) }
& $python -B (Join-Path $PSScriptRoot 'configure_codex_audio.py') @arguments
if ($LASTEXITCODE -ne 0) { throw "Codex audio setup failed (exit code $LASTEXITCODE)." }
