# Cody first-setup network journey

Implementation source checkpoint, 4 October 2026. Build, host checks, simulator
and physical pairing remain unverified for this change.

The source of truth is navigation atlas page 02 for startup and **addendum A01,
page 13**, for QR routes (also planning atlas page 23). The addendum supplements
the printed pages without renumbering them. Hello is screen **04**, not 06.

## Startup and entry

Cold boot/software reset: Sleeping01 fades the animated pearl character in over
2 seconds, Waking02 interpolates over 4 seconds, and Ready03 holds for 4 seconds.
Actual service checkpoints still gate advancement. First setup enters Hello04
with the Happy face and `Hi, I'm Cody!` monospace typewriter. After the complete
text has actually retired to the panel, it remains readable for one second of
active animation time, then enters 05. Non-first setup instead enters Idle16.

Sleep/wake panel fades use the shared 800-ms default. A software wake exposes an
already-rendered Sleeping01 through that panel fade, without multiplying a second
two-second character fade over it; Waking02 and Ready03 then receive 4 seconds each.
Standby and pause do not advance the Hello reading hold or typewriter.

## QR and radio routing

| Current route/action | Result |
| --- | --- |
| 04, intro fully displayed | 05 / Let's pair |
| 05, Show QR | 06 / installation QR |
| 06, Back | 05 |
| 06, Pair Cody | Queue explicit Start (or a new Retry of a clean terminal attempt); wait for the real session |
| ReadyToScan, current attempt/session QR encoded | 06-bis / Scan to pair |
| 06-bis, Back | Queue Cancel; wipe the device QR; await Cancelled before returning to 06 |
| Credentials received / Connecting / Completing | 07 / Pairing... |
| 07, Cancel | Await confirmed cleanup, then 06 |
| Failed / TimedOut | 08 / actual error; Retry only after successful cleanup |
| 08, Retry | New attempt and a new device QR; never replay the whole OOBE |
| Online plus currently valid Wi-Fi association and IP | 09 / You're online. |

06 deliberately has **no visible title**. Its QR encodes the actual inherited
Android provisioning-app installation URL:
`https://play.google.com/store/apps/details?id=com.espressif.provble`.
It does not start BLE. 06-bis encodes only the current device/session payload
copied through `LUNASHELL_provisioning_copy_qr`, scanned inside that app.
Neither illustrative QR drawings nor a previous attempt's QR enter firmware.

09 continues checking the live Wi-Fi observation. Losing the link replaces its
online claim with `No connection` and `Wi-Fi disconnected` or `UNAVAILABLE`.
The subsequent preferences route 10 is not implemented by this increment; no
nonfunctional Continue button or implicit first-boot persistence is added.
The current development force-first-boot setting remains respected.

## Ownership, memory and reusable pieces

- `CODY_lifecycle` owns active-time expression sequencing. One lightweight timing
  header shares the panel fade duration with the power owner.
- `CODY_scene` still owns cloud/title/optional content. `title_visible` explicitly
  hides the installation title without an empty/fallback glyph or a special renderer.
- `CODY2D::CapsuleControl` shares full bounds between drawing and hit testing.
  Actions reuse the existing Blue Primary anchors; Retry remains Primary. Back is
  one left chevron at `(10,203)` in a 60x60 target, separate from content/footer.
- Logical drawing and acquired touch share the existing rotation owner. The cloud
  remains animated/reactive at each authored scale; QR modules are never warped.
- One static 4-KiB action-worker stack moves Start/Retry/Cancel admission and QR
  encoding off the display worker. Radio lifecycle/init/cleanup stays with the
  existing pairing owner. A single-slot release/acquire mailbox has no per-frame
  allocations or unbounded action queue.
- Version-10 QR modules fit in **407 bytes**, with a four-module white quiet zone
  and integer pixel scaling in a 210x210 logical area. No RGB QR framebuffer exists.
  The pinned `espressif/qrcode` dependency is `0.1.0~2`, as in the existing full UI.
- The same licensed JetBrains Mono source supplies an additional 18px printable-
  ASCII caption/action font: **7,125 bytes in internal flash**, no runtime glyph cache.
  The existing title font is unchanged; no localization or SD dependency is added.

All measured radio state comes from `DeviceSnapshotService.readConnectivityObservation`.
Validity/staleness and typed attempt/session identities gate every action and QR.
Admission never means Online or Cancelled. An encoding error remains visible and
requires explicit Retry; there is no automatic allocation/retry loop. Payloads and
QR modules are wiped when retired and are never written to logs, HTTP or files.
Snapshot-read/render failures still propagate their original errors.

## Deferred validation

The new Cody-only QR manifest requires the normal reviewed dependency-lock update
through `scripts/device.ps1 update-lock -Profile cody-companion -BuildMode Debug
-BuildDir artifacts/build-cody-companion-debug` before building. Do not bypass the
canonical lock comparison. Tests/build/flash require the user's explicit approvals.
The asset generator is source preparation, not firmware or simulator validation.
