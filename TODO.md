# Active work

Updated 2026-10-14. This is the concise autonomous work queue; completed evidence
belongs in [done.md](done.md). Historical open gates and product ideas remain in
[planned-todo.md](planned-todo.md), with their stable IDs and evidence limits.
Implementation, host checks, builds, installation and physical validation remain
separate. Follow [AGENTS.md](AGENTS.md) and the current user authorization for each
operation; this list grants no additional permission.

- [ ] **FLASH-FOOTPRINT-01 - Recover firmware growth margin.** Reduce measured
  application footprint while preserving Sophia, the pretrained observer, current
  behavior and the existing partition table. Use actual map/size evidence;
  distinguish fitting in a slot from having useful margin. No training or
  removal of required features to hide overflow.
- [ ] **PC-PLUGIN-ALIGN-01 - Align the PC plugin with reviewed source.** A local
  0.6.5 cache exists, but its audio receiver predates the repository's newer
  readiness, correlation and uncertain-delivery handling. Identify the exact
  source/runtime revision, align the approved installation and preserve pairing,
  hooks and credentials. Check optional dependencies and native endpoint
  readiness without starting a recording or sending audio. Being online proves
  neither current code alignment nor native voice delivery.
- [ ] **FIRMWARE-VALIDATION-01 - Complete selected Debug/Release validation and
  installation.** Finish the current integrated profile's authorized checks,
  target builds/size and deployment steps. Record exact image/profile identity,
  slot margin and individual compiled frames; do not extrapolate to every board,
  full call-chain headroom or physical UX. Preserve the captured battery/NVS
  baseline and distinguish built images from installed images.
- [ ] **RELEASE-ENDURANCE-01 - Provide explicit durable-record readback.** Release
  keeps the NVS recorder but removes its INFO summaries at WARN maximum. Add a
  bounded, explicitly requested readback through the canonical owner, preserving
  CRC/schema errors, source gaps and unknown termination. Do not enable all INFO
  logs or interpret an absent summary as an empty journal. This is preparation
  for a later controlled endurance run, not evidence of improved battery life.

Physical voice tests and walking/movement/classifier trials were explicitly
postponed by the user on 13 October. Keep those gates in planned-todo.md until
reactivated. The accepted SD/PCB limitation is outside this work. No model
training or classifier-driven automatic shutdown is included.
