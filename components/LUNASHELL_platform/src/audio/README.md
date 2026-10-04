# Audio ownership

One shared codec/transport owner handles playback, capture, standby and the voice
AFE. The frame-driven typewriter cue uses its fixed mailbox and existing route;
it does not create a second I2S owner.

Canonical clock policy, saved codec volume and real operation errors are preserved.
Stubs represent audio/AFE features excluded by the selected build.
