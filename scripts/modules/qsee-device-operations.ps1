<#
.SYNOPSIS
Passive firmware and serial operations used by the QSee.ai script entrypoint.

.DESCRIPTION
Defines bounded build, flash, monitor, serial-capture, and port-discovery
functions. Importing this file does not touch the toolchain or any device.
#>

function Initialize-QSeeIdfEnvironment {
    [CmdletBinding()]
    param([Parameter(Mandatory)][object]$Settings)

    $env:IDF_PATH = $Settings.IdfPath
    $env:IDF_TOOLS_PATH = $Settings.IdfToolsPath
    $env:IDF_PYTHON_ENV_PATH = $Settings.IdfPythonEnvPath
    $env:ESP_IDF_VERSION = '5.5.4'
    $env:OPENOCD_SCRIPTS = $Settings.OpenOcdScripts
    $env:ESP_ROM_ELF_DIR = $Settings.EspRomElfDir
    $env:IDF_CCACHE_ENABLE = '0'
    $env:IDF_PYTHON_CHECK_CONSTRAINTS = 'no'

    $toolBins = @(
        (Join-Path $Settings.IdfToolsPath 'tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64'),
        (Join-Path $Settings.IdfToolsPath 'tools\cmake\3.30.2\bin'),
        (Join-Path $Settings.IdfToolsPath 'tools\dfu-util\0.11\dfu-util-0.11-win64'),
        (Join-Path $Settings.IdfToolsPath 'tools\esp-rom-elfs\20241011'),
        (Join-Path $Settings.IdfToolsPath 'tools\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin'),
        (Join-Path $Settings.IdfToolsPath 'tools\idf-exe\1.0.3'),
        (Join-Path $Settings.IdfToolsPath 'tools\ninja\1.12.1'),
        (Join-Path $Settings.IdfToolsPath 'tools\openocd-esp32\v0.12.0-esp32-20251215\openocd-esp32\bin'),
        (Join-Path $Settings.IdfToolsPath 'tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin'),
        (Join-Path $Settings.IdfToolsPath 'tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin'),
        (Join-Path $Settings.IdfToolsPath 'tools\xtensa-esp-elf-gdb\16.3_20250913\xtensa-esp-elf-gdb\bin'),
        (Join-Path $Settings.IdfPythonEnvPath 'Scripts')
    )
    $existingPath = if ($env:PATH) { @($env:PATH -split ';') } else { @() }
    $env:PATH = (($toolBins + $existingPath) | Where-Object { $_ } | Select-Object -Unique) -join ';'

    Write-QSeeLog -Message "Initialized ESP-IDF environment from $($Settings.IdfPath)" -Level DEBUG
}

function Get-QSeeIdfCommand {
    [CmdletBinding()]
    param([Parameter(Mandatory)][object]$Settings)

    return [pscustomobject]@{
        Python = Join-Path $Settings.IdfPythonEnvPath 'Scripts\python.exe'
        IdfPy = Join-Path $Settings.IdfPath 'tools\idf.py'
    }
}

function Get-QSeeSerialPorts {
    [CmdletBinding()]
    param()

    $openablePorts = @(
        [System.IO.Ports.SerialPort]::GetPortNames() |
            ForEach-Object { $_.ToUpperInvariant() }
    )
    $ports = Get-PnpDevice -Class Ports -ErrorAction SilentlyContinue |
        Where-Object { $_.FriendlyName -match '\(COM\d+\)' } |
        ForEach-Object {
            $deviceId = if ($_.FriendlyName -match '\((COM\d+)\)') {
                $Matches[1].ToUpperInvariant()
            }
            else {
                $null
            }

            [pscustomobject]@{
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

function Show-QSeeSerialPorts {
    [CmdletBinding()]
    param()

    $ports = Get-QSeeSerialPorts
    if ($ports.Count -eq 0) {
        Write-QSeeWarning 'No serial ports detected.'
        return @()
    }

    Write-Host 'Detected serial ports:'
    $table = $ports |
        Format-Table DeviceID, FriendlyName, Status, IsOpenable -AutoSize |
        Out-String
    Write-Host $table.TrimEnd()
    foreach ($portInfo in $ports) {
        Write-QSeeLog -Message (
            "Port $($portInfo.DeviceID): status=$($portInfo.Status) " +
            "openable=$($portInfo.IsOpenable) espressif=$($portInfo.IsEspressif)"
        ) -Level DEBUG
    }
    return $ports
}

# Normalize a user-confirmed port without enumerating or opening the USB bridge.
function Resolve-QSeeSerialPort {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$RequestedPort)

    $normalized = ConvertTo-QSeeSerialPortName -Port $RequestedPort
    Write-QSeeInfo "Using requested serial port $normalized without a USB preflight probe."
    return $normalized
}

# Find only monitor processes tied to the requested port or build invocation.
function Get-QSeeStaleMonitorProcesses {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RequestedPort,
        [Parameter(Mandatory)][string]$BuildDir
    )

    $normalized = ConvertTo-QSeeSerialPortName -Port $RequestedPort
    $escapedPort = [regex]::Escape($normalized)
    $portToken = "$escapedPort(?!\d)"
    $genericPattern = [regex]::new(
        "(?i)(device\.ps1.*monitor.*-Port\s+$portToken|" +
        "capture-serial\.ps1.*-Port\s+$portToken|" +
        "idf\.py.*-p\s+$portToken.*monitor|" +
        "idf_monitor\.py.*-p\s+$portToken|" +
        "esp_idf_monitor.*-p\s+$portToken|" +
        "pyserial.*$portToken)"
    )
    $buildPattern = [regex]::new(
        [regex]::Escape("idf.py -B $BuildDir -p $normalized monitor"),
        [Text.RegularExpressions.RegexOptions]::IgnoreCase
    )

    $processes = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
        Where-Object {
            $_.ProcessId -ne $PID -and
            $_.CommandLine -and
            ($genericPattern.IsMatch($_.CommandLine) -or $buildPattern.IsMatch($_.CommandLine))
        } |
        Sort-Object ProcessId -Descending

    return @($processes)
}

# Report conflicting monitors by default and stop them only after explicit opt-in.
function Resolve-QSeeStaleMonitorProcesses {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RequestedPort,
        [Parameter(Mandatory)][string]$BuildDir,
        [switch]$Stop
    )

    $normalized = ConvertTo-QSeeSerialPortName -Port $RequestedPort
    $processes = @(
        Get-QSeeStaleMonitorProcesses -RequestedPort $normalized -BuildDir $BuildDir
    )
    if ($processes.Count -eq 0) {
        return
    }

    foreach ($process in $processes) {
        Write-QSeeWarning (
            "Conflicting monitor process $($process.ProcessId) " +
            "($($process.Name)) is using $normalized."
        )
    }
    if (-not $Stop) {
        throw (
            "A monitor process is already associated with $normalized. " +
            'Close it manually or re-run with -StopStaleMonitors.'
        )
    }

    foreach ($process in $processes) {
        try {
            Stop-Process -Id $process.ProcessId -Force -ErrorAction Stop
            Write-QSeeWarning "Stopped stale monitor process $($process.ProcessId) ($($process.Name))."
        }
        catch {
            Write-QSeeWarning "Could not stop stale monitor process $($process.ProcessId): $($_.Exception.Message)"
        }
    }
}

# Reconfigure stale CMake metadata, then run the bounded parallel Ninja build.
function Invoke-QSeeProjectBuild {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$ProjectRoot,
        [Parameter(Mandatory)][object]$Settings
    )

    $buildPath = if ([System.IO.Path]::IsPathRooted($Settings.BuildDir)) {
        [System.IO.Path]::GetFullPath($Settings.BuildDir)
    }
    else {
        [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot $Settings.BuildDir))
    }
    $buildNinja = Join-Path $buildPath 'build.ninja'
    $reconfigure = -not (Test-Path -LiteralPath $buildNinja -PathType Leaf)

    if (-not $reconfigure) {
        # CMake inputs newer than Ninja metadata require one IDF reconfigure.
        $latestInput = Get-ChildItem -LiteralPath $ProjectRoot -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object {
                -not $_.FullName.StartsWith($buildPath, [StringComparison]::OrdinalIgnoreCase) -and
                $_.Name -in @('CMakeLists.txt', 'sdkconfig', 'sdkconfig.defaults')
            } |
            Sort-Object LastWriteTimeUtc -Descending |
            Select-Object -First 1
        $reconfigure = $null -ne $latestInput -and
            $latestInput.LastWriteTimeUtc -gt (Get-Item -LiteralPath $buildNinja).LastWriteTimeUtc
    }

    $idf = Get-QSeeIdfCommand -Settings $Settings
    if ($reconfigure) {
        Write-QSeeInfo 'Build configuration changed; running ESP-IDF reconfigure.'
        Invoke-QSeeNativeCommand -FilePath $idf.Python `
            -ArgumentList @($idf.IdfPy, '-B', $Settings.BuildDir, 'reconfigure') `
            -FailureMessage 'Reconfigure failed'
    }
    else {
        Write-QSeeInfo 'Using incremental build; skipping reconfigure.'
    }

    $parallelJobs = if ($Settings.Jobs -gt 0) {
        $Settings.Jobs
    }
    else {
        [Math]::Max(2, [Math]::Min(16, [Environment]::ProcessorCount))
    }
    Write-QSeeInfo "Building with Ninja parallelism: -j $parallelJobs"
    Invoke-QSeeNativeCommand -FilePath 'ninja' `
        -ArgumentList @('-C', $Settings.BuildDir, '-j', [string]$parallelJobs, 'all') `
        -FailureMessage 'Build failed'
}

function Invoke-QSeeSerialCapture {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Port,
        [Parameter(Mandatory)][int]$Baud,
        [Parameter(Mandatory)][int]$DurationSeconds,
        [Parameter(Mandatory)][string]$OutputPath,
        [switch]$NoReset
    )

    $Port = ConvertTo-QSeeSerialPortName -Port $Port
    $resolvedOutput = [System.IO.Path]::GetFullPath($OutputPath)
    [System.IO.Directory]::CreateDirectory((Split-Path -Parent $resolvedOutput)) | Out-Null
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
    if ($NoReset) {
        # Set idle control-line levels before Open so the driver never pulses EN.
        $serial.DtrEnable = $false
        $serial.RtsEnable = $false
    }
    $captured = [System.Text.StringBuilder]::new()

    try {
        $serial.Open()
        if (-not $NoReset) {
            # Keep GPIO0 released and pulse EN for one bounded fresh-boot capture.
            $serial.DtrEnable = $false
            $serial.RtsEnable = $true
            Start-Sleep -Milliseconds 120
            $serial.RtsEnable = $false
        }

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
            try {
                $serial.DtrEnable = $false
                $serial.RtsEnable = $false
            }
            catch {
                Write-QSeeWarning "Serial control lines unavailable during capture cleanup: $($_.Exception.Message)"
            }
            try {
                $serial.Close()
            }
            catch {
                Write-QSeeWarning "Serial port close failed during capture cleanup: $($_.Exception.Message)"
            }
        }
        $serial.Dispose()
        [System.IO.File]::WriteAllText(
            $resolvedOutput,
            $captured.ToString(),
            [System.Text.UTF8Encoding]::new($false)
        )
        Start-Sleep -Milliseconds 500
    }

    Write-QSeeSuccess "Saved serial capture: $resolvedOutput"
    return $resolvedOutput
}

# Dispatch one firmware action with the project root as working directory.
function Invoke-QSeeFirmwareAction {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Action,
        [Parameter(Mandatory)][string]$ProjectRoot,
        [Parameter(Mandatory)][string]$ScriptsRoot,
        [Parameter(Mandatory)][object]$Settings,
        [string]$OutputPath,
        [switch]$StopStaleMonitors,
        [switch]$NoReset
    )

    $idf = Get-QSeeIdfCommand -Settings $Settings
    $commonArgs = @($idf.IdfPy, '-B', $Settings.BuildDir)
    $port = ConvertTo-QSeeSerialPortName -Port $Settings.Port

    Push-Location $ProjectRoot
    try {
        switch ($Action) {
            'build' {
                Invoke-QSeeProjectBuild -ProjectRoot $ProjectRoot -Settings $Settings
            }
            'clean' {
                Invoke-QSeeNativeCommand -FilePath $idf.Python `
                    -ArgumentList ($commonArgs + 'clean') -FailureMessage 'Clean failed'
            }
            'fullclean' {
                Invoke-QSeeNativeCommand -FilePath $idf.Python `
                    -ArgumentList ($commonArgs + 'fullclean') -FailureMessage 'Full clean failed'
            }
            'reconfigure' {
                Invoke-QSeeNativeCommand -FilePath $idf.Python `
                    -ArgumentList ($commonArgs + 'reconfigure') -FailureMessage 'Reconfigure failed'
            }
            'set-target' {
                Invoke-QSeeNativeCommand -FilePath $idf.Python `
                    -ArgumentList ($commonArgs + @('set-target', 'esp32s3')) `
                    -FailureMessage 'Target selection failed'
            }
            'ports' {
                Show-QSeeSerialPorts | Out-Null
            }
            'erase' {
                Resolve-QSeeStaleMonitorProcesses -RequestedPort $port `
                    -BuildDir $Settings.BuildDir -Stop:$StopStaleMonitors
                $port = Resolve-QSeeSerialPort -RequestedPort $port
                Invoke-QSeeNativeCommand -FilePath $idf.Python `
                    -ArgumentList ($commonArgs + @('-p', $port, '-b', [string]$Settings.Baud, 'erase-flash')) `
                    -FailureMessage 'Erase failed'
            }
            'flash' {
                Resolve-QSeeStaleMonitorProcesses -RequestedPort $port `
                    -BuildDir $Settings.BuildDir -Stop:$StopStaleMonitors
                $port = Resolve-QSeeSerialPort -RequestedPort $port
                Invoke-QSeeNativeCommand -FilePath $idf.Python `
                    -ArgumentList ($commonArgs + @('-p', $port, '-b', [string]$Settings.Baud, 'flash')) `
                    -FailureMessage 'Flash failed'
            }
            'monitor' {
                Resolve-QSeeStaleMonitorProcesses -RequestedPort $port `
                    -BuildDir $Settings.BuildDir -Stop:$StopStaleMonitors
                $port = Resolve-QSeeSerialPort -RequestedPort $port
                Invoke-QSeeNativeCommand -FilePath $idf.Python `
                    -ArgumentList ($commonArgs + @('-p', $port, 'monitor')) `
                    -FailureMessage 'Monitor failed'
            }
            'capture' {
                Resolve-QSeeStaleMonitorProcesses -RequestedPort $port `
                    -BuildDir $Settings.BuildDir -Stop:$StopStaleMonitors
                $port = Resolve-QSeeSerialPort -RequestedPort $port
                if ([string]::IsNullOrWhiteSpace($OutputPath)) {
                    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
                    $OutputPath = Join-Path $ProjectRoot "runtime-logs\hardware-$stamp.log"
                }
                Invoke-QSeeSerialCapture -Port $port -Baud $Settings.Baud `
                    -DurationSeconds $Settings.DurationSeconds -OutputPath $OutputPath `
                    -NoReset:$NoReset | Out-Null
            }
            'combo' {
                Invoke-QSeeProjectBuild -ProjectRoot $ProjectRoot -Settings $Settings
                Resolve-QSeeStaleMonitorProcesses -RequestedPort $port `
                    -BuildDir $Settings.BuildDir -Stop:$StopStaleMonitors
                $port = Resolve-QSeeSerialPort -RequestedPort $port
                Invoke-QSeeNativeCommand -FilePath $idf.Python `
                    -ArgumentList ($commonArgs + @('-p', $port, '-b', [string]$Settings.Baud, 'flash')) `
                    -FailureMessage 'Flash failed'

                $entrypoint = Join-Path $ScriptsRoot 'device.ps1'
                $monitorArguments = @(
                    '-NoExit', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                    '-File', $entrypoint, 'monitor',
                    '-Port', $port, '-BuildDir', $Settings.BuildDir
                )
                # ArgumentList preserves spaces without building a fragile Windows command line.
                $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
                $startInfo.FileName = Join-Path $PSHOME 'pwsh.exe'
                $startInfo.WorkingDirectory = $ProjectRoot
                $startInfo.UseShellExecute = $true
                $startInfo.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Normal
                foreach ($argument in $monitorArguments) {
                    [void]$startInfo.ArgumentList.Add([string]$argument)
                }
                $monitorProcess = [System.Diagnostics.Process]::Start($startInfo)
                if ($null -eq $monitorProcess) {
                    throw 'The serial monitor process could not be started.'
                }
                $monitorProcess.Dispose()
                Write-QSeeSuccess "Started the serial monitor for $port in a new terminal."
            }
            'recover' {
                Invoke-QSeeProjectBuild -ProjectRoot $ProjectRoot -Settings $Settings
                Resolve-QSeeStaleMonitorProcesses -RequestedPort $port `
                    -BuildDir $Settings.BuildDir -Stop:$StopStaleMonitors
                $port = Resolve-QSeeSerialPort -RequestedPort $port
                Invoke-QSeeNativeCommand -FilePath $idf.Python `
                    -ArgumentList ($commonArgs + @('-p', $port, '-b', [string]$Settings.Baud, 'flash')) `
                    -FailureMessage 'Recovery flash failed'
            }
            default {
                throw "Unsupported firmware action: $Action"
            }
        }
    }
    finally {
        Pop-Location
    }
}
