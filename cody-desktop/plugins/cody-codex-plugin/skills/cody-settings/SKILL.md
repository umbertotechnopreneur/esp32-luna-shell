---
name: cody-settings
description: Personalize a paired Cody with the user's preferred name, set its timezone and quiet-hours schedule through chat, or disable quiet hours while preserving other preferences.
---

Use `ccp_connection_status` to select the intended currently connected Cody. A retained pairing alone is not current presence. Do not pair again to work around missing clock capability. Use `include_simulator: true` only when the user explicitly requests SIMULATED Cody.

The broker initializes the timezone from the Windows PC on the first authenticated connection that advertises uninitialized clock settings. Cody persists that adoption marker, so later connections preserve the user's chosen zone and quiet hours. Automatic setup never guesses an unsupported zone and never retries an ambiguous write. Inspect `clock_setup` in connection status for the actual result.

Use `ccp_read_clock` for the device's current durable preferences. Use `ccp_configure_clock` for changes requested in chat; this reversible preference change does not require a reset or confirmation code. Supported zones are `UTC`, `Europe/Rome` (with daylight-saving rules), and `Asia/Ho_Chi_Minh`. An unsupported zone must be reported as unsupported, never silently replaced with UTC.

- "Quiet hours from 22:00 to 07:00" sets both `quiet_hours_start` and `quiet_hours_end` in strict `HH:MM` and `quiet_hours_enabled: true`. The interval is in Cody's selected local timezone and may cross midnight.
- "Disable quiet hours" sets only `quiet_hours_enabled: false`, retaining the schedule.
- "Set the timezone to Rome" sets only `timezone: "Europe/Rome"`, retaining quiet hours.

Quiet hours default to disabled, with a retained 22:00 to 07:00 schedule. Existing persisted preferences remain authoritative. Omit fields the user did not request; never change master sounds, volume or unrelated settings. If a timezone or interval is ambiguous, resolve it with the user before writing.

For "My name is Umberto" or an equivalent explicit name preference, call `ccp_set_name` with `preferred_name: "Umberto"` on the intended connected Cody. For "Ask me my name", ask in the chat and wait for the user's answer before calling the tool. Use an already provided chat name only when the current request clearly authorizes using it. Never infer a name from the operating-system login, private profile, repository author, pairing identity or these examples. A request to add this capability does not authorize setting the example name.

Names support at most 24 printable ASCII characters without leading or trailing spaces. Do not truncate or transliterate an unsupported name; ask which supported form the user wants. `ccp_read_name` reads its actual stored availability. "Forget my name" sets `preferred_name: ""`, clearing only that preference. Cody uses the canonical stored name in a bounded native body-font greeting line; it does not change the agent's chat identity. These tools preserve timezone, quiet hours, master sounds, volume and pairing.

Report success only after an authenticated `applied` ACK with error 0. `rejected` includes the original device error. `ambiguous` means a write may have reached Cody: read the settings after a fresh heartbeat instead of repeating the mutation. A missing fresh capability means the command was not sent; explain that Cody must be connected and awake. A settings ACK does not prove audible sound or visible device behavior.
