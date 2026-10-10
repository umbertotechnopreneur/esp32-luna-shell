# Deferred work

## CCP protocol security follow-up - 2026-10-10

- [ ] Evaluate standard PAKE or high-entropy out-of-band QR pairing. The current
  six-character code and verifier transcript still permit offline guessing;
  online ingress limits and code expiry cannot invalidate captured transcripts.
- [ ] Design standard authenticated encryption with explicit session/nonce and
  replay discipline. CCP's current HMAC authenticates readable LAN payloads;
  the new challenge gate does not add confidentiality or custom cryptography.
- [ ] Review optional device HTTP separately: plaintext bearer transport,
  authentication-failure isolation and the cost/data scope of public health.
  CCP hardening does not establish physical HTTP configuration or protect a
  compromised local PC. Keep Windows credential ownership for this phase.
- [ ] Bound startup-only runtime.log across repeated PC launches with explicit
  multi-process ownership. Network rejection diagnostics already use the
  exclusive broker's rotating log and cannot grow the bootstrap file.

## Cody character ideas kept simple - 2026-10-10

- [ ] Consider wake stretch and a few nonrepeating idle variants after physical
  review. Attention double blink, observed reply/interruption and work-kind
  acting are now implemented in the 2026-10-11 Codex increment, with final
  execution still pending in TODO.md.
- [ ] Consider richer time-of-day personality and long-task acting only if existing
  synchronized clock, day/night greetings and quiet rhythm prove insufficient.
  Reuse their owner; no new calendar, wellbeing reminders or inferred work fatigue.
- [ ] Reconcile expression-debug screen 38 after its separate agent's review;
  this character change does not add a replacement screen or alter debug poses.

## Cody companion follow-up - 2026-10-10

- [ ] Integrate a supported observer of this desktop chat's App Server token-usage
  notifications when available. Current context occupancy/capacity, reasoning effort,
  cumulative usage and compaction threshold remain explicitly unavailable; do not
  read transcripts or infer model capacity to fill them.
- [ ] Add a relative model cost badge after the current Codex validation gate.
  Proposed identities are Economy, Balanced, Expensive and Unknown; no displayed
  dollar amount, account balance or session-cost estimate. A platform-neutral
  Python pricing owner refreshes an official catalog once at the first plugin
  start on each local calendar day, using the existing exclusive broker instead
  of each hook process. Keep a validated persistent cache with its source,
  retrieval date and explicit stale/unavailable state; network or parse failure
  never overwrites the last valid catalog or blocks hook/approval delivery.
  Resolve only the exact observed model identity, with explicit supported aliases;
  do not guess a price for an unknown slug. The PC sends the selected typed band
  through a coordinated authenticated technical-schema update into the canonical
  snapshot. Cody renders a small Model & Context badge and performs no price
  calculation. Preserve context-row priority and compaction visibility on Home.
  Relative-band thresholds and the source basis still need a product decision:
  [Codex credit rates](https://learn.chatgpt.com/docs/pricing) and
  [API token rates](https://developers.openai.com/api/docs/pricing) are distinct.
  The documented [model-list endpoint](https://developers.openai.com/api/reference/resources/models/methods/list)
  does not supply prices. Keep speed separate; a price is not evidence of latency.
  This is a recorded proposal, not an implemented downloader or display feature.

These items are not active commitments. Move an item to [TODO.md](TODO.md) when
activated; authorization still follows [AGENTS.md](AGENTS.md).

## Cody product

- [ ] Complete Linux/macOS credential and launcher owners for the existing Codex
  Python plugin before claiming multiplatform support. The 2026-10-11 scope
  requires platform-neutral Python additions and excludes a new Windows service
  or general PC observer. Current character changes preserve that boundary;
  they do not port the existing Windows DPAPI/hidden-launch implementation.

- [ ] Explore a remote relay after the screen work: an adapter beside the Codex
  instance publishes genuine events to an ASP.NET Core/Azure backend, and Cody
  opens outbound TLS. Preserve user/device association, sequence/expiry and
  disconnect recovery. This 2026-10-10 idea is not implemented or authorized for
  deployment; the user explicitly deferred Aspire implementation. Verify where
  the remote instance exposes events before designing a relay around them.
- [ ] First investigate the App Server audio channel for speech from Cody.
  The exported Codex 0.147.0 schema inspected on 2026-10-10 contained experimental
  `thread/realtime/start`, `thread/realtime/appendAudio` and
  `thread/realtime/outputAudio/delta`, with transcription and audio/text output.
  External transcription/synthesis is therefore not a decided requirement.
  Hooks remain activity metadata; audio needs separate transport. Account
  availability, audio format and access to an already open desktop chat remain
  unverified. No voice session or device transport was started by that research.

- [ ] Explore three nontechnical product directions: everyday memory ("Remind
  me to..."), reviewed dictation to Codex/other supported AI destinations, and
  a considerate desk companion (focus sessions, break suggestions, snooze and
  temporary silence). Home automation is outside this exploration scope.
  The expanded macro/subfunctions, relative effort and exploration sequence are
  in the Obsidian `Cody Companion.md` product note, section
  "Product exploration priorities - 4 October 2026". These are ideas, not
  activated development or release commitments.
- [ ] Position Cody as a physical voice/status interface to the user's existing
  AI assistant. Explore host adapters for Codex, Claude, Hermes and OpenClaw;
  Your Dot is another candidate whose external-device connection remains
  unverified. Reuse supported assistant memory/reminder tools and define any
  local timer ownership explicitly. Preserve session identity and genuine
  acknowledgement/completion/error events; do not assume a universal API.
- [ ] Explore explicitly initiated offline voice recording on SD and a durable
  message outbox for later delivery. Define original capture time/clock validity,
  destination/session, bounded streaming buffers, interrupted-write recovery,
  retention/deletion, acknowledgement and uncertain-delivery reconciliation.
  Use adapter idempotency where supported, and review/expire stale action
  requests before replay. Missing/full/write-failed SD must surface a real
  error; no implicit formatting or internal-storage fallback. This message
  outbox is separate from the personality channel's no-queue policy.
- [ ] After UX selection, add a fixed-capacity reminder/deadline service with
  typed IDs, timezone/quiet preferences, snooze/missed-event policy and trusted
  canonical time. Schedule Deep-sleep wake through the existing power/timer
  owner; external RTC INT is not directly connected to ESP.
  [RTC](docs/cody/clock.md).
  Begin exploration with one-time reminders, countdown, snooze and a daily
  overview; named lists, quick notes and recurrence are later candidates.
- [ ] Explore explicit confirmed message/reply events over CCP after researching
  the experimental App Server channel above. Consider push-to-talk, preview,
  destination, Send/Cancel and draft recovery, retaining credentials on the PC
  and the existing wake word. Distinguish send, receipt and task completion;
  packet receipt never means Codex completed the work. Additional TTS triggers
  beyond the existing boot diagnostic need a separate product decision.
- [ ] Explore explicit focus timers and gentle break suggestions, with "later",
  break/resume and silence-until controls. Base the first flow on user-started
  sessions; optional PC activity needs a separate consent/integration decision
  and does not establish fatigue or health.
- [ ] Explore independent finite states for activity, rest/drink/meal reminders,
  mood and short reactions, with one stable latest-cue selector. Host session
  identity/activity/liveness must establish actual counted time; disconnection
  is Unknown. Consider one combined rest/drink invitation after a configurable
  active session, with snooze/acknowledgement/quiet rules. The 120-minute
  October 4 exploration was later superseded by the disabled model's
  60-minute screen-break default; delivery remains unimplemented. Preserve
  existing motion-driven stress and battery-driven fatigue causes. See
  [Personality behavior](docs/cody/behavior.md).
- [ ] Choose rig antialiasing/tessellation/dirty-region policy from target
  timings; extend validity-aware environmental composition while retaining
  shared palette, independent face channels and time-based animation.
- [ ] Review the opt-in PNG/triangle cloud with the user before native port:
  texture sampling/CPU, extreme motion/navigation clearance and visual approval.
  Retain the host-only 99-vertex/138-triangle study; the approved pearl scanline
  renderer is distinct. [Experiment](assets/cody/appearance/png-mesh/README.md).
- [ ] Resume cloud-volume/jelly-IMU default exploration only when activated.
  The billows-v5 height-field study was rejected; synthetic previews are not
  native performance evidence. Review final motion/clearance/assets before a
  requested target measurement.
  [Cloud evidence](docs/cody/rendering.md).

## Deferred wellbeing integration - 2026-10-04/05

The disabled native reminder model is described in
[behavior](docs/cody/behavior.md#dormant-wellbeing-logic). Its tested state logic
does not provide a host-session adapter, reminder timer, selector or visible UI.
Future activation should preserve the independent domains explored below:

| Domain | Candidate states | Explicit source |
| --- | --- | --- |
| Activity | Idle, Listening, Working, WaitingForUser, Break, Sleeping | Local interaction, host events and lifecycle |
| Rest need | Rested, Due, Deferred, Recovering | Defined active-session time and pause/resume |
| Drink reminder | Quiet, Due, Deferred, Acknowledged | Configured interval and acknowledgement |
| Meal reminder | Quiet, Due, Deferred, Acknowledged | Opt-in schedule and trusted time |
| Mood | Neutral, Content, Uneasy, Frustrated | Named bounded artistic reactions |
| Reaction | None, Affection, Surprise, Fear | Recognized expiring gesture/event |

Do not replace these with one mutually exclusive mood enum. Preserve motion-driven
stress and battery-driven fatigue; work-session rest needs get separate identities
and composition. Hunger/thirst/fatigue are authored reminder metaphors, without
establishing the person's health or continuous work from an open chat.

Start with one user-started session: start/pause/resume/end and Unknown on lost
host liveness. Retain identity and monotonic event order; do not count an unknown
gap on reconnect. A local offline timer needs its own explicit policy. Publish
new host facts through an owner before consumers use them; no sensor-side reads.

The selector emits one currently eligible latest cue, with no decorative backlog.
Keep true device/lifecycle facts first, explicit priorities, reaction expiry,
cooldown and enter/exit hysteresis. Persistent Due state is separate from queuing
icons: snooze sets one deadline, acknowledgement rearms only its configured cycle.
Suppress during dictation/quiet/sleep and reevaluate relevant overdue needs on return.
A caress does not complete a reminder or prove a drink/break happened.

The original implementation sequence was petting first (now present), then an
explicit session clock, combined rest/drink invitation with snooze/quiet, bounded
mood reactions, and finally opt-in meals/additional needs after UX review.
Use fixed state/deadlines and event or low-rate ticks, without a new framebuffer.
The historical 120-minute invitation was an exploration example, superseded by
the later disabled model's defaults; no user health recommendation is implied.

## Board and dependency graph

- [ ] Remove indirect FatFS/SDMMC dependencies from `esp_board_manager` when
  `sd: off`. BSP implementation exclusion is already present; dependency removal
  is explicitly deferred, not permission to change SD now.
- [ ] Split the Waveshare BSP into shared bus, display/touch, audio, SD and
  expander owners, then review other-board adapters/locks. YAML currently
  supports only this BSP, including the pending rectangular ES8311-input path.

## Power research

- [ ] Identify the actual AMOLED, microphones, speaker, added SD/battery and
  missing current/PMU specifications; establish LC76G command/rail limits before
  assigning Backup current.
- [ ] After Debug/Release validation, investigate USB-session-aware sleep with
  measured reconnect/current/wake behavior. Do not re-enable the known unsafe
  IDF 6.0.2 USB SOF hook without evidence.
- [ ] Measure `WIFI_PS_MAX_MODEM` versus `MIN_MODEM`, a 40 MHz CPU floor and
  CPU-domain Light-sleep power-down against latency, memory and stability.
- [ ] With target timing available, evaluate unleased Dimmed motion/rotation at
  2 Hz while preserving requested Full Snapshot cadence and 600 ms P99 wake.
- [ ] Evaluate battery-absent AXP2101 cell-ADC disable only with BAT_INSERT
  re-enable, conversion-settling and present/absent-current evidence.
- [ ] Future PCB: route PMU/expander IRQ to an ESP RTC GPIO for asynchronous PWR
  wake. Execute the deferred target evidence matrix: battery presence, RTC
  oscillator-stop, reboot metadata, serial/task wakeups and image-tagged current.
  [Low-power](docs/bsp/services/power-management.md#acquisition-policy).

## Maintenance

- [ ] After an explicit banner policy for existing leading comments, make
  `add-source-banner.ps1 -Apply` a staged multi-file transaction with rollback.
- [ ] Replace the pinned LVGL-adapter manifest with an upstream/versioned feature
  override for decoder/filesystem/FreeType dependencies; code exclusion alone
  does not remove managed checkouts.
- [ ] Make the Brookesia LVGL default feature bundle configurable without a
  private patch for unused image fonts, stdio and POSIX filesystems.
- [ ] Extract GNSS NMEA parsing only with requested host coverage for GGA/RMC,
  checksums, malformed/partial input and unchanged fix validity.
- [ ] Extract remaining hardware/audio/Leg/settings presenters one family at a
  time, preserving navigation, cached bindings and scoped connections.
  Logs/pairing are separate; this R7 maintenance is not a promised Cody RAM gain.
