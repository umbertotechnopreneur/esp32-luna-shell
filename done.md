# Latest validation round

## 2026-11-04 · Waveshare 1.75C Wi-Fi pairing on COM8

The Debug image with Sophia and the activity classifier was built from source and
flashed to the 32 MB board. All five regions passed hash verification. Cody
reconnected with its saved Wi-Fi settings and synchronized UTC via SNTP.
The 180-second capture reached the Codex connection prompt without a panic.
It ended before the repeated Wi-Fi setup reached the Debug details screen.

The phone confirmed the provisioning exchange during this validation round.
The pairing worker retained at least 1,016 of its 4,096 stack bytes through
BLE startup and cleanup. The physical 1.75C subsequently confirmed CCP pairing
with the PC, and the plugin reported an authenticated connection.
On wake, the touch controller rejected a packet and
display rotation was deferred. Touch and rotation remain physical acceptance
items.
