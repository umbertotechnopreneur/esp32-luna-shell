# Completed work

A short record of what changed and what was actually observed. The full dated
reports, failed attempts, hashes and original checklists live in [archive.md](archive.md).
Open follow-ups are in [TODO.md](TODO.md) and [planned-todo.md](planned-todo.md).
A recorded build or flash is not physical acceptance of a later revision.

## 2026-10-21: documentation cleanup

Shortened the README, retained the four Waveshare photos and independence/license
notes, and preserved the VibeWare footer. Separated active follow-ups, deferred
work, reusable lessons and historical reports. Updated stale Home/audio/delivery
wording without closing unobserved device gates. Static document/source review
only; no tests, builds, simulator, hardware operations or PDF regeneration.

Earlier the same day, `17f1884b` published the Waveshare gallery/disclaimer to
`codex/cody` with `[skip ci]`. Its changed-file secret scan passed for three
Markdown files; GitHub reported zero workflow runs for that commit.

## Latest recorded firmware deliveries

These are dated records, not a live inventory of the attached device.

| Date | Image | Recorded result |
| --- | --- | --- |
| 2026-10-20 | Debug `cody-229-g027800b1` | Current-source build passed, 1,560 steps at four jobs; **not flashed**. Wake word and activity classifier retained. |
| 2026-10-17 | Release `cody-224-g99b167b3` | Touch-IRQ wake/breadcrumb changes built and flashed to COM10; all five segment hashes verified. Physical wake acceptance remains open. |
| 2026-10-17 | Release `cody-220-gce83bd10` | Built/flashed with five verified segments. PC notice fix has 147/147 consolidated host cases; plugin live activation remains open. |
| 2026-10-17 | Release `cody-218-g35467b25` | Earlier scoped delivery built/flashed with five verified segments. |

Latest Debug application: **7,083,232 bytes**, leaving **191,264 bytes** in the
7,274,496-byte OTA slot. SHA-256:
`4623918b8369b7b7ccbd964176142398ac40e49e8a89538506795a7d225d08d1`.
Its resolved dependency lock matched the canonical lock. The 20 October request
covered Git and build only: no flash, simulator, capture or automated suite.

Latest recorded Release application: **6,926,768 bytes**. SHA-256:
`83ed2d51c8af8bfd7711701c4579cef1e4cf74119d44ffcdd0d2d47071985bfc`.
Two GCC crashes preceded successful incremental retries; their cause is still
open in TODO.md. Flash completion did not establish post-reset device behavior.

[20 October details](archive.md#canonical-branch-consolidation-and-debug-build---2026-10-20)
· [17 October wake delivery](archive.md#native-touch-wake-and-retained-diagnostics---2026-10-17)
· [Approval-notice investigation](archive.md#approval-notice-source-fix-host-checks-and-firmware-flash---2026-10-17).

## Completed milestones

| Date | Change and evidence | Detailed record |
| --- | --- | --- |
| 2026-10-20 | Consolidated source on `main` and `codex/cody` at `027800b1`; verified retired branch tips as ancestors before deletion. Publication scan: 23 changed source files. | [Git delivery](archive.md#canonical-branch-consolidation-and-debug-build---2026-10-20) |
| 2026-10-17 | Lossless sprite storage saved 302,904 flash bytes. Missing display-touch/full-wake-word locks and PDF topology fixed. Consolidated **48/48** automatic checks and four builds/size reports passed; initial failures remain recorded. PDFs rendered and mirrored to Obsidian. | [Footprint, locks and PDFs](archive.md#debug-footprint-dependency-locks-and-pdf-validation---2026-10-17) |
| 2026-10-17 | Readability and native approval fixes; obsolete ACTIVITY formats removed. Earlier Debug OTA overflow and the 46/48 intermediate check result were subsequently resolved by the milestone above. | [Source review](archive.md#readability-native-approval-fixes-and-verification---2026-10-17) |
| 2026-10-17 | Approved UI references confirmed tracked; Windows Studio capture passed 29 cases. Retired PWR screenshot/double-click and facial-channel UI tasks removed. | [Backlog reconciliation](archive.md#backlog-reconciliation---2026-10-17) |
| 2026-10-16 | Unified diagnostics and Memory in Debug/Release; PC-approval presentation, gentle question alerts and shorter offline/manual wake implemented and delivered in the recorded revisions. Physical presentation gates remain deferred. | [Diagnostics](archive.md#memory-diagnostic-release-delivery---2026-10-16) · [Wake delivery](archive.md#offline-standby-and-question-alert-release-delivery---2026-10-16) |
| 2026-10-15 | Imported purchased 1.75C/1.43C references and completed a static board comparison; no board port was implemented. | [Hardware import](archive.md#purchased-enclosed-waveshare-reference-import---2026-10-15) |
| 2026-10-15 | Public queue pilot delivered a 359,406-byte WAV intact to the chat; prompt preparation stripped unsupported audio, so speech understanding was **not** established. Owner declined separately billed transcription. | [Audio reconciliation](archive.md#backlog-reconciliation---2026-10-17) |
| 2026-10-15 | Cancellable recording countdowns, MCP transport-presence state and reply-delivery presentation implemented; physical recording/interaction remain deferred. | [Voice lifecycle](archive.md#codex-transport-presence-and-cancellable-voice-countdowns---2026-10-15) |
| 2026-10-14 | Four Home panels, bounded scrolling, finite @CCP footer, native chat/context metadata and other-chat reply notices integrated. Host/SIMULATED evidence is revision-specific. | [Home panels](archive.md#four-home-panels-finite-ccp-introduction-and-other-chat-reply---2026-10-14) |
| 2026-10-13 | First reported 1,000 mAh Debug run retained **6 h 55 min 42.870 s** last-alive time. One source gap and unavailable final UTC prevent an exact battery-death or continuous-source claim. | [Battery readback](archive.md#first-1000-mah-battery-run-readback---2026-10-13) |
| 2026-10-13 | Saved-network startup and expedited wake reconnect integrated. End-to-end physical latency remains unmeasured. | [Reconnect](archive.md#saved-network-startup-and-wake-reconnect---2026-10-13) |
| 2026-10-12 | Power/shutdown ownership, observational pretrained HAR and endurance recorder integrated. No model training or classifier-driven shutdown. | [Power and activity](archive.md#cody-power-and-pretrained-observer-implementation---2026-10-12) |
| 2026-10-11 and earlier | Native character, CCP/provisioning, OTA partition layout, TTS, display/memory and other platform work retain their original revisions and limits. USB installation did not implement OTA upload/rollback. | [Full history](archive.md#delivery-and-implementation-history) |

## Evidence storage

Tracked summaries and the archive retain results even when local output is cleaned.
The latest recorded delivery folders are `artifacts/build-canonical-delivery-20261020/`,
`artifacts/build-cody-wake-evidence-20261017/`,
`artifacts/build-cody-approval-evidence-20261017/` and
`artifacts/build-cody-delivery-evidence-20261017/`. Their names do not guarantee
that every ignored file still exists. Private core dumps are not publication assets.
Earlier pre-consolidation records remain in Git `9956446`.
