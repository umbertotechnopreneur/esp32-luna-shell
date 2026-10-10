# Documentation map

Current guides describe supported behavior; module READMEs identify code
ownership. [TODO.md](../TODO.md) tracks active work, [done.md](../done.md)
records implementation and evidence, and [lessons.md](../lessons.md) records
recurring lessons.

## Cody and CCP

| Topic | Guide |
| --- | --- |
| Cody components and ownership | [Cody architecture](architecture/cody/README.md) |
| First boot, settings, pairing, notifications and questions | [Cody flow](architecture/cody/onboarding.md) |
| Scenes, runtime clocks and Debug | [Runtime](architecture/cody/runtime.md) |
| Rig, rendering and performance | [Rendering](architecture/cody/rendering.md) |
| Clock and RTC | [Clock](architecture/cody/clock.md) |
| Installation, hooks, tools and CCP broker | [CCP guide](../plugins/cody-codex-plugin/README.md) |
| LAN packet format and authentication | [CCP1 protocol](../plugins/cody-codex-plugin/protocol/CCP1.md) |
| Windows preview and explicitly simulated pairing | [Cody Studio](../simulator/windows/README.md) |

## Illustrated documents

| PDF | Purpose |
| --- | --- |
| [Navigation Atlas](Cody/cody-navigation-atlas-v1.pdf) | Routes, returns, questions and read-only PR updates |
| [Planning Atlas](Cody/cody-planning-atlas-v1.pdf) | Screen inventory, layout and product boundaries |
| [Pairing Quickstart](Cody/cody-ccp-pairing-quickstart-a5-v1.pdf) | Four-page A5 flyer for nontechnical users |
| [CCP Connection Atlas](Cody/cody-ccp-connection-atlas-v1.pdf) | Combined journey, broker ownership, protocol, persistence and security |
| [Manga Status Atlas](Cody/cody-manga-status-atlas-v1.pdf) | Expressive cues, artwork and boot identity |

The CCP Connection Atlas consolidates the former service and Codex connection
atlases. Their original PDFs remain historical sources, rather than parallel
current guides. Screen IDs identify routes independently of physical page numbers.

Revised atlases end with a dedicated revision register. Each row places its date
and intervention type (Security, Errata, Addendum or Change) on the left, with
the description, affected pages/screens and optional supporting artwork on the
right. The shared authoring template is
[cody_pdf_revision_register.py](../scripts/common/cody_pdf_revision_register.py).
Historical dates and corrections remain traceable. The pairing flyer is the
explicit exception in [AGENTS.md](../AGENTS.md): four A5 pages, with no technical
appendices or revision register.

Author through the public CLI:

```powershell
./scripts/device.ps1 generate -Generator cody-connection-atlas
./scripts/device.ps1 generate -Generator cody-pairing-flyer
./scripts/device.ps1 generate -Generator cody-security-atlases
./scripts/device.ps1 generate -Generator cody-pdf-english
```

All document text is English, including preserved historical pages, diagrams,
captions and footers. The English revision uses exact page/text mappings in
`assets/cody/docs/translations/` and keeps original artwork and page identities.
It stores protected originals and authoring receipts under `artifacts/docs/`;
an unchanged translated PDF is skipped, and an unexpected later edit is rejected.
Use `-ToolArguments @('--redraft')` only to revise translations from those
protected originals. Review any newly authored historical content before adding
or updating its mapping; the generator never guesses a translation.

Documentation drawings are source-based illustrations, not physical captures.
Security implementation, compilation and physical verification are separate
evidence levels; pending device validation remains in the root work records.
Native approvals and PR management stay on the computer.

Earlier [pairing notes](planning/2026-10-03-pairing-atlas-addendum.md) and
[look notes](planning/2026-10-04-cody-look-atlas-addendum.md) remain historical.

## Platform and Luna

| Area | Technical guide |
| --- | --- |
| Geometry | [Circular display](architecture/circular-display-layout.md), [UI guidelines](ui/round-display-guidelines.md) |
| Display and touch | [Runtime owner](architecture/display-touch-runtime.md) |
| Startup and logging | [Startup](architecture/startup-and-logger.md), [SD/USB logs](architecture/offline-logging-usb.md) |
| Hardware data | [Snapshot](architecture/device-snapshot.md), [Hardware API](architecture/hardware-api.md) |
| Power | [Low-power policy](architecture/low-power-management.md), [Release deep sleep](architecture/release-deep-sleep.md) |
| Network | [Provisioning](architecture/connectivity-pairing.md), [HTTP API](architecture/http-device-api.md) |
| Audio | [Wake word](architecture/wake-on-word.md), [Optional TTS](architecture/text-to-speech.md) |
| Configuration | [Board configuration](architecture/board-build-configuration.md), [Build modes](architecture/firmware-build-modes.md) |
| Release | [Open-source checklist](release/open-source-pre-release-checklist.md) |

## Hardware and tooling

- [Verified hardware inventory](hardware/hardware-inventory.md)
- [ESP-IDF 6 constraints](esp-idf-6-migration.md)
- [Public development CLI](../scripts/README.md)
- [Repository rules](../AGENTS.md)
