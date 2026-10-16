# Active work

The 17 October readability cleanup and its validation fixes are implemented.
The final compiled source is `e90ca52a20bef1d2ba6bf696da716026ac30527c`.
All 48 automatic check invocations were attempted on Windows: 46 passed and two
remain failed. The 14 checks affected by the final native scene change and both
native driver preparations passed on repeat. [done.md](done.md) records the exact
scope, failures, image identity and source checkpoints.

Active validation gates:

- **BUILD-DEBUG-OTA-01:** Release `cody-210-ge90ca52a` builds at 7,228,000 bytes,
  leaving 46,496 bytes in its OTA slot. Debug compiles and links at 7,384,656
  bytes but fails the actual slot check by 110,160 bytes. Preserve diagnostics,
  errors and the partition/storage contract while addressing its footprint.
- **PROFILE-LOCKS-01:** The full catalog check stops at missing `display-touch.lock`.
  Static catalog inspection also finds missing `full-wake-word.lock`. Resolve and
  review these through the public firmware CLI; the current Cody lock is present.
- **CODY-DOC-TOPOLOGY-01:** The document suite stops at the main-section boundary
  assertion for `cody-navigation-atlas-v1.pdf`. Review document/test alignment
  without losing original screen IDs, dated revisions or unrelated content.

No firmware flash, serial operation or plugin installation/reload was performed
for this cleanup. Physical pixels, touch, sound, actual wake latency and nested
stack headroom remain unmeasured. The larger static refactoring examples remain
CLARITY-01 through CLARITY-03 in [planned-todo.md](planned-todo.md). Future runs
retain the authorization boundaries in [AGENTS.md](AGENTS.md).

The approved PC-approval redesign is implemented: compact cloud, amber PC
heading, white approval title, only the originating chat title in a cyan marquee,
shared native colorful monitor artwork, one lower instruction and lateral Back.
Schema 2 now permits a complete 255-byte approval-title body only; question/other
body limits remain 63 bytes. The same title acquisition owner revalidates origin
session/turn/checkout after IO. Plugin and firmware must be updated together.
Release `cody-204-g7889bffe` is installed on COM10 with all five written hashes
verified. Its app is 7,227,264 bytes, leaving 47,232 bytes in the OTA slot. The
shared artwork/title owner uses bounded PSRAM storage and compiled frames were
reviewed; nested physical stack headroom remains unmeasured. Plugin 0.6.9 was
installed through the public Codex CLI with matching source files. Reopen Codex
to load the updated broker; installation does not prove running adoption.
The later host/native/Studio results are recorded in the 17 October entry. Physical pixels,
Back touch and perceived animation/audio remain unverified. The mistaken personal
firmware name default was reverted; personalization remains a runtime preference.

Earlier Release `cody-199-g2f640422` was installed on COM10 with all five written hashes
verified. The facial-channel list, its PSRAM storage and scrolling are removed;
internal animation channels remain. Device diagnostics are available in Release
and Debug. The Memory action opens existing canonical internal RAM/PSRAM heap
capacity and fragmentation details. Back returns to the overview, then normal.
The plugin and Studio accept only `character` and `normal`; scroll stays zero.

The authenticated physical LAN `character` command returned applied/error 0 and
reopened the overview. This ACK does not verify pixels or the Memory touch path.
Later host/simulator results are recorded in the 17 October entry. Physical
rendering/touch, sound quality/timing and nested target stack headroom remain unverified. Delivery logs, image identity,
compiled-frame review and the separate source-only publication gate are recorded
in [done.md](done.md).

The owner's 15 October enclosed-board reference import and static assessment are
recorded in [done.md](done.md). New 1.75C/1.43C hardware support remains deferred
as HARDWARE-PORT-01 in [planned-todo.md](planned-todo.md); those boards are not
covered by the existing firmware profile.

Updated 2026-10-16. The offline standby/farewell and faster software-wake changes
are implemented in source, together with the soft 1.8-second first question bell and
gentle reminders every ten awake seconds while a fresh question awaits Send.
Release `cody-199-g2f640422` built with ESP-IDF 6.0.2 and eight jobs and was installed
on COM10 with all five written hashes verified. Wake word and the experimental
activity classifier remain selected. Their native timer/owner/voice/lifecycle/
question-cadence checks are included in the 17 October automated results. No
standalone size report, serial capture or physical observation was performed for this cleanup.
Validate USB/battery PC absence, pairing/confirmation deferral, farewell cancellation,
sound/quiet policy, retained-state rollback, manual-only offline wake, nested stack
headroom and actual wake latency only after the owner's fresh explicit authorization.
The authored software-wake timeline is 1.4 seconds; this is not a device timing result.

Earlier delivery: Release `cody-187-g58b67f3b` was installed on COM10 on 15 October;
tested CCP source, including the new presence owner, is installed in the local
plugin cache. Host checks, Release
build/size/individual compiled frames, flash and passive capture are recorded in
[done.md](done.md), with the four Home panels and earlier delivery evidence.

The 15 October delivery-wait visual refinement is implemented in source: a lower
electric-blue breathing indicator shared by firmware and Studio. Its Release
build and COM10 flash passed on 15 October with the written image hash verified.
Fresh host/simulator checks and physical visual confirmation of this indicator
remain pending explicit authorization. One separately authorized question on the
preceding firmware completed with recipient acknowledgement and owner-confirmed physical display
success, as recorded in done.md. The earlier `Reply unconfirmed` was not
reproduced; available broker logs cannot establish its missing delivery step.
No other caller's answer was recovered, replayed or acknowledged.

Normal-EOF reporting loads in existing development clients when Codex next
reopens. Already-loaded clients supply no normal-close proof and remain
Unavailable on release. After broker restart, a genuine new prompt restores
followed chat metadata; no activity is synthesized to fill that gap.

The owner accepts WAV attachment delivery to this existing Codex chat and declines
separate transcription APIs/services/costs (15 October). The public experimental
queue pilot preserved all 359406 WAV bytes with matching SHA-256 in this chat.
Codex prompt preparation replaces unsupported audio with the observed omission
message before the model sees it; no transcription or understanding is claimed.

The audio receiver now uses an explicit installed native executable and the
public queue, with a reused queue-only stdio client. Setup is
`device.ps1 codex-audio -EnableAudioQueue`; it probes read-only access and stores
no API credentials. Completed WAVs are admitted once to the frozen followed chat;
uncertain admission is not retried. Source implementation, local validation and
installed adoption are distinguished in [done.md](done.md). Physical recording
and model-side speech understanding remain in VOICE-01. No private endpoint,
model-modality override or discarded launcher/proxy is restored.

Readable tool captions are active on the PC, including a known-ID table and
complete underscore-cleaned unknown names. Final host, installed-cache and
authenticated post-reload adoption evidence is recorded in done.md.

The 17 October Studio run passed all 29 cases, including actual Windows Tk
window/canvas ImageGrab capture. STUDIO-CAPTURE-01 is resolved in
[planned-todo.md](planned-todo.md), with its earlier failure retained as history.
This captures the SIMULATED client and does not verify physical Cody pixels.

GitHub visibility is public. Main ruleset 25049727 is active: contributors need
a pull request; only the owner's user identity has direct-push bypass.

CCP-USAGE-01 in [planned-todo.md](planned-todo.md) retains physical display/audio
confirmation during normal genuine chat and compaction events. It is not inferred
from synthetic screenshots, current PC metadata or authenticated LAN presence.

The user's deferred voice and walking/classifier trials remain in
[planned-todo.md](planned-todo.md), together with physical wake/shutdown, energy
measurements, nested stack headroom and native Desktop audio/runtime verification.
Future built-in speech understanding still needs a usable public ingress;
existing Desktop questions cannot be imported retroactively. Moving a gate does not mark it passed or request
user action now. A restarted CCP broker awaits a genuine user prompt to follow a chat.

The accepted SD/PCB limitation is unchanged. No model training or classifier-led
automatic shutdown is included. This queue grants no execution authorization;
follow [AGENTS.md](AGENTS.md) and the current user instruction.
