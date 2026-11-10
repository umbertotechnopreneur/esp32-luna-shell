```diff
+ [######--------------------] 23% - CODY-NEXT design / implementation
```

3 of 13 required phases have reached static readiness: 00, 01 and 02.
Count 00-11 plus 14 equally; optional extensions 12/13 are excluded. Round the
percentage to the nearest integer; each phase fills two ASCII `#` cells. The
`diff` addition line renders green in supporting Markdown viewers and remains
readable as plain ASCII. This is phase progress, not effort or runtime acceptance.
Executed validation remains pending. Update this summary whenever phase status
changes in [TODO.md](TODO.md#cody-next---multi-chat-domain-and-muse). Update the phase
table and affected subtask markers at the same checkpoint.

# Deferred work

Activate a stable ID in [TODO.md](TODO.md) before starting deferred work.

## CODY-NEXT - Domain model and Muse implementation plan

This is the implementation plan for Cody's multi-chat domain model and Muse
character. Phase activation and remaining gates live in
[TODO.md](TODO.md#cody-next---multi-chat-domain-and-muse). Keep the table and
individual task markers here aligned with the available evidence at every
checkpoint. Check off a task when its specified deliverable is complete.
A completed design or source task does not pass an execution gate.
Activation does not authorize validation. Preserve unrelated work in this branch.

Phase identifiers `CODY-NEXT-00` through `CODY-NEXT-14` are permanent. Task
identifiers append a two-digit suffix, for example `CODY-NEXT-04.03`. Do not
renumber completed, deferred or cancelled tasks. Add new tasks with new suffixes.
Activate a phase by moving its status and remaining acceptance gates into
`TODO.md`, retaining a link to this specification instead of duplicating it.
Completion gates use `CODY-NEXT-NN-G01` for static readiness and
`CODY-NEXT-NN-G02` for applicable executed acceptance in phase 11 (or separately
authorized extension acceptance for 12/13). Phase 14 is foundational preparation
and is verified in phase 11. A static gate does not pass its
execution gate. For each phase, G01 requires its specified artifacts/tasks to be
complete and statically reviewed, with remaining execution gates listed. Its
Completion paragraph defines the result to establish for G02; design-only
phases 00-02 use document review, with downstream runtime claims still deferred.
Track implementation and validation separately: `planned`, `implementing`,
`implemented / validation pending`, `accepted`, or `blocked` with a concrete cause.

### Agreed product boundaries

- vNext has no backward-compatibility requirement for data, protocol, internal
  APIs or stored domain state. Update broker and device together; reject older
  incompatible peers explicitly. Do not add dual parsers, compatibility shims
  or automatic migrations solely to retain the old runtime. Keep useful screen
  sources/assets and their catalog identities available for reuse.
- The domain owns all admitted chats, turns, individual tool calls, subagents,
  external operations and questions independently of display visibility.
  Foreground selection is a separate local concern, not a filter on ingestion.
- Prepare the code for multiple desktop platforms and physical devices from
  the beginning. Keep portable domain/Muse behavior separate from PC OS services,
  transport and board capabilities. Physical board, display/input interface,
  optional services and build profile are independent choices. Portability
  preparation does not claim an unimplemented OS or board is supported.
- Keep the current Muse renderer, face style, indexed grid and native CODY2D
  integration. Evolve acting and expressions inside Muse. Do not introduce LVGL,
  a vector-face replacement, another raster backend or a renderer comparison.
- Show either a mascot-only scene or a UI-only scene. Existing UI screens keep
  their controls without the character. Tool icons belong beside tools on UI
  screens; semantic tool identity/category belongs in the domain model.
- Preserve existing useful screens and assets in a reusable coded catalog.
  Inactive navigation does not mean deleted implementation. Keep original IDs,
  pairing/recovery routes and explicit mappings for historical ID collisions.
- Retain VibeWare branding before the character. User-selected top-level IDs
  are 16 for Muse Home and 10 for Tools; reconcile older documentation without
  silently renumbering or overwriting a different historical screen 10.
- Quiet hours remain supported but disabled for this rollout. Sounds enablement,
  volume, audio ownership and cancellation continue to apply normally.
- Preserve the push-to-talk/manual recording already in the repository and its
  existing recording/delivery screens. Keep button-triggered recording through
  its current owner and supported profile; this is retained functionality, not
  a new speech engine or a change to hold/release semantics.
- Preserve Muse's round-display Wi-Fi text-entry design as a reference. Its
  LVGL controls are not imported into Cody. A native CODY2D adaptation is a
  separately staged UI extension, retaining the existing Wi-Fi pairing route.
- Start with the existing Codex broker and documented hooks. Other providers
  and cloud delivery use the same domain contract later, through explicit adapters.
- A pending question retains its identity until an observed semantic resolution.
  Navigation, a reaction, stale data, ticket expiry or loss of connection cannot
  silently answer or erase it. Native permission decisions remain on the PC.
- The perimeter's future meaning, new voice features, new hardware, font redesign
  and cloud deployment are outside this plan's first delivery.

### Source baseline and evidence limits

Starting points are `CODY_pixel_avatar.cpp`, `CODY_pixel_demo.cpp`,
`CODY_device_ui.cpp`, `CODY_device_effects.cpp`, `CODY_character_direction.cpp`,
the companion's `third_party/muse/`, the native/Windows simulators, and the plugin's
`hooks/hooks.json`, `mcp/ccp_activity.py` and `protocol/CCP1.md`.

The inspected source connects waking, idle and touch to Muse. Its current viewport
is 192 x 192 at (137,137); the display safe square is 320 x 320, not the current
avatar viewport. Two indexed frames including their palettes occupy 8,448 bytes.
The 40 ms interval is requested pacing, not measured performance. The PC registry
admits 64 sessions, while ACTIVITY/TECH project the followed chat. Recheck these
facts at activation because the worktree already contains local changes.

Reuse the existing display/touch owner, completed-frame retirement, RGB565 strips,
pairing, authenticated transport, semantic response acknowledgements and audio
owner. Existing character-direction code is a candidate for behavioral reuse,
not permission to restore the old vector character or mixed Home panels.

### Implementation ownership and reviewable artifacts

Paths below are relative to this worktree unless identified as external references.
New domain/store modules are proposed work, not existing functionality. Choose
their exact filenames in 01 while retaining the ownership boundaries below.

| Phases | Primary files/owners | Reviewable artifact |
| --- | --- | --- |
| 00 | docs/cody/navigation.md, layout.md, atlas sources, current scene presenters | Screen/source catalog, ID mapping, ownership and evidence inventory |
| 01 | New provider-neutral schema/domain types; existing device model and snapshot interfaces | Entity/state tables, ownership, bounds and transition examples |
| 02 | cody-desktop/plugins/cody-codex-plugin/protocol/CCP1.md; existing authenticated CCP owners | Logical JSON schema, wire specification, examples and recovery rules |
| 03 | Plugin mcp/ccp_activity.py, hook ingestion and hooks/hooks.json | Codex mapping table and complete broker registry |
| 04 | Existing semantic ticket/reply owners; proposed durable ledger | Question/answer transaction design, schema and failure outcomes |
| 05 | Canonical DeviceSnapshotService integration; proposed device domain/persistence owners | Bounded store, coherent queries and recovery implementation |
| 06 | cody-device/simulator/native, simulator/windows, scripts/device.ps1 implementations | Production-backed scenario driver and expected outcomes |
| 07 | CODY_pixel_demo.cpp, CODY_device_ui.cpp, behavior from CODY_character_direction.cpp | Domain-to-Muse mapping and interruptible direction state |
| 08 | CODY_pixel_avatar.cpp, third_party/muse, CODY_device_effects.cpp, existing settings/audio owner | Muse-only acting/effects, cue policy and disabled quiet setting |
| 09 | Existing companion scene/route presenters, navigation/layout and atlas sources | Separate routes, preserved screens/tool icons and recording entry |
| 10 | Plugin hook ingestion, generic domain stores and queries | Correlated child-agent/operation lifecycle |
| 11 | scripts/device.ps1, current source, TODO.md and done.md | Relevant authorized results and explicit remaining gates |
| 12 | Provider adapters and existing cloud owner when activated | One explicit additional-source integration |
| 13 | External Muse settings/keypad reference; new native text-entry presenter; existing Wi-Fi owner | Native keyboard specification/implementation and preserved provisioning |
| 14 | Portable domain/Muse core, existing PC OS and board/platform owners, native host adapters | Dependency boundaries, capability contracts and portability-ready source |

### Phase order and dependencies

| Phase | Deliverable | Dependencies | Done |
| --- | --- | --- | --- |
| CODY-NEXT-00 | Baseline, screen catalog and ownership map | None | 🟢 Accepted · document review |
| CODY-NEXT-01 | Domain schema, invariants and capacity policy | 00 | 🟡 Design reviewed · validation pending |
| CODY-NEXT-02 | Versioned JSON, wire mapping and synchronization rules | 01 | 🟡 Design reviewed · validation pending |
| CODY-NEXT-03 | Codex adapter and complete broker registry | 02, 14 | 🟡 In progress |
| CODY-NEXT-04 | Durable question and answer lifecycle | 02, 03 | ⚪ Planned |
| CODY-NEXT-05 | Device domain store, synchronization and recovery | 02, 03, 04, 14 | 🟡 In progress |
| CODY-NEXT-06 | Deterministic scenario driver and inspection tools | 01, 02; integrate after 05 | ⚪ Planned |
| CODY-NEXT-07 | Domain-to-Muse direction and pose transitions | 05, 06 | ⚪ Planned |
| CODY-NEXT-08 | Muse acting, particles and audio policy | 07 | ⚪ Planned |
| CODY-NEXT-09 | Separate mascot/UI routes and reusable screens | 00, 05, 07 | ⚪ Planned |
| CODY-NEXT-10 | Subagents and observable asynchronous operations | 03, 05, 06 | ⚪ Planned |
| CODY-NEXT-11 | Authorized final validation and hardware acceptance | 00-10 and 14 implemented and statically reviewed | ⚪ Planned |
| CODY-NEXT-12 | Additional source adapters | 11 accepted; separate source activation | 🟣 Optional · deferred |
| CODY-NEXT-13 | Muse-inspired native Wi-Fi keyboard | 00, 09, 14; design first, separate integration/acceptance | 🟣 Optional · deferred |
| CODY-NEXT-14 | Prepare portable core and platform/device adapters | 00, 01, 02; foundation before 03/05 | 🟡 In progress |

🟢 **Complete** for the stated review or deliverable. 🟡 **In progress** or
**validation pending**. ⚪ **Planned**. 🟣 **Optional and deferred**. The
phase table summarizes readiness, and the task markers below record individual
deliverables. See [TODO.md](TODO.md#cody-next---multi-chat-domain-and-muse)
for current work and remaining gates.

Main order: 00 -> 01 -> 02 -> 14 -> 03 -> 04 -> 05 -> 06 -> 07 -> 08 -> 09 -> 10 -> 11.
Phase 14 was assigned a new permanent ID rather than renumbering earlier phases;
its execution position is near the beginning, not after the optional extensions.
Author scenario definitions during 01/02 and the host driver during 03-05 where
interfaces are stable. Screen catalog work can proceed independently of broker
implementation; Muse behavior and UI projection can proceed independently after
07. Only parallelize edits with distinct ownership of files and interfaces.
Do not execute intermediate builds or tests as an implicit phase action.
Phase 13 can be designed from 00 independently of the domain work; its port is
not a dependency of the first Codex delivery or of phase 12.

### CODY-NEXT-00 - Preserve the baseline and reusable screens

**Outcome:** a recoverable implementation baseline and an unambiguous inventory.

- [x] 🟢 **CODY-NEXT-00.01** Read current repository instructions, worktree status,
  source/docs differences and outstanding acceptance gates. Identify local work
  that must be preserved; do not reset, clean, move or overwrite it.
- [x] 🟢 **CODY-NEXT-00.02** Inventory every retained screen: original code, stable
  symbolic key, title, source presenter/assets, entry/return routes, data owner,
  dependencies, active/inactive status and evidence type. Separate source,
  editorial preview and actual hardware observations.
- [x] 🟢 **CODY-NEXT-00.03** Reconcile 16/Muse Home and 10/Tools with the atlas and
  older IDs. Preserve historical codes in a versioned namespace when necessary;
  no ambiguous numeric lookup and no reuse of retired codes for unrelated screens.
- [x] 🟢 **CODY-NEXT-00.04** Retain original reusable screen sources/assets with
  explicit catalog references. Prefer retaining them in place and removing only
  navigation exposure. If a physical move is necessary, preserve dependencies
  and imports before changing routes. An archive must remain useful source,
  not screenshots alone or an unmaintained copy of the application.
- [x] 🟢 **CODY-NEXT-00.05** Map owners for network ingress, canonical observations,
  domain mutation, display, touch, settings, persistence and audio. Record lock,
  buffer and callback lifetimes before extending snapshots.
- [x] 🟢 **CODY-NEXT-00.06** Identify the existing two-dot/swipe implementation or
  its missing pieces, font-source availability and simulator dependencies.
  Record gaps without regenerating assets or running the simulator.
- [x] 🟢 **CODY-NEXT-00.07** Include existing manual recording/push-to-talk and voice
  pages in the retained catalog, with actual implemented states distinguished
  from future atlas illustrations. Record the Record voice -> DeviceButton ->
  requestVoiceInput path, profile availability, current destination binding,
  cancellation and delivery behavior; preserve screen IDs 28-30 where assigned.
- [x] 🟢 **CODY-NEXT-00.08** Preserve references to Muse's `muse_settings_ui.c`
  (`build_text_page`, `build_keyboard`, `on_wifi_pass`) and `muse_keypad.c`
  (`muse_keypad_create`). The source selects its large-key keypad below 2 inches
  or on narrow panels, and a full keyboard otherwise; both paths here use LVGL.
  Record the round-layout and text-editing behavior for phase 13.
- [x] 🟢 **CODY-NEXT-00.09** Inventory platform assumptions in the PC broker, device
  domain, Muse bridge and UI: OS paths/processes/credentials, ESP-IDF/BSP types,
  display size/shape, touch/buttons, audio channels, storage and PSRAM. Map existing
  adapter boundaries and reusable Muse device-descriptor ideas for phase 14.

**Completion:** all useful screens have recoverable source and stable identity;
the ID collision policy and ownership map are explicit. No behavioral or physical
acceptance is claimed by this inventory. Update maintained navigation/layout
documentation when routes actually change, not as if proposed routes already run.

### CODY-NEXT-01 - Specify the domain model

**Outcome:** one presentation-independent schema with explicit invariants.

- [x] 🟢 **CODY-NEXT-01.01** Specify typed IDs and relationships for the objects
  below. IDs survive reordering, reconnects and broker epochs; namespace them by
  stable source/provider identity so future sources cannot collide.

  | Object | Required relationships and responsibility |
  | --- | --- |
  | Source | Provider identity, broker epoch, capabilities, link health and coverage |
  | Chat | Source, stable chat ID, observed title/lifecycle and current-turn reference |
  | Turn | Chat, execution state, outcome, wait reason/targets and source evidence |
  | ToolDefinition | Namespaced tool ID, original name, display label and semantic category |
  | ToolCall | Unique invocation ID, turn, tool definition and executing agent when known |
  | AgentTask | Subagent ID, owning turn and optional parent agent |
  | ExternalOperation | Process/job/service identity and originating call when observable |
  | Question | Stable request ID, trustworthy chat/turn association and semantic lifecycle |
  | AnswerAttempt | Question ID, immutable selected choice and delivery correlation |

- [x] 🟢 **CODY-NEXT-01.02** Specify observation metadata: original source age/time
  domain, revision, validity, coverage, last error and provenance. Unknown,
  absent, zero, stale and unsupported must remain distinct. A transport heartbeat
  never refreshes old source evidence.
- [x] 🟢 **CODY-NEXT-01.03** Separate chat lifecycle, turn execution, turn outcome,
  connection and observation freshness. Define legal transitions and contradictory
  event handling. Stop/reply availability does not prove success; Interrupt does
  not close the chat; tool/subagent completion does not complete the parent turn.
- [x] 🟢 **CODY-NEXT-01.04** Define waits for user/tool/agent/service/unknown, including
  simultaneous dependencies. A user question may coexist with running tools.
  Only mark the parent as waiting on a child when correlation supports it.
- [x] 🟢 **CODY-NEXT-01.05** Separate ToolDefinition from ToolCall. Define stable
  categories such as command, file_read, file_edit, search, browser, agent and
  unknown; keep artwork, screen coordinates and animation names out of the domain.
  Preserve unknown tools instead of guessing their meaning from arbitrary inputs.
- [ ] 🟡 **CODY-NEXT-01.06** Define bounded stores for all 64 currently admitted chats,
  all admitted in-flight calls and unresolved questions. Budget active records,
  terminal history, dictionaries, indexes, tombstones, synchronization staging and
  persistence scratch space. Evaluate up to 4,096 active-call records against
  actual structures and PSRAM availability; do not promise that footprint in advance.
  The structure-based budget is written. Compiled sizes and board PSRAM measurements remain open.
- [x] 🟢 **CODY-NEXT-01.07** Specify overflow/backpressure and history eviction.
  Never evict unresolved questions or active calls silently. Negotiate capacities,
  expose partial coverage and omitted counts, and reject excess admission with
  a specific error when completeness cannot be maintained. Keep bounded terminal
  history locally and longer history on the PC.
- [x] 🟢 **CODY-NEXT-01.08** Define SelectionState separately: local foreground chat,
  auto-follow/manual selection mode and return context. Default may follow a
  genuine prompt; manual inspection remains pinned until explicitly released.
  Local selection does not claim native app focus or change a voice destination.
- [x] 🟢 **CODY-NEXT-01.09** Write transition examples for two chats, overlapping tool
  calls, a background question, out-of-order events and a source restart. Specify
  summaries as derived queries, never competing mutable copies of domain truth.
- [x] 🟢 **CODY-NEXT-01.10** Keep an original Turn record while active calls, questions
  or child agents still reference it, even when the chat starts another turn.
  Route late events to that exact turn; never rewrite them onto current_turn_id.
- [x] 🟢 **CODY-NEXT-01.11** Keep domain types and transition logic free of Windows,
  ESP-IDF, board pin, display-coordinate and microphone-channel assumptions.
  Define typed capability/limit inputs independently of semantic entity state.
  A board without a microphone still represents all chats, tools and questions.

**Completion:** every object has one authoritative mutation path, bounded lifetime,
identity rules and explicit unknown/error states. UI visibility is absent from
ingestion decisions. Native approvals are distinct from answerable questions.

### CODY-NEXT-02 - Define JSON and synchronization

**Outcome:** a provider-neutral logical contract and an implementable wire mapping.

- [x] 🟢 **CODY-NEXT-02.01** Define `cody.state/1` envelopes with stable source_id,
  epoch, revision, kind and coverage. Define full snapshots, deltas with
  base_revision, explicit removals/tombstones and recovery requests. Specify
  integer widths, ID/text bounds, ASCII policy, enum validation and error replies.
- [x] 🟢 **CODY-NEXT-02.02** Define coherent entity references and atomic admission.
  A snapshot cannot expose a turn whose chat is missing or a call whose tool
  definition is unresolved without an explicitly modeled incomplete reference.
  Missing entities in partial snapshots never imply closure/deletion.
- [x] 🟢 **CODY-NEXT-02.03** Specify duplicate/late revision rejection, missing-delta
  recovery and epoch changes. Preserve semantic identities across epochs.
  Separate link liveness from observation age and prevent deadline renewal by
  retransmission. Bind epoch adoption to the current authenticated association
  handshake; random epoch IDs are not ordered. Reject retired epochs so delayed
  packets cannot roll back the model or cause resync oscillation. Define behavior
  when clocks are invalid or reset.
- [x] 🟢 **CODY-NEXT-02.04** Negotiate protocol version, capacities and capabilities.
  Retain explicit pairing/authentication guarantees, without preserving the old
  wire format. Reject old ACTIVITY/TECH peers as unsupported for vNext; do not
  implement a compatibility projection or reinterpret them as a full registry.
- [x] 🟢 **CODY-NEXT-02.05** Map logical JSON to a bounded authenticated versioned wire contract.
  Specify maximum packet size and application-level snapshot chunks with common
  transfer ID, epoch, revision, chunk count and completion integrity. Account for
  reordering, duplicate/missing chunks, timeout, retries, authenticated endpoint
  changes and aggregate memory limits; do not rely on IP fragmentation.
- [x] 🟢 **CODY-NEXT-02.06** Keep the last coherent domain view until a full replacement
  is admitted. Bound staging time/memory and expose resyncing/stale status. Commit
  one generation atomically; cancellation or allocation failure retains the old
  generation with the original error. Define how updates arriving during a
  snapshot are retained or replayed from its exact revision, with bounded backlog
  and explicit restart/backpressure when that backlog overflows.
- [x] 🟢 **CODY-NEXT-02.07** Specify initial synchronization without replaying old
  celebration/sound events. Define event identities/watermarks separately from
  state revisions, including reconnect and post-reboot behavior.
- [x] 🟢 **CODY-NEXT-02.08** Document the replacement protocol, delivery and ownership
  rules plus bounded examples; link its explicit version boundary from CCP1.md.
  Require matching vNext peers, without an old-protocol fallback or migration.
  Describe authentication separately from payload
  confidentiality; do not claim the existing HMAC transport encrypts metadata.
- [x] 🟢 **CODY-NEXT-02.09** Negotiate semantic/transport capabilities and capacities
  without identifying behavior solely by board or OS name. Use explicit wire
  sizes/byte order; never serialize platform structs, native pointers or local
  monotonic timestamps as cross-device identities. Unsupported optional features
  remain explicit and cannot masquerade as successful empty results.

**Completion:** sender and receiver can independently implement the same examples,
loss/recovery rules and maximum memory budget. No protocol field asks the device
to select a facial expression or perform an unspecified privileged action.

### CODY-NEXT-03 - Build the Codex adapter and broker registry

**Outcome:** actual Codex observations populate the complete domain registry.

- [ ] 🟡 **CODY-NEXT-03.01** Separate provider translation from ActivityStore's generic
  lifecycle/storage responsibilities. Replace followed-chat-only consumers with
  domain queries as they are integrated; no legacy compatibility projection is
  required. Reuse current hooks rather than install a second competing observer.
- [ ] 🟡 **CODY-NEXT-03.02** Verify documented native event semantics at implementation
  time and map admitted hooks to domain facts. Handle Stop continuation, resumed
  sessions, Interrupt, compaction and SessionEnd without inferred task success.
  Distinguish observed session lifecycle from visibility of a native application tab.
- [ ] 🟡 **CODY-NEXT-03.03** Preserve each invocation's tool_use_id and reliable turn/
  session association. Normalize ToolDefinition once and create separate ToolCall
  instances. Do not ingest command contents merely to choose a decorative icon.
- [ ] 🟡 **CODY-NEXT-03.04** Make progress, tools, turns and questions from background
  chats update their own records. Foreground selection cannot suppress their
  publication or be stolen by routine background events.
- [ ] 🟡 **CODY-NEXT-03.05** Handle late, duplicate and conflicting native observations
  before assigning outbound revisions. A broker sequence orders admitted evidence;
  it cannot recreate missing provider events. Preserve uncertainty when the
  source lacks stable event IDs, completion evidence or correlation.
- [ ] ⚪ **CODY-NEXT-03.06** Publish complete registry snapshots and bounded deltas;
  include source ages and coverage. Add backpressure and meaningful rejection
  results. Keep public plugin operations and schema changes explicit.
- [ ] ⚪ **CODY-NEXT-03.07** Reconstruct known registry metadata after restart from
  its own persisted evidence or supported source APIs. Mark prior active states
  unverified until reconciled. Do not replay hooks, fabricate fresh prompts or
  infer focus/activity from undocumented transcript formats.

**Completion:** simultaneous chats/tools have independent identities and revisions;
the old followed-chat rule is a projection rather than the broker's data boundary.
Real hook delivery remains a later authorized acceptance item.

### CODY-NEXT-04 - Preserve questions and response delivery

**Outcome:** questions survive presentation changes and ambiguous delivery.

- [ ] ⚪ **CODY-NEXT-04.01** Separate semantic Question identity from an expiring
  transport ticket and its recipient capability. Define open, resolved,
  cancelled, explicitly expired and unresolved/unavailable states. A local TTL
  expiration disables that reply path without inventing semantic resolution.
- [ ] ⚪ **CODY-NEXT-04.02** Establish chat/turn association from trustworthy broker
  invocation context. The existing caller-declared context label is not identity.
  When association is unavailable, keep an unassociated question visible in the
  global attention set; never attach it to the current foreground chat by guess.
- [ ] ⚪ **CODY-NEXT-04.03** Specify AnswerAttempt transitions: local Send admission,
  broker_received, recipient_confirmed and uncertain/rejected outcomes. Retain
  immutable choice IDs and the current exact-answer acknowledgement discipline.
  Repeated delivery of one attempt is idempotent; a conflicting choice is rejected.
- [ ] ⚪ **CODY-NEXT-04.04** Implement a bounded durable broker ledger. Persist an
  accepted answer before reporting durable receipt; preserve original failure
  if storage fails. Persist terminal outcomes/tombstones and recipient correlation
  under the existing credential owner. Do not store authorization in public snapshots.
- [ ] ⚪ **CODY-NEXT-04.05** Define provider-supported reattachment after broker restart.
  If the recipient cannot safely resume polling, expose delivery unavailable or
  unconfirmed. Do not revive expired capabilities, replay an answer as a new
  action or manufacture recipient confirmation from the persisted ledger.
- [ ] ⚪ **CODY-NEXT-04.06** Track all admitted open questions, present one at a time
  and preserve the others. Define deterministic ordering, inspection/dismissal
  distinct from resolution, queue capacity and overflow behavior.
- [ ] ⚪ **CODY-NEXT-04.07** Preserve recipient delivery and device presentation closure
  as separate acknowledgements. Repeated terminal state remains recoverable
  until acknowledged; a late ACK cannot reopen or rewrite a terminal local result.
- [ ] ⚪ **CODY-NEXT-04.08** Keep native PermissionRequest advisory and PC-only.
  Opening its notice or touching the mascot cannot authorize a native approval.
- [ ] ⚪ **CODY-NEXT-04.09** Serialize answer-versus-cancel races and competing answers
  from multiple devices around one durable decision. Define first-accepted choice,
  transaction boundaries and crash outcomes before/after persistence and each ACK.
  Retain sufficient terminal tombstones during compaction to reject late replay.

**Completion:** every pending question and admitted answer attempt has a stable
identity through disconnect/restart; loss of evidence becomes explicit uncertainty.
No deadline, stale snapshot, Back action or new foreground chat silently clears it.

### CODY-NEXT-05 - Implement the device domain store and recovery

**Outcome:** a bounded complete local model independent of UI routes.

- [ ] ⚪ **CODY-NEXT-05.01** Implement typed entity stores and indexes with one mutation
  owner. Fit budgets from 01 before adding snapshot fields; keep large pools and
  staging buffers off callback/task stacks and use PSRAM only under valid access
  conditions. Specify pointer/handle lifetime across generation changes.
- [ ] ⚪ **CODY-NEXT-05.02** Integrate the authenticated acquisition owner and canonical
  DeviceSnapshotService publication. Publish narrow synchronized domain views or
  generation handles, not full registry copies in every sensor snapshot. UI and
  mascot readers never acquire hardware or maintain competing network caches.
- [ ] 🟡 **CODY-NEXT-05.03** Implement snapshot/delta admission, referential integrity,
  ordering, transfer cleanup, explicit freshness and partial-coverage reporting.
  Updates to invisible chats follow exactly the same rules as foreground updates.
- [ ] ⚪ **CODY-NEXT-05.04** Expose bounded queries for selected chat, all chats,
  active calls, pending questions and aggregate attention. Query results are
  disposable projections, not UI-owned domain state.
- [ ] ⚪ **CODY-NEXT-05.05** Persist minimal unresolved question/answer records and
  useful selection references through the existing storage owner. Define schema,
  integrity, atomic replacement, retention and write coalescing. Do not write
  animation ticks, heartbeats, every tool update or whole transcripts to flash.
- [ ] ⚪ **CODY-NEXT-05.06** Restore persisted facts as historical/unverified until
  reconciled. Preserve unknown outcomes, storage errors and data from a missing
  source. A changed pairing/source cannot inherit another peer's questions or
  response authority; partition or retire association explicitly.
- [ ] ⚪ **CODY-NEXT-05.07** Handle standby, reboot and clock discontinuity separately.
  Semantic deadlines/freshness continue according to their clock contract;
  paused animation time cannot extend a response deadline or make old work fresh.
- [ ] ⚪ **CODY-NEXT-05.08** Add bounded diagnostics for counts, bytes, pool occupancy,
  dropped/rejected admission, revision, resync and original errors. Keep diagnostic
  data out of ordinary product flows except when the user needs an incomplete-data status.

**Completion:** the model can represent two working chats and a question while
either presentation is hidden; all readers observe a coherent generation. Memory
estimates exist, while actual stack/heap measurements remain pending phase 11.

### CODY-NEXT-06 - Author deterministic scenarios and inspection

**Outcome:** repeatable domain and animation inputs without depending on live work.

- [ ] ⚪ **CODY-NEXT-06.01** Build an explicitly synthetic host scenario driver with
  injectable source/animation clocks and deterministic randomness. Feed the same
  domain reducer and Muse integration used by firmware; do not duplicate behavior
  in a Python-only mock. Keep synthetic data out of production snapshots.
- [ ] ⚪ **CODY-NEXT-06.02** Provide host controls for start/pause, single step, seek by
  replay from a known initial state, speed and scenario selection. Reset Muse's
  retained clocks/random state deterministically rather than passing time backwards.
- [ ] ⚪ **CODY-NEXT-06.03** Expose a host inspector for entity IDs/relations, coverage,
  freshness, question/answer lifecycle, requested/visible pose and effect identity.
  This is a development tool, not a facial-channel screen on the device.
- [ ] ⚪ **CODY-NEXT-06.04** Author the scenario matrix below with expected domain,
  presentation and audio outcomes. Cover externally meaningful invariants instead
  of tests that merely repeat implementation steps.
- [ ] ⚪ **CODY-NEXT-06.05** Integrate scenario entry points under the public
  `scripts/device.ps1` workflow. Document exact runnable commands when implemented.
  Do not invent flags in advance or execute a suite without its required authorization.

| Scenario ID | Sequence | Required result |
| --- | --- | --- |
| CODY-SC-01 | A and B work with overlapping calls to the same tool | Separate calls; selected A unchanged |
| CODY-SC-02 | B asks while A is selected | B and question update; global attention; return restores A |
| CODY-SC-03 | Touch during work, then question during reaction | Latest requested pose wins; no lost question |
| CODY-SC-04 | Disconnect before Send | Question retained; unavailable reply path |
| CODY-SC-05 | Disconnect after broker receipt, before recipient ACK | Immutable attempt; no false Reply sent |
| CODY-SC-06 | Duplicate/late updates and lost delta | No repeated cues or rollback; coherent resync |
| CODY-SC-07 | Restart broker with an open question and accepted answer | Same IDs; historical state; explicit reconciliation |
| CODY-SC-08 | Reboot device with unresolved question | Restore reference; no automatic answer/replay |
| CODY-SC-09 | Ticket expires while semantic question stays open | Answerability changes; identity remains |
| CODY-SC-10 | Tool finishes after Interrupt or Stop | No parent resurrection or invented success |
| CODY-SC-11 | Tool returns an active process; child agent finishes | Separate operation/agent/turn outcomes |
| CODY-SC-12 | Pools or snapshot staging reach capacity | Explicit coverage/error; no silent active-record eviction |
| CODY-SC-13 | Multiple questions; answer/cancel one | Exact identity closes; others retained |
| CODY-SC-14 | Initial sync, stale evidence and repeated reconnects | No historical cue replay or stale-as-current work |
| CODY-SC-15 | Long idle, standby/wake, interrupted transitions | Calm continuity; no backlog of missed reactions |
| CODY-SC-16 | Sound disabled and quiet hours initially disabled | Policy respected; disabled quiet hours suppress nothing |
| CODY-SC-17 | Corrupt storage, changed pairing, missing correlation | Original errors; no guessed identity/authorization |
| CODY-SC-18 | New turn while an old call/question is unresolved | Original turn retained; late update reaches exact owner |
| CODY-SC-19 | Two devices answer while requester cancels | One serialized durable outcome; no conflicting success |
| CODY-SC-20 | Delayed old epoch and updates during snapshot | No rollback/oscillation; bounded coherent recovery |
| CODY-SC-21 | Existing Record voice button, cancel, stop/send and route return | Existing recording owner/profile preserved; no false delivery |

**Completion:** scenarios, expected outcomes and the host driver are implemented
and statically reviewed. Passing execution and visual acceptance remain explicit
phase-11 gates; authoring fixtures does not establish parity or hardware behavior.

### CODY-NEXT-07 - Connect domain state to Muse direction

**Outcome:** Muse responds to real domain facts with interruptible continuity.

- [ ] ⚪ **CODY-NEXT-07.01** Define a bounded CharacterIntent derived from the domain:
  selected-chat activity, global pending-question attention, uncertainty and an
  eligible semantic reaction. Keep provider names and raw tool events out of Muse.
- [ ] ⚪ **CODY-NEXT-07.02** Implement requested pose, visible pose, transient reaction
  and ambient state separately. Continue updating the base intent during a touch
  reaction; resume the current intent rather than a stale saved Idle pose.
- [ ] ⚪ **CODY-NEXT-07.03** Define priority and interruption rules. A pending question
  remains recognizable through routine work/completion/touch; an unrelated tool
  result cannot celebrate over unresolved user attention. Handle multiple questions
  deterministically without cycling through a backlog of decorative reactions.
- [ ] ⚪ **CODY-NEXT-07.04** Map rest, work, external wait, user attention, reply-ready,
  observed error, interruption and wake to Muse behavior. Extend Muse's own mode/
  pose parameters if necessary; do not imply actual listening/speaking without
  an actual audio source. Work completion and successful outcome stay distinct.
- [ ] ⚪ **CODY-NEXT-07.05** Interpolate from the currently visible pose when interrupted.
  Use neutral bridging only where needed, preserve blink/gaze continuity and
  define minimum dwell/coalescing so tool bursts do not flicker between expressions.
- [ ] ⚪ **CODY-NEXT-07.06** Keep artistic clocks independent of domain deadlines.
  Define hide/standby/wake behavior, pause without accumulated cue replay and safe
  long-frame handling. Respect Muse's retained state and single renderer ownership.
- [ ] ⚪ **CODY-NEXT-07.07** Replace the demo's wake/idle-only decision with the intent
  integration while retaining completed-frame damage comparison and retirement.
  Prevent offline or sending states from silently removing unresolved attention.

**Completion:** each supported domain situation has an explicit Muse mapping;
transition scenarios have expected poses and effect identities. The renderer
continues to use Muse/CODY2D with no alternate face pipeline.

### CODY-NEXT-08 - Refine Muse acting, particles and audio

**Outcome:** a calm, expressive companion with event-driven accents.

- [ ] ⚪ **CODY-NEXT-08.01** Preserve the current Muse face language and evolve its
  gaze, lids, mouth and brows within that renderer. Keep the body relatively calm;
  tune breathing and small sway before adding larger gestures. Reuse ideas from
  existing direction code without introducing its old graphical backend.
- [ ] ⚪ **CODY-NEXT-08.02** Keep blink, gaze and ambient movement on independent,
  bounded schedules. Use irregular pauses and occasional small variations;
  avoid constant whole-body motion or unattended repeated celebration.
- [ ] ⚪ **CODY-NEXT-08.03** Start with authored timing candidates: 120-220 ms blink,
  200-450 ms facial transitions, 0.8-1.5 s wake, current 1.6 s tap reaction and
  final 400 ms fade. These are tuning inputs, not accepted hardware results.
- [ ] ⚪ **CODY-NEXT-08.04** Add a small bounded accent set within Muse: wake sparkles,
  a brief question symbol, a restrained reply-ready accent and touch particles.
  Start with 0.6-1.2 s effects, limited concurrency and behind/in-front passes.
  A persistent question uses a persistent recognizable expression after the
  accent disappears. No inferred task-success celebration.
- [ ] ⚪ **CODY-NEXT-08.05** Trigger existing Cody audio through the effects/audio
  owner on admitted semantic transitions, never from raster calls. Deduplicate
  cues by event/attempt identity; preserve mute, volume, capture/timer priorities,
  cancellation and no replay after reconnect/wake.
- [ ] ⚪ **CODY-NEXT-08.06** Keep quiet-hours configuration supported but apply the
  requested disabled state through the settings owner for this rollout. Default
  only absent keys; preserve a valid saved schedule and report persistence errors
  rather than disguising an update as first-boot defaulting. Disabled quiet hours
  must never suppress a cue. On an already configured device use the existing
  authenticated CLOCK settings `set` with `enabled=0` when configuration work
  is activated; preserve the schedule/timezone and verify the observed result.
  Saved enabled settings survive firmware updates, so a new default or hardcoded
  audio bypass is insufficient. Do not automatically enable a night schedule.
- [ ] ⚪ **CODY-NEXT-08.07** Bound dirty regions, palette changes and effect extents.
  Keep indexed frame ownership until all strips retire; include removed particles
  in damage. Treat 192/256/320-pixel avatar sizing as a later explicit layout
  choice within Muse, not an implicit renderer change.

**Completion:** timings and priorities are documented, buffers/effects are bounded,
quiet hours are disabled by confirmed settings behavior, and audio remains owned
outside the renderer. Visual/audible quality and throughput await phase 11.

### CODY-NEXT-09 - Separate presentation routes and reuse screens

**Outcome:** useful existing UI survives, while Muse has its own screen.

- [ ] ⚪ **CODY-NEXT-09.01** Implement mutually exclusive mascot/UI scene kinds.
  Remove the character from reused UI presenters without deleting their controls,
  data paths or original catalog references. Preserve pairing/recovery usability.
- [ ] ⚪ **CODY-NEXT-09.02** Retain VibeWare -> Muse Home (16), horizontal navigation
  between Home and Tools (10), and BOOT access to Tools. Define two dots strictly
  as navigation chrome, not data widgets. Confirm their placement/visibility
  against the mascot-only requirement before final layout; leave rim semantics open.
- [ ] ⚪ **CODY-NEXT-09.03** Resolve gesture ownership on the completely displayed
  frame: horizontal swipe navigates; a short tap with pending attention opens
  the selected pending question; without attention it triggers the Muse reaction.
  A swipe or wake touch cannot also send a choice or trigger an underlying action.
- [ ] ⚪ **CODY-NEXT-09.04** Provide UI-only access to all chats, selected-chat/turn
  detail, tool calls and pending questions. Map new routes only after the catalog
  audit; assign permanent unique codes then. Retain 26/27/31 question/delivery
  meanings and existing pairing codes, including review-before-Send and Back.
- [ ] ⚪ **CODY-NEXT-09.05** Show tool icons via a presentation-owned category/artwork
  catalog; retain textual names, independent call status and unknown-tool fallback.
  Read model projections rather than maintaining UI-owned tool/session records.
- [ ] ⚪ **CODY-NEXT-09.06** Keep background questions reachable on UI-only screens
  through an attention entry without replacing an in-progress choice/review or
  changing the foreground chat. Opening a question retains caller route, selected
  chat, scroll and stable question ID; Back restores them without resolving it.
- [ ] ⚪ **CODY-NEXT-09.07** Reuse counters/statistics, Connections, settings and recovery
  screens through Tools. Child Back returns to its actual caller; Home returns to
  the mascot. Retain last statistics page without converting it into the mascot.
- [ ] ⚪ **CODY-NEXT-09.08** Update navigation/layout/atlas sources to the implemented
  routes, replacing obsolete mixed-panel descriptions. Preserve historical screen
  codes and evidence. Reconcile text completion, touch admission, circular bounds,
  full Back target and 0/90/180/270-degree rotation.
- [ ] ⚪ **CODY-NEXT-09.09** Preserve the existing push-to-talk/manual Record voice
  control and its recording, cancellation and delivery pages as UI-only routes.
  Reuse VoiceInputTrigger::DeviceButton and requestVoiceInput through the current
  voice owner. Preserve actual stop/send behavior, profile/capture availability,
  fixed destination identity and truthful queue/delivery outcomes. Do not replace
  the recording implementation, remap PWR/BOOT as recording triggers or introduce
  hold-to-record unless separately specified. Keep 09.02's BOOT navigation and
  the existing active-recording precedence: recording retains its Cancel/Send
  controls before BOOT may open Options/Tools. Preserve existing gestures only
  on routes where admitted.
- [ ] ⚪ **CODY-NEXT-09.10** Keep the Wi-Fi keyboard reference accessible by catalog
  code and link its proposed route to phase 13. Existing BLE/QR provisioning and
  recovery stay available while native text entry is designed.

**Completion:** every active route has an entry and return path, every inactive
useful screen is recoverable by catalog code, and no normal UI scene contains
the character. Source/preview/hardware evidence remains clearly distinguished.

### CODY-NEXT-10 - Add subagents and asynchronous-operation evidence

**Outcome:** richer execution detail without guessing parent/child semantics.

- [ ] ⚪ **CODY-NEXT-10.01** Add documented SubagentStart/SubagentStop support across
  hook manifest, input validation, normalization, registry and device storage.
  Keep hook review/trust in the native supported flow; never rewrite trust state.
- [ ] ⚪ **CODY-NEXT-10.02** Preserve parent chat, parent turn, agent ID and nested
  parent-agent relation only when actually supplied. Keep missing relationships
  explicitly incomplete. Completion of one child never completes the root turn.
- [ ] ⚪ **CODY-NEXT-10.03** Distinguish tool return from persistent process/job exit.
  Ingest supported operation IDs and lifecycle evidence; link polling to the
  existing operation. If the provider exposes no reliable outcome, keep unknown
  rather than inferring success from a returned tool invocation.
- [ ] ⚪ **CODY-NEXT-10.04** Handle late child completion after parent interruption,
  stop or closure, orphaned events, restart and stale leases. Expired freshness
  does not invent a cancelled/completed child.
- [ ] ⚪ **CODY-NEXT-10.05** Extend domain queries, UI tool/agent detail and scenario
  inputs; keep the same Muse intent vocabulary. More children must not generate
  an unbounded animation/sound queue or change the user's foreground selection.

**Completion:** subagents/operations have their own identities, lifetimes and
uncertainty. Missing provider capability is displayed as unavailable, not hidden
behind a fabricated complete execution tree.

### CODY-NEXT-11 - Final validation and physical acceptance

**Outcome:** evidence for the integrated source, with unverified behavior explicit.

- [ ] ⚪ **CODY-NEXT-11.01** Complete implementation and static source review before
  validation. Reconcile requirements, interfaces, ID references, memory estimates,
  error propagation and preservation of unrelated work. Identify exact commands
  through `scripts/device.ps1`; obtain the authorization required for each run.
- [ ] ⚪ **CODY-NEXT-11.02** Run authorized domain/protocol/ledger scenarios covering
  CODY-SC-01 through 21, malformed/incompatible payloads, referential integrity,
  ordering, snapshot chunk loss and persistence failure. Assert semantic outcomes
  and bounded resources rather than only pixel snapshots.
- [ ] ⚪ **CODY-NEXT-11.03** Run authorized host playback and UI verification using
  current source. Inspect long idle, rapid transitions, question readability,
  removed-effect damage, deterministic restart, gesture arbitration and route
  restoration. Host results do not establish target performance or audible output.
- [ ] ⚪ **CODY-NEXT-11.04** Run the authorized final firmware build/size checks under
  repository limits. Verify native Cody composition without LVGL, flash/PSRAM
  budgets, generated input consistency and compiled stack frames where applicable.
  A compiler failure is fixed at its cause; reruns follow the same authorization rules.
- [ ] ⚪ **CODY-NEXT-11.05** With separate flash/device authorization, verify actual
  VibeWare/Muse handoff, genuine Codex hook delivery, multiple chats/tool calls,
  question delivery/recovery and all retained setup/recovery routes. Observe the
  exact running image and paired plugin revision; synthetic traces are not live evidence.
- [ ] ⚪ **CODY-NEXT-11.06** Measure composition/damage/transfer time separately,
  completed-frame FPS, missed cadence, touch latency, deepest stack margins,
  internal heap, PSRAM and synchronization peak allocation. Include idle, particles,
  all supported rotations, active networking and a catalog at configured capacity.
  Target requested 40 ms pacing without claiming 25 completed FPS until measured.
- [ ] ⚪ **CODY-NEXT-11.07** Listen to admitted cues, mute/volume behavior and disabled
  quiet hours. Verify enabled quiet-hours behavior only through an explicit test
  setting, then restore the requested disabled state with confirmed persistence.
  Check standby/wake and no historical cue replay.
- [ ] ⚪ **CODY-NEXT-11.08** Resolve failures or retain named unaccepted gates. Update
  current docs and the single latest validation summary in done.md; do not create
  historical validation logs/docs. Record remaining work under its stable phase ID.
  Commits/publication follow repository authorization; no push, PR or release is
  implied by accepting this plan or by completing local validation.
- [ ] ⚪ **CODY-NEXT-11.09** Check retained push-to-talk wiring and UI routes as part
  of the integration. Actual microphone capture and speech-to-Codex behavior
  remain separate physical voice gates under VOICE-01; execute only when those
  trials are explicitly activated. Preserving an existing path does not establish
  model-side speech understanding or a working transcript-review page.
- [ ] ⚪ **CODY-NEXT-11.10** Validate phase-14 portability boundaries through authorized
  host/current-board compositions: same domain/Muse behavior, declared capacities,
  geometry adapters, unavailable optional input/audio and clean adapter ownership.
  Distinguish a simulated capability case from support for a physical board.
  Linux/macOS or new-board support requires its own implemented adapter and real
  acceptance; current-host success does not certify those platforms.

**Acceptance:** no lost unresolved question, false delivery/success, ambiguous
entity identity or silent active-record eviction in the admitted scenarios; no
mascot/UI mixing; all retained routes remain usable; measured resource and timing
results support the chosen capacity and appearance. Open failures remain visible.

### CODY-NEXT-12 - Extend to additional sources

**Activation:** phase 11 accepted and a specific provider/cloud source requested.
This phase is not required for the first Codex delivery.

- [ ] 🟣 **CODY-NEXT-12.01** Verify the new source's documented public integration,
  lifecycle events, IDs, response acknowledgements and restart behavior.
- [ ] 🟣 **CODY-NEXT-12.02** Implement a source adapter to the existing domain schema
  and declare capabilities/coverage. Add provider-specific facts only when the
  generic contract cannot express them without losing meaning.
- [ ] 🟣 **CODY-NEXT-12.03** Namespace source identities and deduplicate observations
  arriving over alternate transports. Define source failover explicitly; a cloud
  connection cannot create a second copy of the same chat or replay a response.
- [ ] 🟣 **CODY-NEXT-12.04** Reuse the scenario matrix with provider-specific fixtures
  and separately authorized live acceptance. Integrate cloud transport through
  CLOUD-02/CLOUD-03 owners when activated; do not implement a parallel relay here.

**Completion:** a new source can populate the model without changes to Muse or
screen ownership. Provider limitations remain explicit rather than hidden in
firmware conventions.

### CODY-NEXT-13 - Adapt Muse Wi-Fi text entry to native Cody UI

**Outcome:** a reviewable large-key input design for the round screen, followed
by a separately staged CODY2D implementation if selected for activation.
The existing Wi-Fi setup remains available throughout this work.

**Verified reference:** `references/muse-gadget-sdk/esp32/components/muse/`
contains `muse_settings_ui.c` and `muse_keypad.c`. These are paths relative to
the Cody workspace root, not the worktree. `build_keyboard()` uses
`lv_keyboard_create`; `muse_keypad_create()` uses an LVGL button matrix and
textarea. `build_text_page()` selects the large-key keypad for sub-2-inch/narrow
displays. The round keypad adjusts bottom-row spacers/padding and text placement.
This source evidence does not prove Cody geometry, touch comfort or performance.

- [ ] 🟣 **CODY-NEXT-13.01** Catalog the reference's key maps, group/multi-tap timing,
  uppercase/numeric/symbol entry, deletion, password visibility, confirm/cancel,
  swipe suppression and round-screen geometry. Preserve the exact reference and
  license boundary; do not import LVGL dependencies as part of the Muse avatar.
- [ ] 🟣 **CODY-NEXT-13.02** Produce a native UI-only layout specification for the
  466 x 466 panel. Prefer the large-key small-screen behavior as the starting
  point; assess full-keyboard feasibility separately. Fit complete key hit bounds,
  field, title, Back and confirm actions against each occupied circular chord.
  Retain Cody's font/ASCII contracts and usable password symbols.
- [ ] 🟣 **CODY-NEXT-13.03** Define a renderer-independent bounded text-entry state
  owned by the UI input controller: buffer length, cursor, current character
  group, pending multi-tap commit, mode and cancellation. Render with CODY2D and
  route acquired touch through the existing owner; no LVGL or second touch reader.
- [ ] 🟣 **CODY-NEXT-13.04** Integrate network selection/manual SSID and password
  submission with the existing Wi-Fi owner. Define supported network/security
  cases, field limits and original connection/storage errors. Never imply a
  successful connection from text submission or remove the existing BLE/QR route.
- [ ] 🟣 **CODY-NEXT-13.05** Preserve credentials only as required by the existing
  configuration owner. Mask passwords by default, make visibility explicit and
  clear temporary input on cancellation or final exit from the entry flow.
  Navigation within that flow may retain the bounded draft as specified in 13.06.
  Keep credentials out of logs,
  domain telemetry, screenshots and public diagnostics.
- [ ] 🟣 **CODY-NEXT-13.06** Assign a permanent catalog/route code after 00's ID audit.
  Preserve caller context and entered text under deliberate local navigation as
  specified; accidental swipe must not submit or discard credentials. Cancel
  restores its caller without replacing a valid saved network.
- [ ] 🟣 **CODY-NEXT-13.07** Author scenarios for multi-tap timeout, rapid/repeated
  presses, deletion, symbols, long SSIDs/passwords, rotation, cancel, failed
  connection and missing/stale network results. Gate execution under the same
  host/build/device authorization rules as phase 11.
- [ ] 🟣 **CODY-NEXT-13.08** After implementation and static review, obtain applicable
  authorized visual and physical acceptance for hit accuracy, legibility, circle
  clearance and real network connection/persistence. If not activated, retain
  the design/reference as planned; do not claim a keyboard is already on Cody.

**Completion:** the selected behavior is implemented natively without mascot/UI
mixing or LVGL, existing provisioning/recovery remains usable, and the applicable
acceptance evidence supports typing and connecting on the real round display.

### CODY-NEXT-14 - Prepare the code for platform and device portability

**Outcome:** the portability flexibility of Muse's architecture, adapted to Cody:
one domain and one Muse behavior implementation, with explicit desktop-platform
and device adapters. This phase prepares the code now; it does not add every
future OS/board port or replace the existing platform architecture.

**Execution position:** after 00-02, before broker/device implementation in 03/05.
Use the current Windows host/native simulator and Waveshare firmware as concrete
consumers. Future Linux/macOS and different devices remain separately activated
ports, not presumed working products.

- [x] 🟢 **CODY-NEXT-14.01** Write a dependency map separating portable domain/reducer,
  provider adapters, transport, desktop OS services, Muse direction/rendering,
  display/input adapters, BSP and optional hardware services. Compare Muse's device
  descriptor approach as a reference; reuse useful boundaries without importing
  its LVGL dependency or whole application lifecycle.
- [ ] 🟡 **CODY-NEXT-14.02** Keep the domain reducer, identifiers, state transitions
  and logical protocol independent of OS and MCU headers. Supply time, admitted
  observations and bounded storage through explicit interfaces. Keep platform
  error causes visible at the adapter boundary rather than converting failures
  to success or forcing platform error types into every domain object.
- [x] 🟢 **CODY-NEXT-14.03** Keep Muse character behavior and its C rendering core
  portable. Separate pose/time/audio-energy inputs from device acquisition and
  RGB565 display transfer. Maintain one source implementation for host preview
  and firmware; do not fork expression behavior for individual boards. Make
  artistic randomness seedable for scenarios; keep credential/cryptographic
  randomness owned by the platform security services, never by that artistic seed.
- [ ] 🟡 **CODY-NEXT-14.04** Isolate PC-specific paths, credential storage, process
  launch, executable discovery and local IPC behind the appropriate existing
  OS owners. Use platform-correct path semantics, preserve provider-native IDs
  and avoid embedding drive letters, Windows-only shell commands or credential
  APIs in provider-neutral broker logic. Keep Windows behavior operational.
- [x] 🟢 **CODY-NEXT-14.05** Define a typed device-capability descriptor from actual
  board configuration/owners: display dimensions/shape/rotation/pixel format,
  touch and physical inputs, audio input/output and channel count, storage,
  available memory classes and optional sensors. Capabilities describe availability;
  health/freshness still report whether an available component currently works.
- [x] 🟢 **CODY-NEXT-14.06** Route viewport, clipping, scaling and hit geometry through
  board/display adapters. Preserve the current 466 x 466 circular layout and its
  accepted bounds as the first implementation. Prepare separate geometry policies
  for other shapes/resolutions without assuming every device has a 320-pixel safe
  square. Use the same acquired logical coordinates for drawing and touch.
- [ ] 🟡 **CODY-NEXT-14.07** Keep physical board, build profile and optional services
  orthogonal. A microphone-free profile disables recording entry cleanly while
  retaining the domain and mascot; a single microphone never creates two channels.
  Parameterize memory budgets/capacities explicitly and keep overflow behavior
  truthful on a smaller-memory device. Missing PSRAM is not silent fallback success.
- [ ] 🟡 **CODY-NEXT-14.08** Reuse existing native host and board adapters for clock,
  persistence, input/audio observations and display submission. Add only the
  interfaces required by this plan; avoid a speculative universal hardware layer
  or replacing unrelated BSP functionality. Preserve canonical acquisition owners.
  Specify each adapter's actual durability and memory guarantees: atomic file
  commit, DMA alignment/retirement and PSRAM/cache access remain target-owned,
  not assumptions of a generic allocation or persistence wrapper.
- [x] 🟢 **CODY-NEXT-14.09** Wire portable sources and adapter selection into existing
  build/profile composition through scripts/device.ps1. Keep generic code free
  of scattered board-name branches; put unavoidable platform conditionals at
  the adapter/build boundary. No unsupported OS/board is advertised as enabled.
- [ ] 🟡 **CODY-NEXT-14.10** Author conformance scenarios for the same domain events
  and Muse inputs across host/current-board adapters, capability absence, smaller
  capacities and alternate geometry fixtures. Run them only under phase-11
  authorization; label simulated geometry and devices explicitly.
- [x] 🟢 **CODY-NEXT-14.11** Document how to add one PC OS adapter or one device:
  required capabilities/interfaces, source/build registration, ownership,
  resource budget and acceptance evidence. A future port should not require
  rewriting Chat/Turn/ToolCall/Question or Muse expressions. Select actual future
  OS/board targets separately when that port is activated.

**Completion:** shared domain and Muse code have explicit portable boundaries,
current host/board consumers use their adapters, geometry/capacities are not
universal hardcoded constants, and unsupported features fail explicitly. Phase 11
establishes working current-target behavior; new targets require separate evidence.

### Milestones, decisions and existing work

- **CODY-M1 - Contract ready:** 00-02 complete as design, including capacity,
  correlation and recovery rules. Activate 14's portability foundation before
  implementing platform-dependent consumers.
- **CODY-M2 - Domain ready:** 03-06 implemented and statically reviewed. The PC
  and device retain multiple chats/tool calls/questions without presentation,
  using the portability boundaries established by 14.
- **CODY-M3 - Companion integrated:** 07-10 implemented and statically reviewed;
  Muse, audio and separate UI routes consume the same model.
- **CODY-M4 - Codex accepted:** 11 passes the authorized applicable gates.
  Missing hardware evidence leaves the milestone pending, not silently complete.

Decisions to close in their owning phase: exact store/history sizes (01), transfer
budgets and incompatible-peer rejection (02), supported question correlation and
recipient reattachment (04), persistence partitions/retention (05), final Muse
timings/size (08), two-dot visibility and new route codes (09), native Wi-Fi
keypad layout and activation (13). Use the agreed
defaults above until an explicit product decision changes them; do not reopen
the Muse renderer choice or quiet-hours disabled state as unresolved questions.

Existing CCP-01/02/03 acceptance maps to 03-05/11; ART-01 and CCP-05/06 map to
07-08/11; AUDIO-01 and TIME-01 contribute to 08/11; PERF-01 and HW-01 contribute
to 05/11; UI-01/04/07 and BOOT-01 contribute to 00/09/11. Preserve their remaining
gates and consolidate overlap when activating work. Older mixed-Home tasks stay
historical/deferred references until explicitly reconciled with this plan; they
must not reintroduce a character into UI screens. The separate audio-menu,
cloud, hardware and voice work is not automatically activated or marked complete.

## Boundaries that still apply

- Physical voice, walking/classifier and speech-to-Codex trials remain deferred.
  Keep HAR observational, using original labels and actual timestamps. Training
  and automatic shutdown require separate product decisions.
- Keep the accepted SD/PCB standby and wake workaround. Further investigation
  of the underlying SD fault requires a separate decision.
- The experimental WAV queue uses public ingress. Model-side speech understanding
  still needs verification. Keep separately billed transcription out of scope.
- Firmware build, host tests, flash and physical acceptance are separate evidence.
  Authorization follows [AGENTS.md](AGENTS.md).

## Implementation and product decisions

- [ ] **CLARITY-01 - Split route/frame responsibilities.** Extract coherent
  presenters from `CODY_onboarding.cpp` and stages from `draw_companion_frame()`;
  keep orchestration, lifetime, lock ownership and errors explicit.
- [ ] **CLARITY-02 - Share bounded marquee calculations.** The activity and approval
  headers still duplicate first-glyph/offset/capacity/centering math. Share only
  that calculation; retain text, viewport and pausable clocks with each owner.
- [ ] **CLARITY-03 - Replace narrow uses of full snapshots.**
  Replace snapshot() by-value reads with bounded, fallible projections,
  starting with storage formatting. Budget the nested stack before removing the
  API. Keep the v1 scene-font reader until its assets migrate and continue
  rejecting invalid calibration records.
- [ ] **OTA-01 - Upload, boot selection and rollback.**
  Implement authenticated Wi-Fi upload, boot selection and rollback on boards
  with two supported OTA slots. Define shared storage policy and observe
  rollback on target.
- [ ] **LUNA-03 - Product controls.** Shared JSON geometry tokens, product keyboard/
  confirmations, Settings access to existing IMU calibration and opt-in HTTP,
  plus measured FFT gain/layout/latency and status presentation. Use public APIs.
- [ ] **PRODUCT-01 - Character presets.** Decide whether 04/05 share one size
  (three presets) or retain four; preserve response/QR clearance.
- [ ] **UI-05 - Font generation and raster.** Committed glyphs are 2bpp. Generate
  4bpp only when requested; then inspect coverage, tiles, byte order, compiled cost,
  native parity, legibility and Options/Look scrolling.

## Physical power, activity and fault evidence

- [ ] **POWER-OFF-01 - Shutdown behavior.** Observe battery-only 60-second/scene
  inactivity, USB/full/absent battery, first hold, three-second farewell, owner
  drains/rollback and configured power-on timing. Verify Sophia/touch/shake wake
  and whole-board off. The independent 10-second emergency hold and supply-dependent
  PMU restart sources prevent an unconditional Power-only restart claim.
- [ ] **ACTIVITY-01 - Pretrained observer.** Preserve recovered preprocessing,
  original labels and real 20-Hz timestamps; validate placement, accuracy, cadence
  gaps, calibration/standby/pressure boundaries, energy and stack headroom before
  relying on classifications. No synthetic samples or training. Resolve original
  weight redistribution rights before publishing an embedding firmware binary.
- [ ] **BATTERY-ENDURANCE-01 - Repeatable runtime.**
  Measure continuous runtime through confirmed battery shutdown. Compare Debug
  and Release with matched workload, battery and starting conditions. Treat a
  source gap or unknown interruption as an incomplete observation.
- [ ] **WAKE-RECONNECT-01 - Wake-to-Codex latency.**
  Measure the interval from wake request through completed wake, association,
  IP and authenticated READY. Check saved-network startup, immediate progress
  after IP and recovery choices after 15 seconds offline.
- [ ] **AUDIT-TARGET-01 - Memory/failure recovery.**
  Measure nested stack margins during wake and callbacks. Exercise
  rejected-model cleanup and incremental model-tamper rejection.
- [ ] **FAULT-04 - Codex-choice reboot report.**
  If selecting a Codex choice triggers a reboot, capture the first panic from
  the actual choice and decode it against the running ELF.
- [ ] **FAULT-02 - Touch/caress freeze.** Capture the first rejected packet or
  transition; review two contacts, D000/AB ACK, malformed fifteen-FF isolation,
  normal-mode return, invalid samples and release-before-readmission. Then observe
  an eligible 300-ms caress and its sound, distinguishing gaps/speed/zone exit.
- [ ] **FAULT-03 - Renderer stop.**
  Capture a renderer stop from its first failing frame with the matching ELF.
  Inspect SPI acquisition deadlines and measure sustained rendering on the
  affected route.
- [ ] **FAULT-PERIPHERAL-01 - Peripheral/USB errors.** Diagnose codec/PMU I2C,
  I2S disable-before-enable, logger loss and interrupted serial captures separately.
- [ ] **POWER-01 - Sleep/wake transactions.** Observe Debug standby and Release
  Deep-sleep: dark restore, retained clocks/routes, failed-entry rollback, known-off
  radio cleanup/BLE reserve, rotations and QMI ACK/held IRQ. Include cold boots,
  two 60-second cycles and five rotated/diagnostic PWR cycles. Proposed permanent
  provisioning/telemetry stop is a separate decision from existing suspend/resume.
- [ ] **POWER-02 - Electrical behavior.** Measure board/radio/codec/IMU current,
  dim recovery and GNSS ACK/TTFF/accuracy. True GNSS Backup needs a switched rail.
  Retain the 80-MHz floor; lower clocks are research, not established savings.
- [ ] **SAFE-01 - Fault injection.** On an explicitly authorized disposable device,
  exercise GUI/display/snapshot/NVS/provisioning/audio/radio stalls, Task-WDT,
  breadcrumbs, deadlines and DMA quarantine; measure deepest worker margins.

## CCP and device presentation

The four-panel Home contract is in [Cody layout](docs/cody/layout.md).

- [ ] **CCP-01 - Security rollout on target.**
  Identify the paired-PC revision and observe pairing, recovery, packet/ACK
  loss, restart, consecutive questions and routing. Measure ingress bursts and
  nested stack use on target.
- [ ] **CCP-02 - Genuine hook delivery.** After an authorized reload, follow a
  real prompt through autoload, latest-prompt selection, chat leases, broker/LAN
  recovery and turn correlation. No replayed activity, transcript inference or trust edits.
- [ ] **CCP-03 - Decisions and delivery.** Observe screens 26/27/31, selection,
  Send, received versus recipient-confirmed delivered, original TTL, cancel/offline
  retention and terminal EVENTACK ordering. Include notices and read-only PR/check
  views; physical restart/reset require their own explicit intent.
- [ ] **CCP-04 - Home, mode and gestures.** Review all four current panels and
  five-row Options, strict Desk/Away persistence/errors, metadata freshness,
  approval/compaction priority, paused tap cycling and deliberate shake.
- [ ] **CCP-05 - Character response.** Observe cardinal-rotation gaze, caress
  warmth, positive-only mood and exact compaction release, preserving source-gap,
  setup/preset/Debug isolation and protected channels. Measure nested stack minima.
- [ ] **CCP-06 - Acting and notices.**
  Observe Thinking/Editing/Running, attention blink, exact-turn reply and
  interruption, genuine PC approval, source gaps, hidden routes,
  battery/charging thresholds and expiry.
- [ ] **CCP-07 - Price bands and panel timing.**
  Observe the displayed price band, compaction priority, full marquees and
  tap/shake transitions. Verify PC adoption and current-image stack headroom.
- [ ] **CCP-USAGE-01 - Native telemetry on the display.** Observe Home counts and
  genuine compaction animation/sound. Reload clears followed-chat state until a
  new prompt; do not synthesize recovery or infer bills/thresholds from model names.
- [ ] **UI-ACTIVITY-02 - Connection/activity presentation.**
  Observe discovery, the ten-minute search and three-minute Working standby,
  the valid-battery wake prelude, genuine activity text and four-panel Home
  metadata and geometry.
- [ ] **UI-ACCENT-01 - Animation acceptance.**
  Observe gears and clock accents, rotations and completed partial repaint on
  the device. Measure nested stack headroom.
- [ ] **BATTERY-01 - Quiet notices.** Observe ten-second free-Home admission,
  four-second dwell, 20/10-percent warnings, charge/full transitions and stale/error
  removal with route/touch deferral. Studio automatic notice/caption parity and
  physical clearance/headroom remain; manual power studies keep factual selectors.

## Routes, character and sound

- [ ] **UI-01 - Setup/retained onboarding.** Observe first-run/saved pairing and
  Wi-Fi, DHCP/recovery, No/Later without writes, development replay, standby/Deep-sleep
  and 09 -> 10 -> 36 -> 11 -> optional 12 -> 13 -> 16 -> 14 -> 15 confirmation gates.
- [ ] **UI-02 - Escape/holds.** Observe the 1500-ms hold, 12-pixel cancellation,
  250-ms sample gaps, across-wake contacts, busy-route cancellation, actual release
  and rotations without clearing Wi-Fi/pairing.
- [ ] **UI-03 - Pairing/timers.** Observe 05/06/07/08/09, pending/ready/error,
  cancel/readback/Retry, first-boot persistence and Debug 39. Cover awake 30-second
  route closure, 90-second pairing, independent 60/90-second standby overrides
  and screen 12's 180-second copied-code window, including late worker teardown.
- [ ] **UI-04 - Controls/QR.** Review semantic cards/capsules, numbered rows,
  completed-text reveal, visible-only touch, contrast/chords/quiet zones and real
  QR scans at all rotations. Reconcile 05/06/07's suppressed lateral Back with
  the previous-route rule; Home retains its explicit no-Back contract.
- [ ] **UI-06 - Look persistence.** Observe save/readback retry, busy Back/Done,
  reset-during-write, rotated touch, stable IDs and repeated-entry leak/race behavior.
- [ ] **UI-07 - Diagnostics/Memory.**
  Observe the measured overview and heap bars in Debug and Release. Cover
  unavailable states, Back/Normal restoration, standby, allocation errors and
  rotations. Reserve PWR for standby and shutdown and keep Debug BOOT
  independent. Keep facial channels internal.
- [ ] **ART-01 - Muse avatar.** Review physical wake, idle and tap playback,
  frame continuity, full display rotation and touch mapping. Keep additional
  interaction modes separate until their event sources are defined.
- [ ] **AUDIO-01 - Device sound.** Listen to BootRise and sixteen current Codex
  cues with frame/semantic admission, quiet/volume, no wake replay, microclick
  reveal deltas, cancellation and codec contention. Greeting C remains an audition.
- [ ] **AUDIO-02 - Windows sound.**
  Listen to the host tap and boot cues. Confirm
  cancellation and the reports from real playback errors.
- [ ] **BOOT-01 - Branding handoff.** Review the full 42-frame/3500-ms VibeWare
  cycle, readiness dwell, titles, failure/DMA parking, first/returning boot,
  rotations, sleep policy and Luna linked memory.
- [ ] **PERF-01 - Native budget.** Measure a truly static route and full stage/
  payload/DMA/CPU distributions, interruption,
  alignment/tails, log overhead and radio/HTTP/BLE load. Pixels and current-image
  nested headroom must accompany improvements against the 40-ms target.
- [ ] **PERF-02 - Host budget.** Measure ABI-2 raster/snapshot cost and memory,
  atomic import/failure, redaction/aging and sparse coverage. Review DPI/controls/
  gestures after a requested restart; cross-monitor DPI handling is unimplemented.

## Optional services and release coverage

- [ ] **STORE-01 - Logger/readback.**
  Measure recovered internal memory, queue pressure, archival failure,
  unavailable SD, optional failures and watermarks. Check CRC and session
  identity across passive or interrupted USB capture, busy storage, remount and
  standby.
- [ ] **STORE-02 - SD lifecycle.**
  On separately authorized disposable media, review format, cancel, progress,
  failure, removal, full-card states, mounted wake and reset recovery.
  Formatting requires its own single-use approval.
- [ ] **TIME-01 - Clock/routines.** Observe atomic timezone/quiet settings, overnight
  windows/Rome DST, corrupt/missing values, NTP failure/recovery, RTC trust/write/readback,
  backup retention, greeting damage and deferred boot sound.
- [ ] **VOICE-PRESENCE-01 - Recording lifecycle.** When voice trials resume,
  observe complete-frame/three-second start, five acquired seconds of silence,
  cancellable three-second send, late cancellation and real delivery. MCP transport
  EOF is not independent GUI-process/crash detection. See VOICE-01.
- [ ] **VOICE-POLL-UI-01 - Voice timing/resources.** Measure AFE notification waits,
  disconnected cadence, ribbon pacing/gain, 350-ms send/double-tap arbitration,
  shake/discard, rotations, rollback, stacks and current. No battery-gain claim from code alone.
- [ ] **VOICE-01 - Recognition and end-to-end recording.**
  When physical trials resume, review Sophia, Jarvis, Computer and three-model
  packing separately. Cover capture/AEC ownership, CPU/PSRAM, standby/stale
  events, Home’s 420-ms double tap, recording countdowns and 350-ms
  arbitration, bounded PCM, ACK, gaps, replay, recovery, changed chats and
  uncertain END. Trace capture through WAV, the frozen-chat queue and Desktop
  consumption. Assess speech understanding separately.
- [ ] **TTS-01 - Audible SD speech.**
  With the accepted SD workaround in place, check availability, the boot
  diagnostic, bounded wait, errors and diagnostic-only quiet bypass. Listen for
  intelligible Italian.
- [ ] **TTS-02 - Variants/budgets.** Cover excluded, metadata-only SD and English
  flash profiles, locks/config/flash lists, SHA rejection and four-language UTF-8.
  Measure PCM, CPU/PSRAM/stacks, LVGL coexistence, cancel/quiet/codec/sleep and SD removal.
  Full Luna TTS and flash-English builds remain separate; new triggers need a decision.
- [ ] **HW-01 - Acquisition/stacks.**
  Measure deepest physical stack use for acquisition and staging, full-snapshot
  HTTP, Luna, DeepSleepRelease, BLE, callbacks, logging and wake.
- [ ] **LUNA-01 - Composition matrix.**
  Cover the Brookesia 0.8 display-off/all-services and audio I/O combinations.
  Observe display, touch, rotation, DMA, radios and sleep on target and cover
  the PMU-enabled, battery-service-disabled profile.
- [ ] **LUNA-02 - Document lifetime.** Observe lazy bootstrap/unload memory,
  navigation/history, zero-to-five commands, stale generations, scoped bindings,
  partial failure, reordered identities, pause/frame locks and BLE Settings responsiveness.
- [ ] **BOARD-01 - Leg Edition.**
  Review all five views with fresh sensors and peaks, controls, levels, audio,
  PWR, settings, rotations, excluded peripherals and disabled RTC status.
  Observe load-bearing behavior on the current image.
- [ ] **BOARD-02 - ToF.**
  Before enabling, review both ToF modules, R15/R16 isolation, shared TX and
  dual RX wiring, calibration, persistence, timeout and recovery, tilt and
  Settings.
- [ ] **TOOL-01 - Console runtime/package cases.**
  Exercise cancellation, ambiguous ports and artifact ownership during
  packaging, capture and concurrent operations.
- [ ] **DOC-02 - Wider links/anchors.**
  Review moved documentation links and anchors across the repository.
- [ ] **RELEASE-01 - Broader acceptance.**
  Define and execute the needed profile matrix and physical startup/branding
  acceptance when release work resumes. Resolve wider product and licensing
  requirements, including model-weight rights under ACTIVITY-01.

## Product ideas

| Area | Possible next work and limits |
| --- | --- |
| Other PC platforms | Implement Linux/macOS credential and launcher owners before claiming support. Keep Python additions neutral; no new Windows service/general PC observer. |
| Remote connection | Private ASP.NET Core/Aspire/OpenTelemetry relay source is developed separately from this public repository, using standard OS-mounted SMB/Samba storage without Azure APIs. Firmware integration remains deferred: key-bound account pairing, outbound HTTPS polling, exact event identity, expiry, receipts and revocation. Native-platform SMB validation, Dot connection and deployment remain acceptance gates; no live cloud support is claimed. |
| Built-in assistant audio | Recheck supported App Server audio access, formats, account availability and existing-chat attachment. No separate paid transcription. |
| Other assistants | Explore explicit adapters for Codex, Claude, Hermes, OpenClaw and possibly Your Dot; verify each public integration and real acknowledgements rather than assume a universal API. |
| Offline outbox | Explicit SD recording with original timestamp/clock validity, destination, bounded buffers, interrupted-write recovery, retention/deletion and uncertain-delivery reconciliation. No formatting/fallback; review stale actions before replay. |
| Reminders and focus | Start with user-started sessions, one-time reminders/countdown/snooze/daily overview; later consider lists, notes and recurrence. Trusted time, quiet preferences and existing power/timer ownership; RTC INT cannot directly wake ESP. No home automation or inferred health/fatigue. |
| Message/reply UX | Research push-to-talk, preview, destination, Send/Cancel and drafts through a supported channel. Keep credentials on PC and distinguish sent, received and completed; additional speech triggers need a decision. |
| Wellbeing model | Still disabled: no session adapter/timer/selector/UI. Keep activity, rest, drink, meal, mood and reaction domains separate; motion stress/battery fatigue retain their causes. Lost liveness is Unknown, not counted work. One latest eligible cue, no decorative queue; snooze, quiet, expiry and acknowledged events remain distinct. A caress is not proof of a break/drink. |
| Character | Consider other Muse interaction modes after physical review of wake, idle and touch. |
| Automatic power | Only after observer validation: persistent observations, absent interaction, cancellable warning and measured false-shutdown rate. Keep this separate from the classifier and explicit shutdown. |
| Protocol security | Evaluate standard PAKE/high-entropy QR against offline guessing and standard AEAD with nonce/replay discipline; current HMAC adds authenticity, not confidentiality. Review optional plaintext HTTP separately. Bound startup runtime.log with multiprocess ownership. |
| Dependencies/BSP | Deferred removal of indirect FatFS/SDMMC when SD is off; split bus/display/audio/SD/expander owners and review board locks, including rectangular ES8311 input. Replace pinned LVGL-adapter/default feature workarounds with supported upstream configuration. |
| Energy research | Measure AFE/transport cadence and actual component currents. BOOT interrupt work needs debounce/rearm/hold ownership; idle CCP still needs deadlines. Compare Wi-Fi power-save, 40-MHz floor/Light-sleep and USB reconnect; retain unsafe SOF exclusion. Consider 2-Hz dim acquisition only with Full Snapshot cadence and measured 600-ms P99 wake. Battery-absent ADC disable needs BAT_INSERT reenable/settling evidence. Future PCB may route PMU IRQ to an RTC GPIO. |
| Maintenance | Transactional banner updates only after a banner-policy decision; scoped GNSS parser extraction with separately requested coverage; extract hardware/audio/Leg/settings presenters without changing navigation/bindings or promising Cody RAM gains. |

