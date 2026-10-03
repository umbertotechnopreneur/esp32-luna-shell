# Serial selection is separate from ESP-IDF initialization and never opens a port.

# RequestedPort is a Windows COM name or its positive port number, such as COM10 or 10.
# Throws when the supplied name is empty or does not identify a Windows COM port.
function Normalize-PortName([string]$RequestedPort) {
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

# No parameters: enumerate present Windows serial devices without probing their handles.
# Throws the real Windows discovery error instead of treating a failed query as an empty list.
function Get-PresentSerialPorts {
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

# Selection is the COM name or port number entered by the user.
# PresentPorts is the exact inventory shown to the user, with COM names as identities.
# Throws for cancellation, an absent or ambiguous COM identity, or a reported device failure.
function Resolve-SerialPortSelection([string]$Selection, [object[]]$PresentPorts) {
    if ([string]::IsNullOrWhiteSpace($Selection)) {
        throw 'Serial port selection cancelled; no device operation started.'
    }

    $selectedPort = Normalize-PortName $Selection
    $matchingPorts = @($PresentPorts | Where-Object { $_.DeviceID -eq $selectedPort })
    if ($matchingPorts.Count -eq 0) {
        throw "Serial port $selectedPort is not in the detected list. Reconnect the device and retry, or specify -Port explicitly."
    }
    if ($matchingPorts.Count -ne 1) {
        throw "Serial port $selectedPort has ambiguous Windows device identities. Run device.ps1 discovery before retrying."
    }
    if ($matchingPorts[0].Status -ne 'OK') {
        throw "Serial port $selectedPort reports Windows device status '$($matchingPorts[0].Status)'. Run device.ps1 discovery before retrying."
    }

    return $selectedPort
}

# RequestedAction identifies the device operation waiting for an explicit user selection.
# Throws if interactive input or a present serial device is unavailable, or selection fails.
function Select-SerialPort([string]$RequestedAction) {
    if ([Console]::IsInputRedirected) {
        throw "Serial port selection for '$RequestedAction' requires a terminal. Supply -Port COM10 or -Port 10 when running non-interactively."
    }

    $ports = @(Get-PresentSerialPorts)
    if ($ports.Count -eq 0) {
        throw 'No present serial ports detected. Connect the device and retry; use device.ps1 discovery to inspect USB enumeration errors.'
    }

    Write-Host "Detected serial ports for '$RequestedAction':"
    $ports | Format-Table DeviceID, FriendlyName, Status -AutoSize | Out-Host
    # Even a single detected device needs a deliberate choice; list order has no meaning.
    $selection = Read-Host 'Port (COM name or port number; Enter cancels)'
    $selectedPort = Resolve-SerialPortSelection -Selection $selection -PresentPorts $ports
    Write-Host "Selected serial port: $selectedPort"
    return $selectedPort
}
