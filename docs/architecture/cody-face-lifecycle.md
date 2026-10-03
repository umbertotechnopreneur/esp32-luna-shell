# Cody face lifecycle and expression catalog

The standalone Cody renderer starts with animated **Calm -> Happy -> Calm** on
every ordinary cold boot, software restart and hardware reset. It begins with
the rig in the mandatory first frame rather than waiting for startup bitmap
checkpoints. The sequence uses active animation time: Calm 900 ms, Happy
1,200 ms, then Calm 900 ms, followed by idle. The real startup checkpoints
continue to report completed services and do not select static facial images.

The source atlas names are **35A / Sleep**, **35B / Wake**, and **16 / Idle**.
Calm and Happy are expression combinations, in a separate typed namespace from
screen identities. The atlas recommends restoring the previous useful screen;
the requested current behavior instead returns to 16 / Idle after waking.
The implementation covers these facial presentations, not every planned route
or label in the atlas.

The boot face sits above the bounded ASCII brand `umbertogiacobbi.biz`, centered
at logical y=348 and checked against the circle's chord at every bounding corner.
The existing packed title font is reused. No performance HUD appears during
Calm/Happy/Calm. The branding fades out over 300 ms before **04 / Hi, I'm Cody!**
enters; its typewriter and authored Happy face then run over the continuous rig.
The FPS/CPU readout and its measurement windows start with screen 04. Readout
text is RGB565 whitesmoke (#F5F5F5), retaining real CPU measurement and unavailable
states. An explicit named expression selection may override the scene's face.

The persisted first-boot opt-out remains respected: when screen 04 is skipped,
branding still fades away after ordinary startup but the screen-04 HUD gate
stays closed. Deep-sleep wake also skips boot branding and the greeting, without
rewriting the persisted preference. Touch/body interaction does not depend on
whether performance counters have started.

## Runtime owners and transitions

`CODY_lifecycle` chooses authored channel targets. `advance_face` interpolates
the independent eye, brow, mouth and other controls; `evaluate_rig` reconstructs
the corresponding anchored points and curves. This is the existing point-based
rig morph, not replacement of full-screen PNGs. Body/contour controls keep their
separate owner and do not select an unrelated pose when the expression changes.

`CODY_response` composes breathing and natural blinking after selecting the base
expression. Their phases remain continuous across Calm, Happy, idle and software
wake transitions. The base breath is 0.25 Hz with the existing real-input response;
visible Sleep slows towards 0.15 Hz without resetting phase. Awake eyes blink
over the existing 280 ms closure/reopening interval every 4.8 seconds. Sleep eyes
stay closed. An explicit diagnostic Pause still freezes the character; ordinary
expression selection never pauses it. All sensor input remains exclusively from
the canonical device snapshot, with original health, errors and timestamps.

The single display consumer registers a power lifecycle callback before the
power manager starts. Entering standby requests **35A / Sleep**, morphs the
eyelids closed over 400 ms and holds a closed-eye animated breath for 300 ms.
Only a completely retired panel frame acknowledges preparation. The power owner
then suspends producers and display writes and applies its existing panel fade.
This face change does not change the selected power policy or fade duration.

On software wake, the power owner restores producers and enables standalone
writes while the panel is dark. The worker prepares **35B / Wake** before the
panel fade-in, first targeting Drowsy for 300 ms, then Calm; after 900 ms the
lifecycle is **16 / Idle**. A Deep-sleep reset is distinguished using the real
ESP reset reason and starts Wake -> Idle rather than replaying the ordinary
boot greeting. Its rhythm starts a new CPU session; no previous sensor reading
is invented or retained as current data.

Preparation is bounded by a three-second failure deadline. Renderer failures
return the original error; rejected Sleep preparation attempts a Wake recovery
and remains a failed transaction. A failed Wake preparation leaves the panel
dark. Existing transactional rollback retains real resource failures. The
explicit Light/Deep-sleep APIs also request the facial lifecycle; their existing
surrounding hardware lifecycle remains the caller's responsibility.

## Implemented service index

Authored combinations live in `assets/cody/cody.yaml`; the rig generator embeds
their channels in internal flash. `CODY_expression_catalog` provides immutable
definitions with stable IDs, exact case-sensitive keys, and names. Reordering
the tables does not change selection. Compile-time contracts reject duplicate
identities/keys, unresolved atlas references and an authored preset omitted from
the catalog. Unknown keys and ambiguous `35` fail explicitly.

| Expression key | Name | Availability |
| --- | --- | --- |
| `calm` | Calm | Lifecycle and runtime selection |
| `happy` | Happy | Lifecycle and runtime selection |
| `sleeping` | Sleeping | Sleep combination and runtime selection |
| `drowsy` | Drowsy | Wake combination and runtime selection |
| `embarrassed` | Embarrassed | Existing authored combination |
| `boot.sleep` | Boot sleeping preview | Existing authored preview channels |
| `boot.waking` | Boot waking preview | Existing authored preview channels |
| `boot.ready` | Boot ready preview | Existing authored preview channels |

| Atlas key | Typed identity | Base combination |
| --- | --- | --- |
| `01` | `AtlasScreenId::Sleeping01` | Boot sleeping preview |
| `02` | `AtlasScreenId::Waking02` | Boot waking preview |
| `03` | `AtlasScreenId::Ready03` | Boot ready preview |
| `04` | `AtlasScreenId::Greeting04` | Happy; existing `SceneId::Greeting` route |
| `16` | `AtlasScreenId::Idle16` | Calm |
| `35A` | `AtlasScreenId::Sleep35A` | Sleeping |
| `35B` | `AtlasScreenId::Wake35B` | Drowsy, then Calm |

`expression_catalog()` exposes the implemented index. `find_expression()` and
`find_atlas_screen()` resolve typed identities or exact keys, and
`expression_channels()` returns validated base channels. Lifecycle selection
uses the same atlas lookup, so the service index is the actual runtime source.
Screen 04 resolves its real greeting scene through the catalog. Screen identities
01/02/03 describe the existing authored preview combinations; they do not override
the requested ordinary Calm/Happy/Calm startup sequence. For example,
`find_atlas_screen("35A", screen)` resolves the screen mapping, while
`find_expression("sleeping", expression)` resolves only its face combination.

`CODY_companion_set_expression("happy")` selects an implemented combination
while the boot-completed renderer is in Idle. It resolves the key once to an
`ExpressionId`, marks the existing worker dirty and preserves its current
breath/blink clocks and rig interpolation. This is a session setting, not an NVS
write. Power transitions take priority, clear the selection and return to Calm
after wake. Selecting the `sleeping` expression changes the face only; requesting
physical standby remains the power owner's operation. No HTTP endpoint, task,
sensor owner, framebuffer or LVGL dependency is added.

## Delivery evidence and limits

Implementation and generated source assets were reviewed statically. Only the
rig asset generator ran, through `scripts/device.ps1 generate -Generator
cody-rig`. No build, configure, tests, checks, simulator, serial access or hardware
operation was authorized or performed. Appearance, physical transitions,
measured frame cost and real sleep/wake behavior remain unverified.

This change is in the Cody Companion worktree on `main`. The separate
Debug/Release and Deep-sleep policy work in Luna Shell remains on its current
branch; it has not been merged into this worktree. The facial lifecycle is
independent of build mode and does not import that policy implicitly.
