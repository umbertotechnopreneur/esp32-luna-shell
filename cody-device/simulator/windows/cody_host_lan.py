"""Read host IPv4 configuration without transmitting or accessing credentials.

This is a simulated input source, not a measurement of the Cody device or a
claim that Internet or another peer is reachable.
"""

import ipaddress
import json
import subprocess
import sys


_WINDOWS_QUERY = r"""
$ErrorActionPreference = 'Stop'
$rows = @(Get-NetIPConfiguration | Where-Object { $_.NetAdapter.Status -eq 'Up' } | ForEach-Object {
    $hasGateway = $null -ne $_.IPv4DefaultGateway
    foreach ($ip in @($_.IPv4Address)) {
        if ($null -ne $ip -and $ip.IPAddress) {
            [pscustomobject]@{ address = [string]$ip.IPAddress; gateway = [bool]$hasGateway }
        }
    }
})
ConvertTo-Json -InputObject $rows -Compress
"""


# command: Fixed, read-only OS query; no shell, device access or network request.
# Returns parsed configuration, or raises the original process/JSON error.
def _query(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=8, check=True)
    return json.loads(result.stdout)


# No parameters: obtain a currently configured host LAN IPv4, if one exists.
# Returns a string IPv4 or None for a successful query without an eligible address.
# Raises OSError, subprocess.SubprocessError or ValueError when it cannot be read.
def host_lan_ipv4():
    if sys.platform == "win32":
        rows = _query(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", _WINDOWS_QUERY])
        if not isinstance(rows, list):
            raise ValueError("Unexpected host LAN response")
        candidates = [(not bool(row.get("gateway")), row.get("address")) for row in rows if isinstance(row, dict)]
    elif sys.platform.startswith("linux"):
        interfaces = _query(["ip", "-j", "-4", "address", "show", "up"])
        routes = _query(["ip", "-j", "-4", "route", "show", "default"])
        if not isinstance(interfaces, list) or not isinstance(routes, list):
            raise ValueError("Unexpected host LAN response")
        defaults = {row.get("dev") for row in routes if isinstance(row, dict)}
        candidates = []
        for interface in interfaces:
            if not isinstance(interface, dict):
                continue
            preferred = interface.get("ifname") in defaults
            for address in interface.get("addr_info", []):
                if isinstance(address, dict) and address.get("family") == "inet":
                    candidates.append((not preferred, address.get("local")))
    else:
        raise OSError("Host LAN configuration is unsupported on this platform")
    valid = []
    for fallback, value in candidates:
        try:
            address = ipaddress.IPv4Address(value)
        except (ipaddress.AddressValueError, TypeError):
            continue
        if not (address.is_loopback or address.is_link_local or address.is_unspecified or address.is_multicast):
            valid.append((fallback, str(address)))
    return min(valid)[1] if valid else None
