# Latest validation round

## 2026-11-15 · Cody Studio boot replay, audio and guide routes on Linux

The host device-runtime build succeeds with the production C/C++ rendering and
audio owners: `pwsh -NoProfile -File ./scripts/device.ps1 generate -Generator cody-simulator-graphics -ToolArguments '--device-runtime'`.

`check -Check cody-studio -ToolArguments '--phase-e'` passes **29/29**. Two
independent processes repeat S01/S02 source and display-worker captures and
the observed P01 ↔ P04 plus ten adjacent S17–S22 guide gestures. S19 is
conditional on the compiled wake-word feature. The S21 QR page reveals its
controls before each observed exit. Same-frame route receipts, route history,
backwards replay, invalid inputs, pending touch and orientation are checked.
A later Tk run covers the additional comparison regressions.

`check -Check cody-studio -ToolArguments '--host-audio'` passes **27/27** on
this host: the native owner and selected ALSA backend reported both S02 replay
cues completed. The isolated PCM probe also verified nonzero mixer output with
Sounds on and no output with Sounds off. These observations do not establish
that a person heard the speakers or prove physical Cody sound quality.

`check -Check cody-studio -ToolArguments '--pilot'` passes **32/32** and the
real Tk/native window suite passes **31/31** using
`xvfb-run -a -s '-screen 0 1920x1200x24' pwsh -NoProfile -File ./scripts/device.ps1 check -Check cody-studio -ToolArguments '--ui'`.
Screenshots of the live preview, guide and small window were visually inspected.
The final focused `--guide` check passes **31/31** after the comparison fixes.
The `snapshot-integrity` and `cody-runtime` public checks pass.

Live boot replay is a returning-device scenario with a real-time host clock;
its preview is not capture evidence. Static and controlled-worker captures are
silent, and display-worker replay does not control every service or timer.
Comparison preserves complete navigation provenance and rejects incompatible
or unknown inputs. Historical route evidence still depends on the current
catalog. Other routes, remaining inventory, atlas automation/PDF, Windows and
physical acceptance remain open under `CODY-NEXT-06`. No device was contacted
or firmware flashed. Generated artifacts remain under `artifacts/` for manual
cleanup.
