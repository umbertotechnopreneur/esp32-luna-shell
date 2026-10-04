# Cody / CCP pairing quick start

Date: 4 October 2026, Asia/Saigon.
Requested by: Umberto Giacobbi, project owner.

Created a separate English, two-page A5 portrait leaflet:
[`cody-ccp-pairing-quickstart-a5-v1.pdf`](../Cody/cody-ccp-pairing-quickstart-a5-v1.pdf).
Print at 100%, two-sided, flipping on the long edge. PDF viewer preferences
also request no scaling and long-edge duplex. Printer settings remain the
user's responsibility; physical printing was not verified.

## Content and design

- Front: prerequisites, ZIP extraction, one local-marketplace registration
  command, installation/enabling in the desktop plugin directory, and the
  private-network firewall reminder.
- Back: the six-character display code, example Codex pairing prompt,
  confirmation, Let's go, ten active seconds in Idle, finding the paired PC,
  connection, and concise retry guidance.
- Same atlas navy/cyan palette and embedded Segoe UI/JetBrains Mono fonts.
  Reuses two unmodified ImageGen illustrations from the connection atlas.
- One handwritten-note line per side; illustrative code clearly labelled.

## ZIP distribution assumption

This is a packaging concept, not a released or installed ZIP. The leaflet
assumes an archive named `Cody-CCP.zip` containing this root:

```text
CCP/
  .agents/plugins/marketplace.json
  plugins/cody-codex-plugin/
    plugin.json
    mcp.json
    .codex-plugin/plugin.json
    .mcp.json
    mcp/
    skills/
    protocol/
    README.md
```

The proposed marketplace has `name: cody-ccp`,
`interface.displayName: Cody CCP`, and an entry for `cody-codex-plugin` with
local source `./plugins/cody-codex-plugin`, installation `AVAILABLE`,
authentication `ON_INSTALL`, and category `Productivity`.
These distribution wrapper files were not added to the actual plugin.

From the extracted `CCP` folder, `codex plugin marketplace add .` registers
the source. It is not itself the plugin-install action; the leaflet then
directs the user to restart the desktop client and install/enable the plugin.
Prerequisites explicitly include Codex CLI, a desktop client supporting local
plugins, Windows, and Python available as `python`.

Installation guidance was checked against OpenAI's
[Package your plugin](https://developers.openai.com/plugins/build/plugins)
documentation on 4 October 2026. The PDF includes a clickable source link.
Client/account support can vary; no invented Install ZIP action is shown.
The local CCP README, skill, protocol, current source labels and connection
atlas supply the pairing behavior. No future session/task transport is claimed.

## Authoring and review

Primary output and source live alongside the other Obsidian atlases:
`C:\Users\umber\OneDrive\Obsidian\30_Engineering\Products\Cody Companion\atlas`.

- Authoring source: `source/build_ccp_pairing_quickstart_a5.py`.
- Review record: `source/ccp-pairing-quickstart-a5-review.json`.
- Reused images: `codex-connection-assets/cody-codex-hero.png` and
  `codex-connection-assets/display-code-to-codex.png`.
- Local render evidence: `artifacts/document-review/ccp-quickstart-a5/`.

Both PDF pages were rendered with Poppler at a 1600-pixel long edge and
visually inspected for clipping, alignment, text legibility and flow order.
PDF metadata reports two pages, each 419.528 by 595.276 points (A5 portrait).
Existing atlases and their sources were not rebuilt or replaced.

Document authoring and review only: no firmware tests, build, simulator,
plugin installation/execution, LAN changes or physical-device checks.
