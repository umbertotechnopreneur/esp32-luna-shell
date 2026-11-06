# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested VibeWare branding.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

# Serial selection is separate from ESP-IDF initialization and never opens a port.

# RequestedPort: Windows COM identity or an absolute Linux USB serial path.
# Throws when the supplied name is empty or does not identify a supported serial port.
function Normalize-PortName([string]$RequestedPort) {
    if ($IsLinux) {
        $port = [string]$RequestedPort
        if ($port -cnotmatch '\A/dev/tty(?:ACM|USB)[0-9]+\z' -and
            $port -cnotmatch '\A/dev/serial/by-id/(?!\.{1,2}\z)[0-9A-Za-z._:+#=@-]+\z') {
            throw "Invalid serial port '$RequestedPort'. Use -Port /dev/ttyACM0, /dev/ttyUSB0, or /dev/serial/by-id/<name>."
        }
        return $port
    }

    if ([string]::IsNullOrWhiteSpace($RequestedPort)) {
        throw 'Specify a serial port with -Port COM10 or -Port 10.'
    }

    $normalizedPort = $RequestedPort.Trim().ToUpperInvariant()
    if ($normalizedPort -match '^[1-9]\d*$') {
        $normalizedPort = "COM$normalizedPort"
    }
    if ($normalizedPort -notmatch '^COM[1-9]\d*$') {
        throw "Invalid serial port '$RequestedPort'. Use -Port COM10 or -Port 10."
    }

    return $normalizedPort
}

# RequestedPort: Exact Linux tty node or a udev by-id alias. The caller retains
# the requested path for opening the serial device.
# Throws on a non-Linux host, malformed identity, invalid symlink, or failed metadata read.
# Returns no object for a device that is absent or has disappeared.
function Get-LinuxSerialPortDevice([string]$RequestedPort) {
    if (-not $IsLinux) {
        throw 'Linux serial device metadata requires a Linux host.'
    }

    $port = Normalize-PortName $RequestedPort
    $devicePath = $port
    try {
        $entry = Get-Item -LiteralPath $port -Force -ErrorAction Stop
        if ($port.StartsWith('/dev/serial/by-id/', [StringComparison]::Ordinal)) {
            if ($entry.LinkType -ne 'SymbolicLink') {
                throw "Serial alias '$port' must be a symbolic link to a USB serial device."
            }
            $target = $entry.ResolveLinkTarget($true)
            if ($null -eq $target -or $target.FullName -cnotmatch '\A/dev/tty(?:ACM|USB)[0-9]+\z') {
                throw "Serial alias '$port' must resolve to /dev/ttyACM* or /dev/ttyUSB*."
            }
            $devicePath = $target.FullName
        }

        $ttyName = [IO.Path]::GetFileName($devicePath)
        $ttyClass = Get-Item -LiteralPath "/sys/class/tty/$ttyName" -Force -ErrorAction Stop
        $kernelDevice = $ttyClass.ResolveLinkTarget($true)
        if ($null -eq $kernelDevice -or
            -not $kernelDevice.FullName.StartsWith('/sys/devices/', [StringComparison]::Ordinal)) {
            throw "Serial port '$port' has an invalid Linux tty device identity."
        }
        $deviceNumbers = [string](Get-Content -LiteralPath (Join-Path $kernelDevice.FullName 'dev') -Raw -ErrorAction Stop)
    } catch [System.Management.Automation.ItemNotFoundException], [System.IO.FileNotFoundException], [System.IO.DirectoryNotFoundException] {
        return
    }

    # stat reads the node metadata without opening it or changing DTR/RTS. Match
    # the character node's major/minor to the same current kernel tty identity.
    $stat = Get-Command stat -CommandType Application -ErrorAction Stop
    $PSNativeCommandUseErrorActionPreference = $false
    $metadata = & $stat.Path -c '%f %t %T' -- $devicePath 2>&1
    $statExitCode = $LASTEXITCODE
    if ($statExitCode -ne 0) {
        try {
            $null = Get-Item -LiteralPath $devicePath -Force -ErrorAction Stop
        } catch [System.Management.Automation.ItemNotFoundException] {
            return
        }
        throw "Serial port metadata read failed for '${devicePath}' (stat exit $statExitCode): $($metadata -join ' ')"
    }
    if ($metadata -isnot [string] -or
        $metadata -notmatch '\A([0-9a-f]+) ([0-9a-f]+) ([0-9a-f]+)\z') {
        throw "Unexpected serial node metadata for '$devicePath'."
    }
    $mode = [Convert]::ToUInt32($Matches[1], 16)
    $major = [Convert]::ToUInt32($Matches[2], 16)
    $minor = [Convert]::ToUInt32($Matches[3], 16)
    if (($mode -band 0xf000) -ne 0x2000 -or $deviceNumbers.Trim() -cne "${major}:$minor") {
        throw "Serial port '$port' does not identify the current USB serial character device."
    }

    [pscustomobject]@{
        DeviceID = $port
        DevicePath = $devicePath
        FriendlyName = "USB serial ($ttyName)"
        InstanceId = $kernelDevice.FullName
        Status = 'OK'
    }
}

# No parameters: enumerate current serial devices without probing their handles.
# Throws the original discovery error rather than treating a failed query as an empty list.
function Get-PresentSerialPorts {
    if ($IsLinux) {
        $ports = @(
            foreach ($name in (Get-ChildItem -LiteralPath '/dev' -Name -ErrorAction Stop)) {
                if ($name -cmatch '\Atty(?:ACM|USB)[0-9]+\z') {
                    Get-LinuxSerialPortDevice -RequestedPort "/dev/$name"
                }
            }
            try {
                $aliases = @(Get-ChildItem -LiteralPath '/dev/serial/by-id' -Name -ErrorAction Stop)
            } catch [System.Management.Automation.ItemNotFoundException] {
                # udev may not create any aliases for the present adapters.
                $aliases = @()
            }
            foreach ($name in $aliases) {
                Get-LinuxSerialPortDevice -RequestedPort "/dev/serial/by-id/$name"
            }
        )
        return @($ports | Sort-Object DeviceID -CaseSensitive)
    }

    try {
        # Filter after enumeration so an absent Ports class produces an empty list,
        # while a failure of the Windows query remains a real error.
        $devices = @(Get-PnpDevice -PresentOnly -ErrorAction Stop)
    } catch {
        throw [System.InvalidOperationException]::new(
            "Serial port discovery failed: $($_.Exception.Message)", $_.Exception)
    }

    $ports = foreach ($device in $devices) {
        if ($device.Class -ne 'Ports' -or $device.FriendlyName -notmatch '\((COM\d+)\)') {
            continue
        }

        [pscustomobject]@{
            DeviceID = $Matches[1].ToUpperInvariant()
            FriendlyName = $device.FriendlyName
            InstanceId = $device.InstanceId
            Status = $device.Status
        }
    }

    return @($ports | Sort-Object DeviceID)
}

# Selection: Explicit COM identity or Linux path entered by the user.
# PresentPorts: Exact inventory shown before selection.
# Throws for cancellation, an absent or ambiguous identity, or a reported device failure.
function Resolve-SerialPortSelection([string]$Selection, [object[]]$PresentPorts) {
    if ([string]::IsNullOrWhiteSpace($Selection)) {
        throw 'Serial port selection cancelled; no device operation started.'
    }

    $selectedPort = Normalize-PortName $Selection
    $comparison = if ($IsLinux) { [StringComparison]::Ordinal } else { [StringComparison]::InvariantCultureIgnoreCase }
    $matchingPorts = @($PresentPorts | Where-Object { [string]::Equals($_.DeviceID, $selectedPort, $comparison) })
    if ($matchingPorts.Count -eq 0) {
        throw "Serial port $selectedPort is not in the detected list. Reconnect the device and retry, or specify -Port explicitly."
    }
    $platform = if ($IsLinux) { 'Linux' } else { 'Windows' }
    if ($matchingPorts.Count -ne 1) {
        throw "Serial port $selectedPort has ambiguous $platform device identities. Run device.ps1 discovery before retrying."
    }
    if ($matchingPorts[0].Status -ne 'OK') {
        throw "Serial port $selectedPort reports $platform device status '$($matchingPorts[0].Status)'. Run device.ps1 discovery before retrying."
    }

    return $selectedPort
}

# RequestedAction: Device operation waiting for an explicit user selection.
# Throws if interactive input or a present serial device is unavailable, or selection fails.
function Select-SerialPort([string]$RequestedAction) {
    if ([Console]::IsInputRedirected) {
        $example = if ($IsLinux) { '-Port /dev/ttyACM0' } else { '-Port COM10 or -Port 10' }
        throw "Serial port selection for '$RequestedAction' requires a terminal. Supply $example when running non-interactively."
    }

    $ports = @(Get-PresentSerialPorts)
    if ($ports.Count -eq 0) {
        throw 'No present serial ports detected. Connect the device and retry; use device.ps1 discovery to inspect USB enumeration errors.'
    }

    Write-Host "Detected serial ports for '$RequestedAction':"
    $ports | Format-Table DeviceID, FriendlyName, Status -AutoSize | Out-Host
    # Even a single detected device needs a deliberate choice; list order has no meaning.
    $prompt = if ($IsLinux) { 'Port (exact /dev path; Enter cancels)' } else { 'Port (COM name or port number; Enter cancels)' }
    $selection = Read-Host $prompt
    $selectedPort = Resolve-SerialPortSelection -Selection $selection -PresentPorts $ports
    Write-Host "Selected serial port: $selectedPort"
    return $selectedPort
}
