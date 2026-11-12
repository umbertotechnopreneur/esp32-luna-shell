<img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="72">

# Radio and pairing lifecycle

Wi-Fi station lifecycle, Bluetooth admission, network provisioning, phone
pairing coordination and the separate CCP1 Codex-link commissioning/liveness
owner. `LUNASHELL_radio_memory.hpp` holds a 34-KiB aggregate DMA startup budget
in chunks of at most 8 KiB. Admission policy is not proof of SDK initialization;
keep the actual SDK errors and cleanup outcomes.

Feature-disabled stubs remain beside their real owners. Public state and service
interfaces stay in `include/`; this folder does not own UI routes or HTTP rendering.

The optional Cody OpenRouter catalog worker fetches each exact configured Codex
model from the public per-model API over certificate-validated HTTPS without an API key. These are
OpenRouter base prices in USD per token, not Codex billing or official OpenAI
pricing; the tiered validity and value flags distinguish absent override data
from reported additional price overrides. The request
exposes the device's network address to OpenRouter but sends no chat or model
selection. The worker refreshes every 12 hours and retries failures after five
minutes; it checks canonical Wi-Fi and clock state every ten seconds before TLS.
The nine configured identities and their quote buffers are bounded in static
memory; one PSRAM response buffer is capped at 8192 bytes with nesting and node
limits. Generation-consistent pages of up to eight records are read through
`DeviceSnapshotService`. Each identity remains visible when no quote exists;
HTTP result, failed refresh, missing fields, actual clock sample and monotonic
fetch time remain explicit per model. The TLS and JSON libraries also allocate
memory; their peak use has not been measured here.
The direct cJSON manifest pin matches the existing Cody dependency locks;
the updated manifest and affected build profiles still require lock verification
and an authorized build before this integration can be considered validated.

---

**VibeWare** · Created by Umberto Giacobbi · [Cody on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) · [VibeWare](https://github.com/umbertotechnopreneur/VibeWare)
