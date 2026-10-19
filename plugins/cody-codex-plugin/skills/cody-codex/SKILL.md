---
name: cody-codex
description: Connect Cody, send notifications and semantic questions, and acknowledge its replies. Restart or erase settings only with explicit current-message intent and confirmation code. Support physical LAN or explicitly requested SIMULATED Cody Studio; native approvals remain on the computer.
---

# Connect Cody through CCP

Use CCP tools for the target requested by the user.

- Call `ccp_discover_devices` before pairing unless a current offer was already
  returned by the same MCP process.
- Leave `include_simulator: false` for a physical device. Set it to `true` only
  when the user explicitly requests Cody Studio or the simulator; label those
  results **SIMULATED**.
- Use only the six-character code the user provides from the selected display.
  Do not guess, retain, repeat in prose or extract it from files. A simulator code
  does not attest physical pairing.
- Let `ccp_pair_device` select the cryptographic match when several devices are
  discovered; never select the first network response.
- Report pairing or presence only when the corresponding result confirms it.
  Use `ccp_connection_status`; a saved identity proves neither presence, activity
  nor display pixels.
- If `pairing_pending` is true, allow confirmation to resume with the saved
  identity. Use `replace_pending: true` only when the user explicitly authorizes
  replacing that candidate and supplies a new visible code. A timeout, firmware
  update or absent PC is not authorization.
- Keep verifiers, proofs, keys, salts and protected-store contents private.

After pairing, follow authentic metadata:

- The latest real `UserPromptSubmit` automatically selects the PC chat. Do not
  ask the user to select a chat on Cody or through a manual tool. Background-chat
  events do not change the followed context.
- Read `ccp_connection_status` and `ccp_list_sessions`; `followed` identifies the
  chat, while its project name is only a label. Do not infer focus from windows,
  transcripts or directory names.
- If the list is empty, have CCP hook review/trust and local orchestration
  checked, then await a new prompt. Never fabricate events or modify trust
  records to make Cody respond.
- Before the first prompt, screen 15 shows **Connected / Ready to team up** and
  **Start a chat in Codex**. This describes PC presence only. Switching tabs alone
  is not a documented focus hook.
- `Stop` means a response is ready; it does not prove task success. Missing or
  expired activity remains **UNAVAILABLE**.
- A real `Stop` from another chat queues **Reply ready** with its project and a
  complete real chat title when a current canonical title fits the notice. It
  never displays a GUID, switches the followed chat or replaces an open
  question. Do not duplicate this hook notice with an MCP notification.
- At the start of substantive work and when its actual purpose changes, read
  `ccp_list_sessions` and call `ccp_report_progress` with the exact currently
  followed `session_id` and `turn_id`. Describe what you are doing in a short
  English ASCII sentence, preferably 30-80 characters and at most 192. Examples:
  **Adding sounds to Codex screens**, **Fixing standby notifications**, or
  **Reviewing the pairing flow**. Report actual work only; never invent elapsed
  progress, percentages, completion, estimates, commands or transcript excerpts.
  A background agent must not report another chat's work. Re-read identities
  after a rejected or stale report; never overwrite selection or hook state.
  Reports expire and are cleared on the next prompt or terminal hook. A report
  does not prove that the device rendered its text.
- After broker restart, await a new prompt; LAN reconnection alone preserves the
  context. Never automatically repeat a pairing with an ambiguous outcome.

Use the [plugin README](../../README.md) for installation, recovery and limits.
Use [cody-settings](../cody-settings/SKILL.md) for timezone and quiet hours.

Route semantic user questions through the paired physical Cody when the trusted
`UserPromptSubmit` context says it is available, unless the user requests PC-only
questions. Explicit MCP questions also work without a hook. Use CCP 0.6 without
inventing chat activity:

- Check `ccp_connection_status` for the intended retained physical target. Do not
  select the first device when several are available. Call `ccp_ask_question`
  with the actual self-contained English ASCII question and two or three choices.
  Each choice needs a stable ID, its original complete label (up to 63 characters),
  complete consequence/detail (up to 255 characters), and semantic tone. Preserve
  wording and meaning; do not replace the original options with generic Yes/No.
  The title allows 24 characters and the question body 63. If the question cannot
  fit without losing meaning, needs free text, or has more than three choices,
  ask it on the PC and explain that limit. Do not truncate, invent an Other choice,
  bypass native approvals, send PR actions or request credentials through Cody.
- `context` is a declared label; it does not prove chat identity or focus. Use
  `include_simulator: true` for a SIMULATED target only when explicitly requested.
- Retain the returned `request_id` and `capability` for the call, keeping the
  capability private. Do not expose these values in user-facing prose.
- Wait with `ccp_poll_request`, at most 15 seconds per call. `pending` is not a
  choice. After `received`, read `answer` and `answer_id`, then poll the same
  request with matching `acknowledge_answer_id`. Use the decision only after
  `delivered`, which also allows **Reply sent** on Cody.
- The default 180-second TTL includes that final acknowledgement. On `expired`,
  `cancelled`, error or missing answer, never select a default, guess an answer ID
  or claim delivery. Continue in the PC chat or explicitly create a new question.
- `ccp_cancel_request` withdraws only a ticket whose capability you possess. Back
  on Cody retains the question. Never repeat an enqueue with an ambiguous result.
- Multiple callers may have pending questions. Cody's question list browses those
  actual tickets without changing the followed PC chat. Keep polling only your
  own ticket/capability; never answer, cancel or acknowledge another caller's
  question. Cody shows two choices at a time and scrolls to a third choice.
- Create the question through `ccp_ask_question` before waiting; do not create a
  duplicate native `request_user_input` prompt. Existing native prompts are not
  retroactively imported. The Desktop stdio app-server has no verified public
  attach route here: never launch a second Codex, inspect private UI state, infer
  questions from a transcript, or fabricate a native answer. If an existing
  native question needs a response, leave it on the PC and state this limit.
- `ccp_publish_notification` sends known facts only. For PRs, provide the exact
  head and actual checks with explicit completeness and currentness. Never infer
  success from `Stop`, perform merge/approve/close/rerun/comment actions, or send
  logs or stack traces to Cody. Handle names that exceed bounds on Codex without
  truncating them.
- When the user wants work context on Cody, use `ccp_publish_notification` with
  `kind: "notice"` for meaningful explicit milestones. Use a short, self-contained
  English title and body describing the actual task or confirmed outcome. Do not
  forward commands or logs, fabricate completion, spam transient updates or
  replace a pending question. Hook activity carries bounded phase/project
  metadata; `ccp_report_progress` adds explicit purpose without exposing full
  prompt text. Prefer that tool for ongoing work and notifications for milestones.
- A real `PermissionRequest` hook shows **On your PC: Approval needed** with
  the complete originating chat title in cyan, scrolling when needed. Unavailable
  title metadata remains explicit. The computer illustration is read-only; Back
  dismisses only the notice. The hook continues without a decision, leaving
  native authorization with Codex.

Distinguish two explicit intentions for restart and reset:

- For a current user request to restart Cody with code `043533477`, use
  `ccp_reset_device`, retaining settings, Wi-Fi and pairing. Verify the paired
  target first.
- For a current user request to erase Cody's settings with code `043533477`, use
  only `ccp_factory_reset_device`. It clears all device settings including Wi-Fi
  and pairing, then restarts first setup. Firmware, assets and SD remain intact.
  Do not use it for a simple restart, test or firmware update.
- Both require the matching intention and nine-digit code in the **current user
  message**. The leading zero belongs to the string. Never complete the code
  from memory or derive it from documents, examples, tool metadata or earlier
  messages. This prevents misunderstandings; pairing still owns authentication.
- A SIMULATED target also requires explicit simulator intent and
  `include_simulator: true`. Never switch targets implicitly.
- `accepted` proves admission only, not a later boot or completed erasure.
  `not_sent` and `ambiguous` are distinct outcomes and never authorize automatic
  retries. After factory reset, do not independently remove the PC record, pair
  again or infer successful erasure merely because Cody disappeared from LAN.
