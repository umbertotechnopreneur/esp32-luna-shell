[CmdletBinding()]
param(
    [string]$Port = "COM12",
    [int]$Baud = 115200,
    [int]$DurationSeconds = 30,
    [string]$OutputPath
)

$ErrorActionPreference = "Stop"

if (-not $OutputPath) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputPath = Join-Path $PSScriptRoot "..\runtime-logs\hardware-$stamp.log"
}

$resolvedOutput = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $resolvedOutput
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null

$serial = [System.IO.Ports.SerialPort]::new(
    $Port,
    $Baud,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$serial.ReadTimeout = 100
$serial.WriteTimeout = 100
$serial.NewLine = "`n"
$captured = [System.Text.StringBuilder]::new()

try {
    $serial.Open()

    # ESP32-S3 USB Serial/JTAG: keep GPIO0 released and pulse EN for a fresh boot.
    $serial.DtrEnable = $false
    $serial.RtsEnable = $true
    Start-Sleep -Milliseconds 120
    $serial.RtsEnable = $false

    $deadline = (Get-Date).AddSeconds($DurationSeconds)
    while ((Get-Date) -lt $deadline) {
        $chunk = $serial.ReadExisting()
        if ($chunk.Length -gt 0) {
            [void]$captured.Append($chunk)
            [Console]::Write($chunk)
        }
        Start-Sleep -Milliseconds 40
    }
}
finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    $serial.Dispose()
}

[System.IO.File]::WriteAllText($resolvedOutput, $captured.ToString(), [System.Text.UTF8Encoding]::new($false))
Write-Host "`nSaved serial capture: $resolvedOutput"
