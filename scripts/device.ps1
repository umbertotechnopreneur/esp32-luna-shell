[CmdletBinding()]
# Route the common build, flash, and monitor workflows through one project-aware entrypoint.
param(
    [Parameter(Position = 0)]
    [ValidateSet("build", "clean", "fullclean", "flash", "monitor", "erase", "reconfigure", "set-target", "combo", "ports", "recover")]
    [string]$Action = "build",

    [string]$Port = "COM8",

    [int]$Baud = 115200,

    [string]$BuildDir = "build",

    [int]$WaitSeconds = 30
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
[System.Console]::InputEncoding = [System.Text.Encoding]::UTF8
[System.Environment]::SetEnvironmentVariable("PYTHONUTF8", "1", "Process")
[System.Environment]::SetEnvironmentVariable("PYTHONIOENCODING", "utf-8", "Process")

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$IdfPath = "E:\Espressif\.espressif\v5.5.4\esp-idf"
$ToolsPath = "E:\Espressif"
$PythonExe = "E:\Espressif\tools\python\v5.5.4\venv\Scripts\python.exe"

if (-not (Test-Path $IdfPath)) {
    throw "ESP-IDF non trovato in $IdfPath"
}

if (-not (Test-Path $PythonExe)) {
    throw "Python ESP-IDF non trovato in $PythonExe"
}

$env:IDF_PATH = $IdfPath
$env:IDF_TOOLS_PATH = $ToolsPath
$env:IDF_PYTHON_ENV_PATH = "E:\Espressif\tools\python\v5.5.4\venv"
$env:ESP_IDF_VERSION = "5.5.4"
$env:OPENOCD_SCRIPTS = "E:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20251215\openocd-esp32\share\openocd\scripts"
$env:ESP_ROM_ELF_DIR = "E:\Espressif\tools\esp-rom-elfs\20241011"
# Parallel ccache processes intermittently exit without diagnostics on this
# Windows workstation; deterministic firmware builds are more valuable here.
$env:IDF_CCACHE_ENABLE = "0"
$env:IDF_PYTHON_CHECK_CONSTRAINTS = "no"

$toolBins = @(
    "E:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64",
    "E:\Espressif\tools\cmake\3.30.2\bin",
    "E:\Espressif\tools\dfu-util\0.11\dfu-util-0.11-win64",
    "E:\Espressif\tools\esp-rom-elfs\20241011",
    "E:\Espressif\tools\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin",
    "E:\Espressif\tools\idf-exe\1.0.3",
    "E:\Espressif\tools\ninja\1.12.1",
    "E:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20251215\openocd-esp32\bin",
    "E:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin",
    "E:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin",
    "E:\Espressif\tools\xtensa-esp-elf-gdb\16.3_20250913\xtensa-esp-elf-gdb\bin",
    "E:\Espressif\tools\python\v5.5.4\venv\Scripts"
)

$existingPath = @()
if ($env:PATH) {
    $existingPath = $env:PATH -split ";"
}

$env:PATH = (($toolBins + $existingPath) | Where-Object { $_ } | Select-Object -Unique) -join ";"
$IdfPy = Join-Path $IdfPath "tools\idf.py"
$MonitorScript = Join-Path $PSScriptRoot "device.ps1"

function Get-SerialPorts {
    $openablePorts = @([System.IO.Ports.SerialPort]::GetPortNames() | ForEach-Object { $_.ToUpperInvariant() })

    $ports = Get-PnpDevice -Class Ports -ErrorAction SilentlyContinue |
        Where-Object { $_.FriendlyName -match '\(COM\d+\)' } |
        ForEach-Object {
            $deviceId = $null
            if ($_.InstanceId -match '\((COM\d+)\)') {
                $deviceId = $Matches[1].ToUpperInvariant()
            } elseif ($_.FriendlyName -match '\((COM\d+)\)') {
                $deviceId = $Matches[1].ToUpperInvariant()
            }

            [PSCustomObject]@{
                DeviceID = $deviceId
                FriendlyName = $_.FriendlyName
                InstanceId = $_.InstanceId
                Status = $_.Status
                IsEspressif = $_.InstanceId -match 'VID_303A'
                IsOpenable = $openablePorts -contains $deviceId
            }
        } |
        Where-Object { $_.DeviceID } |
        Sort-Object DeviceID

    return @($ports)
}

function Show-SerialPorts {
    $ports = Get-SerialPorts

    if ($ports.Count -eq 0) {
        Write-Host "No serial ports detected."
        return
    }

    Write-Host "Detected serial ports:"
    $ports | Format-Table DeviceID, FriendlyName, Status, IsOpenable -AutoSize
}

function Resolve-Port([string]$RequestedPort) {
    $ports = Get-SerialPorts
    $normalizedRequestedPort = $RequestedPort.ToUpperInvariant()

    if (($ports | Where-Object { $_.DeviceID -eq $normalizedRequestedPort -and $_.IsOpenable }).Count -gt 0) {
        return $normalizedRequestedPort
    }

    if (($ports | Where-Object { $_.DeviceID -eq $normalizedRequestedPort }).Count -gt 0) {
        Write-Host "Requested port $normalizedRequestedPort is listed by PnP but is not currently openable."
    } else {
        Write-Host "Requested port $normalizedRequestedPort was not found."
    }
    Show-SerialPorts
    throw "No usable serial port found. Windows sees no openable COM port for the device right now."
}

function Wait-ForPort([string]$RequestedPort, [int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $normalizedRequestedPort = $RequestedPort.ToUpperInvariant()

    Write-Host "Waiting for an openable ESP serial port for up to $TimeoutSeconds seconds..."

    while ((Get-Date) -lt $deadline) {
        $ports = Get-SerialPorts
        $requested = @($ports | Where-Object { $_.DeviceID -eq $normalizedRequestedPort -and $_.IsOpenable })
        if ($requested.Count -gt 0) {
            Write-Host "Using requested port $normalizedRequestedPort."
            return $normalizedRequestedPort
        }

        Start-Sleep -Milliseconds 750
    }

    Write-Host "No openable ESP serial port became available."
    Show-SerialPorts
    throw "Timed out waiting for an openable ESP serial port."
}

function Stop-StaleMonitorProcesses([string]$RequestedPort) {
    $normalizedRequestedPort = $RequestedPort.ToUpperInvariant()
    $patterns = @(
        [regex]::new([regex]::Escape("device.ps1 monitor")),
        [regex]::new([regex]::Escape("idf.py -B $BuildDir -p $normalizedRequestedPort monitor")),
        [regex]::new([regex]::Escape("idf_monitor.py -p $normalizedRequestedPort")),
        [regex]::new([regex]::Escape("esp_idf_monitor -p $normalizedRequestedPort"))
    )

    $genericMonitorPattern = [regex]::new(
        "(?i)(device\.ps1.*monitor.*-Port\s+$normalizedRequestedPort|idf\.py.*-p\s+$normalizedRequestedPort.*monitor|idf_monitor\.py.*-p\s+$normalizedRequestedPort|esp_idf_monitor.*-p\s+$normalizedRequestedPort)"
    )

    $toStop = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
        Where-Object {
            $_.ProcessId -ne $PID -and
            $_.CommandLine -and
            ($genericMonitorPattern.IsMatch($_.CommandLine) -or
             @($patterns | Where-Object { $_.IsMatch($_.CommandLine) }).Count -gt 0)
        } |
        Sort-Object ProcessId -Descending

    foreach ($process in $toStop) {
        try {
            Stop-Process -Id $process.ProcessId -Force -ErrorAction Stop
            Write-Host "Stopped stale monitor process $($process.ProcessId) ($($process.Name))."
        } catch {
            Write-Warning "Could not stop stale monitor process $($process.ProcessId): $($_.Exception.Message)"
        }
    }
}

function Invoke-ProjectBuild([string]$RequestedBuildDir) {
    & $PythonExe $IdfPy -B $RequestedBuildDir reconfigure
    if ($LASTEXITCODE -ne 0) {
        throw "Reconfigure failed"
    }

    # Keep the Windows cross-compiler process count bounded and deterministic.
    & ninja -C $RequestedBuildDir -j 2 all
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed"
    }
}

Push-Location $ProjectRoot
try {
    $commonArgs = @("-B", $BuildDir)

    switch ($Action) {
        "build" {
            Invoke-ProjectBuild $BuildDir
        }
        "clean" {
            & $PythonExe $IdfPy @commonArgs clean
        }
        "fullclean" {
            & $PythonExe $IdfPy @commonArgs fullclean
        }
        "reconfigure" {
            & $PythonExe $IdfPy @commonArgs reconfigure
        }
        "set-target" {
            & $PythonExe $IdfPy @commonArgs set-target esp32s3
        }
        "ports" {
            Show-SerialPorts
        }
        "erase" {
            if (-not $Port) {
                throw "Specify the serial port with -Port, for example -Port COM5"
            }
            $Port = Resolve-Port $Port
            Stop-StaleMonitorProcesses $Port
            & $PythonExe $IdfPy @commonArgs -p $Port -b $Baud erase-flash
        }
        "flash" {
            if (-not $Port) {
                throw "Specify the serial port with -Port, for example -Port COM5"
            }
            $Port = Resolve-Port $Port
            Stop-StaleMonitorProcesses $Port
            & $PythonExe $IdfPy @commonArgs -p $Port -b $Baud flash
        }
        "monitor" {
            if (-not $Port) {
                throw "Specify the serial port with -Port, for example -Port COM5"
            }
            $Port = Resolve-Port $Port
            & $PythonExe $IdfPy @commonArgs -p $Port monitor
        }
        "combo" {
            Invoke-ProjectBuild $BuildDir

            $Port = Resolve-Port $Port
            & $PythonExe $IdfPy @commonArgs -p $Port -b $Baud flash
            if ($LASTEXITCODE -ne 0) {
                throw "Flash failed"
            }

            $monitorArgs = @(
                "-NoExit",
                "-NoProfile",
                "-ExecutionPolicy", "Bypass",
                "-File", "`"$MonitorScript`"",
                "monitor",
                "-Port", $Port,
                "-BuildDir", $BuildDir
            )

            Start-Process -FilePath "pwsh.exe" -ArgumentList $monitorArgs -WorkingDirectory $ProjectRoot
        }
        "recover" {
            Invoke-ProjectBuild $BuildDir

            $Port = Wait-ForPort $Port $WaitSeconds
            Stop-StaleMonitorProcesses $Port
            & $PythonExe $IdfPy @commonArgs -p $Port -b $Baud flash
            if ($LASTEXITCODE -ne 0) {
                throw "Flash failed"
            }
        }
        default {
            throw "Unsupported action: $Action"
        }
    }
}
finally {
    Pop-Location
}
