<img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="72">

# Cody symbol and connectivity sounds

Seventeen original effects use analytic synthesis without external recordings,
voices or samples. Each cue has a 16-kHz mono PCM16 WAV and raw PCM copy. The
compiled flash catalog uses the same samples, keyed by stable `PersonalityCue`.

[Listen to all seventeen](audition-all.wav). Individual clips last 170-300 ms
with authored -18 dBFS peaks. Native PCM payload totals 128,960 bytes before
catalog descriptors and code. Listening quality and physical speaker loudness
remain unverified.

Connectivity has four distinct sound identities:

| Fact or transition | Cue | Sound |
| --- | --- | --- |
| Wi-Fi connected with an IP address | WifiConnected | Two rising radio-link tones |
| Wi-Fi disconnected | NoWifi | Two separated falling radio-link tones |
| Codex PC connected | CodexConnected | Three rising terminal tones resolving into a chord |
| Codex PC unavailable/disconnected | NoCodex | Three falling terminal tones with a broken final pulse |

The display owner consumes real connection edges after a completed active frame,
including first observed connection/absence. Repeated observations or returning
icons cannot repeat the sound. Wi-Fi loss is the primary audible cause when it
also removes the PC link. Each link retains only its latest event for at most
800 ms of busy admission; hidden views, unavailable observations and standby
cancel it. The existing audio owner still controls volume, Sounds, quiet hours,
request expiry and asynchronous failure reporting. Suppressed/failed attempts
are not replayed, and dispatch does not prove audible completion.

Other symbol sounds retain the existing completed-appearance policy. Search,
BatteryReady and BatteryLow remain silent. The shared audio owner admits one
in-flight sound; the authored global cooldown is zero and visual cue cooldowns
remain independent. WAV wrappers and the listening reel are not firmware inputs.

Regenerate through the sole public CLI:

```powershell
./scripts/device.ps1 generate -Generator cody-personality-audio
```

`manifest.json` retains recipes, durations, hashes and reel offsets. Compilation,
host checks, listening and device playback verification remain pending.

---

**VibeWare** · Created by Umberto Giacobbi · [Cody on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) · [VibeWare](https://github.com/umbertotechnopreneur/VibeWare)
