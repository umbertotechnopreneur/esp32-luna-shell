<#
.SYNOPSIS
Opt in to public Codex WAV queue delivery using the installed native executable.
#>
[CmdletBinding()]
param(
    [string]$CodexExecutable,
    [switch]$EnableAudioQueue,
    [switch]$InstallAudioDependencies,
    [string]$PythonExecutable
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$plugin = Join-Path $repository 'plugins/cody-codex-plugin'

# ExplicitPath: Optional operator selection; otherwise inspect the actual Desktop child.
# Throws if the installed executable is missing or the running Desktop selection is ambiguous.
function Resolve-CodexAudioExecutable([string]$ExplicitPath) {
    if ($ExplicitPath) {
        $selected = Get-Item -LiteralPath $ExplicitPath -ErrorAction Stop
        if ($selected.PSIsContainer -or $selected.Name -notin @('codex.exe','codex')) {
            throw 'Select the installed native Codex executable.'
        }
        return $selected.FullName
    }
    if (-not $IsWindows) { throw 'Specify -CodexExecutable for this platform.' }
    $processes = @(Get-CimInstance Win32_Process -ErrorAction Stop)
    $servers = @($processes | Where-Object { $_.Name -eq 'codex.exe' -and $_.CommandLine -match '\bapp-server\b' })
    $desktopServers = @($servers | Where-Object {
        $parentId = $_.ParentProcessId
        $parent = @($processes | Where-Object ProcessId -eq $parentId)
        $parent.Count -eq 1 -and $parent[0].Name -in @('ChatGPT.exe','Codex.exe') -and
            $parent[0].ExecutablePath -match '\\WindowsApps\\OpenAI\.(Codex|ChatGPT)_[^\\]+\\app\\(ChatGPT|Codex)\.exe$'
    })
    $paths = @($desktopServers.ExecutablePath | Sort-Object -Unique)
    if ($paths.Count -ne 1) { throw 'Cannot select one running Desktop native CLI; specify -CodexExecutable explicitly.' }
    return $paths[0]
}

$native = Resolve-CodexAudioExecutable $CodexExecutable
Write-Host "Installed Codex executable: $native"
if (-not $EnableAudioQueue) {
    Write-Host 'Configuration unchanged. Use -EnableAudioQueue to enable public experimental WAV queue delivery.'
    Write-Host 'This route attaches audio to the existing chat; it does not transcribe it.'
    return
}
$pythonName = if ($PythonExecutable) { $PythonExecutable } else { 'python' }
$python = (Get-Command $pythonName -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
if ($InstallAudioDependencies) {
    & $python -m pip install --disable-pip-version-check -r (Join-Path $plugin 'mcp/requirements-audio.txt')
    if ($LASTEXITCODE -ne 0) { throw "Cody audio dependency installation failed (exit code $LASTEXITCODE)." }
}
& $python -B (Join-Path $PSScriptRoot 'configure_codex_audio.py') --codex-executable $native
if ($LASTEXITCODE -ne 0) { throw "Codex audio queue setup failed (exit code $LASTEXITCODE)." }
