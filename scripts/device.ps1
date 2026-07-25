[CmdletBinding()]
# Route the common build, flash, and monitor workflows through one project-aware entrypoint.
param(
    [Parameter(Position = 0)]
    [ValidateSet("build", "clean", "fullclean", "flash", "monitor", "capture", "erase", "reconfigure", "set-target", "size", "combo", "ports", "recover")]
    [string]$Action = "build",

    [string]$Port = "COM8",

    [int]$Baud = 115200,

    [string]$BuildDir = "build",

    [int]$WaitSeconds = 30,

    [ValidateRange(1, 86400)]
    [int]$DurationSeconds = 30,

    [string]$OutputPath,

    # Explicit parameters take precedence over the dedicated QSEE_IDF_*
    # environment variables and the workstation defaults below.
    [string]$IdfPath,

    [string]$ToolsPath,

    [string]$PythonExe,

    # The workstation has 16 cores and 32 GB RAM; allow overriding for CI or
    # constrained machines while keeping the local default fully parallel.
    [int]$Jobs = 0
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
[System.Console]::InputEncoding = [System.Text.Encoding]::UTF8
[System.Environment]::SetEnvironmentVariable("PYTHONUTF8", "1", "Process")
[System.Environment]::SetEnvironmentVariable("PYTHONIOENCODING", "utf-8", "Process")

$ProjectRoot = Split-Path -Parent $PSScriptRoot

if (-not $IdfPath) {
    $IdfPath = if ($env:QSEE_IDF_PATH) {
        $env:QSEE_IDF_PATH
    } else {
        "E:\Espressif\.espressif\v6.0.2\esp-idf"
    }
}
if (-not $ToolsPath) {
    $ToolsPath = if ($env:QSEE_IDF_TOOLS_PATH) {
        $env:QSEE_IDF_TOOLS_PATH
    } else {
        "E:\Espressif"
    }
}

if (-not (Test-Path -LiteralPath $IdfPath -PathType Container)) {
    throw "ESP-IDF non trovato in $IdfPath"
}
if (-not (Test-Path -LiteralPath $ToolsPath -PathType Container)) {
    throw "Root degli strumenti Espressif non trovata in $ToolsPath"
}

$IdfPath = (Resolve-Path -LiteralPath $IdfPath).Path
$ToolsPath = (Resolve-Path -LiteralPath $ToolsPath).Path
$IdfPy = Join-Path $IdfPath "tools\idf.py"
$IdfToolsPy = Join-Path $IdfPath "tools\idf_tools.py"
foreach ($requiredIdfFile in @($IdfPy, $IdfToolsPy)) {
    if (-not (Test-Path -LiteralPath $requiredIdfFile -PathType Leaf)) {
        throw "Installazione ESP-IDF incompleta: file non trovato $requiredIdfFile"
    }
}

if (-not $PythonExe) {
    $idfInstallName = Split-Path -Leaf (Split-Path -Parent $IdfPath)
    $PythonExe = Join-Path $ToolsPath "tools\python\$idfInstallName\venv\Scripts\python.exe"
    if ($env:QSEE_IDF_PYTHON) {
        $PythonExe = $env:QSEE_IDF_PYTHON
    }
}
if (-not (Test-Path -LiteralPath $PythonExe -PathType Leaf)) {
    throw "Python ESP-IDF non trovato in $PythonExe"
}
$PythonExe = (Resolve-Path -LiteralPath $PythonExe).Path
$PythonEnvPath = Split-Path -Parent (Split-Path -Parent $PythonExe)

$env:IDF_PATH = $IdfPath
$env:IDF_TOOLS_PATH = $ToolsPath
$env:IDF_PYTHON_ENV_PATH = $PythonEnvPath

# Ask the selected ESP-IDF checkout for its exact supported tool paths instead
# of duplicating CMake, Ninja, debugger, and compiler versions in this script.
$toolExports = & $PythonExe $IdfToolsPy export --format key-value
if ($LASTEXITCODE -ne 0) {
    throw "Impossibile esportare la toolchain ESP-IDF da $IdfPath"
}
foreach ($line in $toolExports) {
    if ($line -notmatch '^([A-Z][A-Z0-9_]*)=(.*)$') {
        continue
    }
    $name = $Matches[1]
    $value = $Matches[2]
    if ($name -eq "PATH") {
        $value = $value.Replace("%PATH%", $env:PATH)
    }
    [System.Environment]::SetEnvironmentVariable($name, $value, "Process")
}

# Parallel ccache processes intermittently exit without diagnostics on this
# Windows workstation; deterministic firmware builds are more valuable here.
$env:IDF_CCACHE_ENABLE = "0"

function Get-ToolCommand([string]$Name) {
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -eq $command) {
        throw "Strumento ESP-IDF richiesto non trovato nel PATH: $Name"
    }
    return $command
}

$CMakeCommand = Get-ToolCommand "cmake"
$NinjaCommand = Get-ToolCommand "ninja"
$ToolchainCommand = Get-ToolCommand "xtensa-esp32s3-elf-gcc"
$IdfVersion = (& $PythonExe $IdfPy --version | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Impossibile determinare la versione ESP-IDF"
}
$PythonVersion = (& $PythonExe --version | Out-String).Trim()
if ($IdfVersion -notmatch '^ESP-IDF v6\.0\.2(?:\s|$)') {
    throw "Versione ESP-IDF non supportata: '$IdfVersion'. Il repository richiede ESP-IDF v6.0.2."
}
if ($PythonVersion -ne "Python 3.13.7") {
    throw "Versione Python non supportata: '$PythonVersion'. Il repository richiede Python 3.13.7."
}
$CMakeVersion = (& $CMakeCommand.Source --version | Select-Object -First 1)
$NinjaVersion = (& $NinjaCommand.Source --version | Select-Object -First 1)
$ToolchainVersion = (& $ToolchainCommand.Source --version | Select-Object -First 1)
Write-Host "ESP-IDF: $IdfVersion ($IdfPath)"
Write-Host "Python: $PythonVersion ($PythonExe)"
Write-Host "CMake: $CMakeVersion ($($CMakeCommand.Source))"
Write-Host "Ninja: $NinjaVersion ($($NinjaCommand.Source))"
Write-Host "Toolchain: $ToolchainVersion ($($ToolchainCommand.Source))"

$MonitorScript = Join-Path $PSScriptRoot "device.ps1"
$CaptureScript = Join-Path $PSScriptRoot "capture-serial.ps1"

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
        [regex]::new([regex]::Escape("idf.py -B $BuildDir -p $normalizedRequestedPort monitor")),
        [regex]::new([regex]::Escape("idf_monitor.py -p $normalizedRequestedPort")),
        [regex]::new([regex]::Escape("esp_idf_monitor -p $normalizedRequestedPort")),
        [regex]::new("(?i)pyserial.*$normalizedRequestedPort")
    )

    $genericMonitorPattern = [regex]::new(
        "(?i)(device\.ps1.*(?:monitor|capture).*-Port\s+$normalizedRequestedPort|capture-serial\.ps1.*-Port\s+$normalizedRequestedPort|idf\.py.*-p\s+$normalizedRequestedPort.*monitor|idf_monitor\.py.*-p\s+$normalizedRequestedPort|esp_idf_monitor.*-p\s+$normalizedRequestedPort)"
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

function Wait-ForExclusiveSerialPort([string]$RequestedPort, [int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $lastError = $null

    while ((Get-Date) -lt $deadline) {
        $probe = [System.IO.Ports.SerialPort]::new($RequestedPort, 115200)
        try {
            $probe.Open()
            $probe.Close()
            $probe.Dispose()
            Start-Sleep -Milliseconds 500
            Write-Host "Serial port $RequestedPort is released and ready."
            return
        } catch {
            $lastError = $_.Exception.Message
            $probe.Dispose()
            Stop-StaleMonitorProcesses $RequestedPort
            Start-Sleep -Milliseconds 500
        }
    }

    throw "Serial port $RequestedPort stayed busy for $TimeoutSeconds seconds. Last error: $lastError"
}

function Invoke-ProjectBuild([string]$RequestedBuildDir) {
    $buildNinja = Join-Path $RequestedBuildDir "build.ninja"
    $reconfigure = -not (Test-Path $buildNinja)

    if (-not $reconfigure) {
        $trackedBuildInputs = @(
            Get-ChildItem -Path $ProjectRoot -Recurse -File -ErrorAction SilentlyContinue |
                Where-Object {
                    $_.FullName -notlike "$(Join-Path $ProjectRoot $RequestedBuildDir)\*" -and
                    ($_.Name -eq "CMakeLists.txt" -or $_.Name -eq "sdkconfig" -or $_.Name -eq "sdkconfig.defaults")
                }
        )
        $latestInput = $trackedBuildInputs |
            Sort-Object LastWriteTimeUtc -Descending |
            Select-Object -First 1
        $reconfigure = $null -ne $latestInput -and
            $latestInput.LastWriteTimeUtc -gt (Get-Item $buildNinja).LastWriteTimeUtc
    }

    if ($reconfigure) {
        Write-Host "Build configuration changed; running ESP-IDF reconfigure."
        & $PythonExe $IdfPy -B $RequestedBuildDir reconfigure
        if ($LASTEXITCODE -ne 0) {
            throw "Reconfigure failed"
        }
    } else {
        Write-Host "Using incremental build; skipping reconfigure."
    }

    $parallelJobs = if ($Jobs -gt 0) {
        $Jobs
    } else {
        [Math]::Max(2, [Math]::Min(16, [Environment]::ProcessorCount))
    }
    Write-Host "Building with Ninja parallelism: -j $parallelJobs"
    & ninja -C $RequestedBuildDir -j $parallelJobs all
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
            if ($LASTEXITCODE -ne 0) {
                throw "Clean failed"
            }
        }
        "fullclean" {
            & $PythonExe $IdfPy @commonArgs fullclean
            if ($LASTEXITCODE -ne 0) {
                throw "Full clean failed"
            }
        }
        "reconfigure" {
            & $PythonExe $IdfPy @commonArgs reconfigure
            if ($LASTEXITCODE -ne 0) {
                throw "Reconfigure failed"
            }
        }
        "set-target" {
            & $PythonExe $IdfPy @commonArgs set-target esp32s3
            if ($LASTEXITCODE -ne 0) {
                throw "Set target failed"
            }
        }
        "size" {
            & $PythonExe $IdfPy @commonArgs size
            if ($LASTEXITCODE -ne 0) {
                throw "Size report failed"
            }
        }
        "ports" {
            Show-SerialPorts
        }
        "erase" {
            if (-not $Port) {
                throw "Specify the serial port with -Port, for example -Port COM5"
            }
            Stop-StaleMonitorProcesses $Port
            Wait-ForExclusiveSerialPort $Port $WaitSeconds
            $Port = Resolve-Port $Port
            & $PythonExe $IdfPy @commonArgs -p $Port -b $Baud erase-flash
            if ($LASTEXITCODE -ne 0) {
                throw "Erase failed"
            }
        }
        "flash" {
            if (-not $Port) {
                throw "Specify the serial port with -Port, for example -Port COM5"
            }
            Stop-StaleMonitorProcesses $Port
            Wait-ForExclusiveSerialPort $Port $WaitSeconds
            $Port = Resolve-Port $Port
            & $PythonExe $IdfPy @commonArgs -p $Port -b $Baud flash
            if ($LASTEXITCODE -ne 0) {
                throw "Flash failed"
            }
        }
        "monitor" {
            if (-not $Port) {
                throw "Specify the serial port with -Port, for example -Port COM5"
            }
            Stop-StaleMonitorProcesses $Port
            Wait-ForExclusiveSerialPort $Port $WaitSeconds
            $Port = Resolve-Port $Port
            & $PythonExe $IdfPy @commonArgs -p $Port monitor
            if ($LASTEXITCODE -ne 0) {
                throw "Monitor failed"
            }
        }
        "capture" {
            if (-not $Port) {
                throw "Specify the serial port with -Port, for example -Port COM5"
            }
            Stop-StaleMonitorProcesses $Port
            Wait-ForExclusiveSerialPort $Port $WaitSeconds
            $Port = Resolve-Port $Port
            $captureArgs = @(
                "-NoProfile",
                "-File", $CaptureScript,
                "-Port", $Port,
                "-Baud", $Baud,
                "-DurationSeconds", $DurationSeconds
            )
            if ($OutputPath) {
                $captureArgs += @("-OutputPath", $OutputPath)
            }
            & pwsh @captureArgs
            if ($LASTEXITCODE -ne 0) {
                throw "Serial capture failed"
            }
        }
        "combo" {
            Invoke-ProjectBuild $BuildDir

            Stop-StaleMonitorProcesses $Port
            Wait-ForExclusiveSerialPort $Port $WaitSeconds
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
                "-BuildDir", $BuildDir,
                "-IdfPath", "`"$IdfPath`"",
                "-ToolsPath", "`"$ToolsPath`"",
                "-PythonExe", "`"$PythonExe`""
            )

            Start-Process -FilePath "pwsh.exe" -ArgumentList $monitorArgs -WorkingDirectory $ProjectRoot
        }
        "recover" {
            Invoke-ProjectBuild $BuildDir

            Stop-StaleMonitorProcesses $Port
            $Port = Wait-ForPort $Port $WaitSeconds
            Wait-ForExclusiveSerialPort $Port $WaitSeconds
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
