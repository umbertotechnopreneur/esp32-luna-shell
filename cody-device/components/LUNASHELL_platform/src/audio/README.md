# Audio ownership

One shared codec/transport owner handles playback, capture, standby and the voice
AFE. The frame-driven typewriter cue uses its fixed mailbox and existing route;
it does not create a second I2S owner.

Codex activity and personality symbols share the existing optional microcue
worker and one-slot mailbox. Their presentation gates and cancellation
generations are independent; standby cancels both. Codex begins suspended.
Each immutable clip is at most 350 ms at 16 kHz. Activity requests are limited
to one start every 1.5 seconds; notices bypass that cooldown but cannot interrupt
an in-flight cue. Busy or expired requests are dropped explicitly and never
replayed. No extra audio task, transport owner or unbounded queue is introduced.

Canonical clock policy, saved codec volume and real operation errors are preserved.
Stubs represent audio/AFE features excluded by the selected build.
