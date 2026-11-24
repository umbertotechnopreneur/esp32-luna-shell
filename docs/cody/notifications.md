> **File name** `notifications.md`<br>
> **Full path** `E:\Cody\esp32-luna-shell\docs\cody\notifications.md`<br>
> **Path relative to the repository root** `docs/cody/notifications.md`

# Device notifications

Notifications use semantic types independent of their provider: `Message`,
`Information`, `Question`, `Success`, `Warning` and `Error`. The type selects an
existing semantic icon and color. A source label identifies the integration or
originating chat separately from the notification title and detail.

```mermaid
flowchart TB
  accTitle: Local and remote notification ownership
  accDescr: Local records and authenticated broker events retain their original owner when the display merges and reads them.
  producer["Local integration<br/>Semantic type, source and bounded text"]
  broker["Authenticated PC broker<br/>Owns remote events and acknowledgements"]
  local["CODY_notifications<br/>Owns local IDs, unread state and 32 retained records"]
  inbox["Cody inbox presenter<br/>Combines available pages and retains origin plus exact ID"]
  user["User opens a detail or confirms a read"]
  sound["Audio owner<br/>Optional cue on the original announcement"]

  producer -->|"Publish a local record"| local
  producer -->|"Announce once when policy permits"| sound
  local -->|"Coherent local records"| inbox
  broker -->|"Authenticated remote rows"| inbox
  inbox -->|"Current detail and read action"| user
  user -->|"Confirmed action for the visible item"| inbox
  inbox -->|"Local ID"| local
  inbox -->|"Remote owner and exact event ID"| broker

  classDef external fill:#fff0d8,stroke:#9b6218,color:#51330e
  classDef owner fill:#dcf0e9,stroke:#20766a,color:#143e38
  classDef view fill:#e6effb,stroke:#315a92,color:#18334f
  class producer,user external
  class broker,local,sound owner
  class inbox view
```

`CODY_notifications` owns local device notifications. Producers supply a type,
printable ASCII source, title, detail and positive monotonic arrival time. The
owner assigns a stable ID and unread state. Invalid or oversized input is
rejected without replacing a retained record. No account-specific information
is inferred from other sources.

The local history holds 32 records in RAM, newest first, with coherent pages of
three records. Marking an exact ID read preserves the record. A full history
replaces its oldest record and exposes the replacement count. The history
survives provider disconnects and reconnects during the same boot; restart or
power loss clears it. It performs no persistent writes and does not use the
WhatsApp credential partition.

The authenticated Codex broker remains the owner of remote events and their
acknowledgements. The UI must preserve the origin and exact owner ID when
merging sources, opening a detail or confirming a read. Local actions do not
require the PC. A touch acquired before a row changes must never confirm the
replacement row. Source unavailability must not hide the other source's
retained history.

The current merged list places retained local records first, newest first,
followed by the broker's remote ordering. Read local records remain in that
section until replaced or restarted. Remote requests retain the broker's own
question priority within its section. A remote page being fetched is labelled
as updating; rows from another page are not substituted into its positions.

Banner display, sound playback and read state are separate. A producer may
announce a new notification once through the existing audio owner. Reading,
scrolling, reopening and marking history read do not replay that sound.

WhatsApp currently publishes type `Message`, source `WhatsApp` and title
`New message`. Its notification-only protocol API exposes neither sender nor
message content. Those fields remain unavailable in the detail. Other local
integrations use the same publication and read APIs without adding a
provider-specific list or notification type.

This is a source contract. Firmware integration, memory headroom, display,
touch, sound and delivery behavior require separately authorized verification.

> **File name** `notifications.md`<br>
> **Full path** `E:\Cody\esp32-luna-shell\docs\cody\notifications.md`<br>
> **Path relative to the repository root** `docs/cody/notifications.md`
