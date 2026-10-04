# Radio and pairing lifecycle

Wi-Fi station lifecycle, Bluetooth admission, network provisioning, phone
pairing coordination and the separate CCP1 Codex-link commissioning/liveness
owner. `LUNASHELL_radio_memory.hpp` holds a 34-KiB aggregate DMA startup budget
in chunks of at most 8 KiB. Admission policy is not proof of SDK initialization;
keep the actual SDK errors and cleanup outcomes.

Feature-disabled stubs remain beside their real owners. Public state and service
interfaces stay in `include/`; this folder does not own UI routes or HTTP rendering.
