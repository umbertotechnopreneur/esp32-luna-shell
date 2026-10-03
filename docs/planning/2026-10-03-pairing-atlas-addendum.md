# Pairing atlas addendum A01

Date: 3 October 2026, Asia/Saigon.
Requested by: Umberto Giacobbi, project owner.
Worktree: `E:\ESP32 Cody Companion HTTP and WiFi`.
Branch: `feat/http-and-wifi`.

## Requested design change

The app-installation QR and the device-provisioning QR have different purposes.
Keep installation on screen 06, remove its visible `Scan to install` title,
and insert `06-bis / Scan to pair` without renumbering existing screen IDs.
The owner requested dated, append-only addenda so the existing printouts and
handwritten notes remain useful.

`Pair Cody` starts BLE provisioning and opens 06-bis. The provisioning app scans
the device QR; receipt of credentials advances to 07. Failure leads to 08 and
Retry starts a new attempt at 06-bis. Confirmed Wi-Fi and IP lead to 09.
Back from 06 returns to 05. Back from 06-bis and Cancel from 07 close the active
session before returning to 06. Cleanup failures remain visible.

The planning addendum also records one shared capsule action control using the
existing Blue, Mint, Gold, Coral and Neutral palette families. Retry is Primary.
The new QR drawings are illustrative; real payloads belong to later firmware
integration. This checkpoint changes documentation only.

## Delivered documents and printing

The PDFs are stored outside Git, in the user's Obsidian project folder:
`C:\Users\umber\OneDrive\Obsidian\30_Engineering\Products\Cody Companion\atlas`.

| Document | Original pages retained | Appended page to print | Total pages |
| --- | --- | --- | --- |
| `cody-planning-atlas-v1.pdf` | 1-22 | 23 | 23 |
| `cody-navigation-atlas-v1.pdf` | 1-12 | 13 | 13 |

Both addenda show A01, the request date, requester, reason, affected base pages
and the single new page to print. The original inventories and printed pages
remain historical references; the addenda supply the revised decision.
The atlas README records this append-only convention for later changes.

## Document review evidence

All 34 original pages preserve their decoded content streams and extracted
text. Their rendered pixel hashes are identical before and after appending
at 96 DPI. The two new pages were rendered with Poppler and reviewed visually,
including the QR quiet zones and the navigation-arrow routing.

The machine-readable document evidence is stored beside the external sources:
`atlas/source/addendum-a01-2026-10-03-review.json`.
Original PDF backups and intermediate authoring files are local runtime output
under `artifacts/pdf-review/` in this worktree.

Final SHA-256 values:

- Planning: `e81bb67524de221c8e52773950423fa191cf9090f02b94fa4d0fa925230b2b4f`.
- Navigation: `24acebfe91c030c79a77e1f8047bae2ac238364f3a91d6192afa83d65e6a6571`.

No firmware implementation, configure/build/size pass, repository test suite,
simulator run, validation scan, device or serial operation was performed.
This Git checkpoint records the external artifact update; the PDFs themselves
are retained at the requested Obsidian paths and are not added to the repository.
