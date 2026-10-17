<#
.SYNOPSIS
Create a visibly synthetic spoken WAV for the explicitly authorized audio queue pilot.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'The synthetic speech fixture requires Windows SAPI.' }
$repository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$directory = Join-Path $repository 'artifacts/checks/cody-public-audio-queue'
[IO.Directory]::CreateDirectory($directory) | Out-Null
$destination = Join-Path $directory 'synthetic-speech.wav'
if (Test-Path -LiteralPath $destination) { throw 'Synthetic speech fixture already exists; do not overwrite pilot evidence.' }
$voice = $null
$stream = $null
$opened = $false
try {
    $voice = New-Object -ComObject SAPI.SpVoice
    $stream = New-Object -ComObject SAPI.SpFileStream
    # SAPI's documented SAFT16kHz16BitMono format; output is a file, never speakers.
    $stream.Format.Type = 18
    $stream.Open($destination, 3, $false)
    $opened = $true
    $voice.AudioOutputStream = $stream
    [void]$voice.Speak('This is a synthetic Cody audio queue test. The verification phrase is mango forty seven. Continue the audio integration and report this phrase.', 0)
} finally {
    if ($opened) { $stream.Close() }
    if ($null -ne $voice) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($voice) }
    if ($null -ne $stream) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($stream) }
}
Write-Host "SYNTHETIC speech fixture: $destination"
