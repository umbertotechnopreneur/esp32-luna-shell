param([Parameter(Mandatory)][string]$WirePath)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../common/EnduranceReadback.ps1')

# Condition is a required decoder outcome from the actual production encoder fixture.
# Message identifies the rejected contract.
function Assert-EnduranceContract([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$bytes = [IO.File]::ReadAllBytes($WirePath)
$result = ConvertFrom-DeviceEnduranceSnapshot -Bytes $bytes
Assert-EnduranceContract ($result.schema -eq 'LBE1' -and $result.initialized -and -not $result.source_current) 'Snapshot health lost.'
Assert-EnduranceContract ($result.owner_error -eq 0x107) 'Original owner error lost.'
Assert-EnduranceContract ($result.current.session_id -eq 18 -and $result.previous.session_id -eq 17) 'Record identities swapped.'
Assert-EnduranceContract ($result.current.elapsed_lower_bound_ms -eq 61000) 'Elapsed lower bound changed.'
Assert-EnduranceContract ($null -eq $result.current.first_wake_elapsed_ms -and $null -eq $result.current.first_wake_utc_s) 'Unobserved wake fabricated.'

foreach ($scenario in @('size','magic','flags','reserved','phase','record-flags','record-reserved','empty','utc-validity','wake-validity','negative-time','session-order','absent-current')) {
    $corrupt = [byte[]]$bytes.Clone()
    switch ($scenario) {
        size { $corrupt = [byte[]]$corrupt[0..126] }
        magic { $corrupt[0] = 0 }
        flags { $corrupt[4] = 128 }
        reserved { $corrupt[12] = 1 }
        phase { $corrupt[36] = 5 }
        record-flags { $corrupt[37] = 128 }
        record-reserved { $corrupt[38] = 1 }
        empty { $corrupt[36] = 0 }
        utc-validity { $corrupt[37] = [byte]($corrupt[37] -band 254) }
        wake-validity { $corrupt[37] = [byte]($corrupt[37] -bor 8) }
        negative-time { $corrupt[23] = 128 }
        session-order { [BitConverter]::GetBytes([uint32]18).CopyTo($corrupt, 80) }
        absent-current { [Array]::Clear($corrupt, 32, 48) }
    }
    $rejected = $false
    try { $null = ConvertFrom-DeviceEnduranceSnapshot -Bytes $corrupt }
    catch [IO.InvalidDataException] { $rejected = $true }
    Assert-EnduranceContract $rejected "Malformed $scenario accepted."
}
$unavailable = [byte[]]::new(128)
[Text.Encoding]::ASCII.GetBytes('LBE1').CopyTo($unavailable, 0)
[BitConverter]::GetBytes([int]0x103).CopyTo($unavailable, 8)
$result = ConvertFrom-DeviceEnduranceSnapshot -Bytes $unavailable
Assert-EnduranceContract (-not $result.initialized -and $result.owner_error -eq 0x103 -and $result.current.phase -eq 'empty') 'Initializing unavailable state lost.'

# RequestedPath is a fixture-owned artifact path; only the real publication wrapper is under test here.
# DefaultRelativePath is unused because every fixture path is explicit.
function Resolve-ArtifactPath([string]$RequestedPath, [string]$DefaultRelativePath) {
    return [IO.Path]::GetFullPath($RequestedPath)
}
# RequestedPort is a synthetic identity; this fixture never creates a serial object.
# RequestedBaud is the forwarded transport option.
# StoredLog must select the endurance owner exclusively.
# Destination is the artifact wire output consumed by the real wrapper.
# ResponseTimeoutSeconds is the forwarded response bound.
function Invoke-DeviceStoredLog([string]$RequestedPort, [int]$RequestedBaud, [string]$StoredLog,
        [string]$Destination, [int]$ResponseTimeoutSeconds) {
    Assert-EnduranceContract ($StoredLog -eq 'endurance') 'Wrapper selected the SD owner.'
    [IO.File]::WriteAllBytes($Destination, $script:transportBytes)
}
$script:transportBytes = [byte[]]$bytes.Clone()
$destination = Join-Path ([IO.Path]::GetDirectoryName($WirePath)) 'readback-publication.json'
$ownerFailed = $false
try {
    Invoke-DeviceEnduranceReadback -RequestedPort FAKE -RequestedBaud 115200 -Destination $destination -ResponseTimeoutSeconds 1
} catch [IO.IOException] {
    $ownerFailed = $_.Exception.Message.Contains('esp_err=263')
}
Assert-EnduranceContract $ownerFailed 'Owner failure did not produce a failing command result.'
$saved = [IO.File]::ReadAllText($destination) | ConvertFrom-Json
Assert-EnduranceContract ($saved.owner_error -eq 263 -and $saved.current.session_id -eq 18 -and
    $saved.transport_status -eq 'usb_crc_verified') 'Owner failure lost its verified retained fields.'
[Array]::Clear($script:transportBytes, 8, 4)
Invoke-DeviceEnduranceReadback -RequestedPort FAKE -RequestedBaud 115200 -Destination $destination -ResponseTimeoutSeconds 1
$saved = [IO.File]::ReadAllText($destination) | ConvertFrom-Json
Assert-EnduranceContract (-not $saved.source_current -and $saved.owner_error -eq 0) 'Source gap was treated as a new owner error.'
Write-Host 'Endurance decoder contracts passed using real encoder output.'
