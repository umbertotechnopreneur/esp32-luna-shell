# Explicit canonical endurance readback over the existing checked USB transport.

# Bytes is the exact 128-byte canonical wire snapshot after transport CRC verification.
# Throws for unknown schemas, invalid flags, inconsistent records or unsupported host byte order.
function ConvertFrom-DeviceEnduranceSnapshot {
    param([Parameter(Mandatory)][byte[]]$Bytes)
    if (-not [BitConverter]::IsLittleEndian) { throw 'Endurance decoder requires a little-endian host.' }
    if ($Bytes.Length -ne 128 -or [Text.Encoding]::ASCII.GetString($Bytes, 0, 4) -cne 'LBE1') {
        throw [IO.InvalidDataException]::new('Invalid endurance snapshot size or schema.')
    }
    if (($Bytes[4] -band 252) -ne 0 -or ($Bytes[5..7] | Where-Object { $_ -ne 0 }) -or
            ($Bytes[12..15] | Where-Object { $_ -ne 0 })) {
        throw [IO.InvalidDataException]::new('Invalid endurance snapshot flags or reserved bytes.')
    }
    $observed = [BitConverter]::ToInt64($Bytes, 16)
    $persisted = [BitConverter]::ToInt64($Bytes, 24)
    if ($observed -lt 0 -or $persisted -lt 0) { throw [IO.InvalidDataException]::new('Negative endurance timestamps.') }
    $records = @()
    $phases = @('empty','running','external_power_observed','interrupted_unknown','shutdown_requested')
    foreach ($offset in @(32,80)) {
        $session = [BitConverter]::ToUInt32($Bytes, $offset)
        $phase = [int]$Bytes[$offset + 4]
        $flags = [int]$Bytes[$offset + 5]
        $elapsed = [BitConverter]::ToUInt64($Bytes, $offset + 8)
        $started = [BitConverter]::ToInt64($Bytes, $offset + 16)
        $alive = [BitConverter]::ToInt64($Bytes, $offset + 24)
        $wakeElapsed = [BitConverter]::ToUInt64($Bytes, $offset + 32)
        $wakeUtc = [BitConverter]::ToInt64($Bytes, $offset + 40)
        if ($phase -gt 4 -or ($flags -band 224) -ne 0 -or $Bytes[$offset+6] -ne 0 -or $Bytes[$offset+7] -ne 0) {
            throw [IO.InvalidDataException]::new('Invalid endurance record phase, flags or reserved bytes.')
        }
        if ($phase -eq 0) {
            if ($Bytes[$offset..($offset+47)] | Where-Object { $_ -ne 0 }) {
                throw [IO.InvalidDataException]::new('Nonempty data in an empty endurance record.')
            }
        } elseif ($session -eq 0 -or $elapsed -gt 9223372036854775 -or
                (($flags -band 1) -ne 0 -and $started -le 0) -or (($flags -band 1) -eq 0 -and $started -ne 0) -or
                (($flags -band 2) -ne 0 -and $alive -le 0) -or (($flags -band 2) -eq 0 -and $alive -ne 0) -or
                (($flags -band 4) -ne 0 -and $wakeElapsed -gt $elapsed) -or (($flags -band 4) -eq 0 -and $wakeElapsed -ne 0) -or
                (($flags -band 8) -ne 0 -and ($wakeUtc -le 0 -or ($flags -band 4) -eq 0)) -or
                (($flags -band 8) -eq 0 -and $wakeUtc -ne 0)) {
            throw [IO.InvalidDataException]::new('Inconsistent endurance record values.')
        }
        $records += [ordered]@{
            session_id = $session; phase = $phases[$phase]; flags = $flags
            elapsed_lower_bound_ms = $elapsed
            started_utc_s = $(if ($flags -band 1) { $started } else { $null })
            last_alive_utc_s = $(if ($flags -band 2) { $alive } else { $null })
            first_wake_elapsed_ms = $(if ($flags -band 4) { $wakeElapsed } else { $null })
            first_wake_utc_s = $(if ($flags -band 8) { $wakeUtc } else { $null })
            source_unverified = [bool]($flags -band 16)
        }
    }
    if ($records[1].session_id -ne 0 -and $records[1].session_id -ge $records[0].session_id) {
        throw [IO.InvalidDataException]::new('Endurance previous session must precede a present current session.')
    }
    return [ordered]@{
        schema = 'LBE1'; initialized = [bool]($Bytes[4] -band 1); source_current = [bool]($Bytes[4] -band 2)
        owner_error = [BitConverter]::ToInt32($Bytes, 8); observed_at_us = $observed; persisted_at_us = $persisted
        current = $records[0]; previous = $records[1]
        interpretation = 'Committed elapsed time is a lower bound, not measured battery death or confirmed physical shutdown.'
    }
}

# RequestedPort is the selected COM port; the shared transport never resets it.
# RequestedBaud is the USB serial rate.
# Destination is the dispatcher-owned artifact JSON path.
# ResponseTimeoutSeconds bounds one complete response, including unrelated serial output.
# Throws transfer/schema/file errors; old JSON survives until complete validation.
# Throws the reported owner failure after saving its verified retained records for inspection.
function Invoke-DeviceEnduranceReadback {
    param(
        [Parameter(Mandatory)][string]$RequestedPort,
        [Parameter(Mandatory)][int]$RequestedBaud,
        [Parameter(Mandatory)][string]$Destination,
        [Parameter(Mandatory)][int]$ResponseTimeoutSeconds
    )
    $destinationPath = Resolve-ArtifactPath -RequestedPath $Destination -DefaultRelativePath ''
    $wirePath = Resolve-ArtifactPath -RequestedPath ($destinationPath + '.wire')
    Invoke-DeviceStoredLog -RequestedPort $RequestedPort -RequestedBaud $RequestedBaud -StoredLog endurance `
        -Destination $wirePath -ResponseTimeoutSeconds $ResponseTimeoutSeconds
    $snapshot = ConvertFrom-DeviceEnduranceSnapshot -Bytes ([IO.File]::ReadAllBytes($wirePath))
    $snapshot['transport_status'] = 'usb_crc_verified'
    $temporary = Resolve-ArtifactPath -RequestedPath ($destinationPath + '.partial-' + [Guid]::NewGuid().ToString('N'))
    try {
        [IO.File]::WriteAllText($temporary, ($snapshot | ConvertTo-Json -Depth 5) + "`n", [Text.UTF8Encoding]::new($false))
        [IO.File]::Move($temporary, $destinationPath, $true)
    } catch {
        $originalFailure = $_
        try {
            if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
        } catch {
            Write-Warning "Endurance partial artifact cleanup also failed: $($_.Exception.Message)"
        }
        throw $originalFailure
    }
    Write-Host "Canonical endurance readback saved to $destinationPath (owner error $($snapshot.owner_error))."
    if ($snapshot.owner_error -ne 0 -or -not $snapshot.initialized) {
        throw [IO.IOException]::new("Endurance owner unavailable: esp_err=$($snapshot.owner_error), initialized=$($snapshot.initialized). USB transfer succeeded; retained fields and failure state are saved in $destinationPath. Empty records do not establish a successfully initialized journal.")
    }
}
