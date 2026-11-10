# Native text entry

`CODY_text_entry` is a reusable CODY2D keypad for a 466 x 466 display. It is
compiled into the companion component and used by the local Wi-Fi password
route. The existing phone-based setup remains available.

The caller supplies a title, optional field hint, native font, byte limit and
whether input is secret. Drafts accept printable ASCII and are bounded to 64
bytes. Callers apply their own field-specific limits and validation. The editor
currently appends and deletes at the end; it does not move a cursor within text.
Long values show their trailing characters. Secret values start masked; Show and
Hide change only their presentation.

Character groups use multi-tap with a one-second window; a single-character key
appends immediately on every tap. Shift toggles letter
case. `123` opens digits; the first digit group cycles through 0 and 1. `#+=`
opens printable symbols, and `123` returns to digits. `ABC` returns to letters.
Space and Del edit the draft. Done emits a submission event; the caller decides
whether the value is valid and what to do with it. Back and cancellation belong to
the caller, and must not submit the value.

The input owner passes acquired, already-oriented contact samples to
`handle_text_entry_touch`. A key activates on release only if the gesture stays
within its original target and does not move more than 12 pixels. Rotation
changes cancel the gesture. Reset `TextEntryTouch` when the screen is hidden.
Use a scene with its ordinary title hidden, since this view renders its own
heading. Keep each prepared view and its title, hint and font alive and immutable
until the frame retires. Prepare a new frame after edits, then clear retired
views. On cancellation or final exit, clear the draft and every retired view;
the latter may contain a revealed value. Do not log or publish drafts or capture
them in screenshots or diagnostics.

The first tap on a Wi-Fi network highlights its row and shows its complete SSID,
security and BSSID in the button. If the caption exceeds the available width,
it scrolls in both directions with pauses at the ends. A second tap on that row
selects the network; choosing a different row starts its own preview. The
animation pauses when another surface covers the scene.
The local route selects the scanned access point by BSSID and security; persisting
that selection pins the profile to that AP and restricts roaming. Hidden and
non-printable/non-ASCII names are not supported by this local route.

The layout and interaction are implemented in source. Compilation, visual
inspection, touch comfort, rotation and physical-device behavior have not been
validated for this component.
