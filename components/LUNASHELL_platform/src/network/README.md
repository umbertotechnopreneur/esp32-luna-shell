# Radio and pairing lifecycle

Wi-Fi station lifecycle, Bluetooth admission, network provisioning, phone
pairing coordination and the separate CCP1 Codex-link commissioning/liveness
owner. `LUNASHELL_radio_memory.hpp` is private radio-admission support.

Feature-disabled stubs remain beside their real owners. Public state and service
interfaces stay in `include/`; this folder does not own UI routes or HTTP rendering.
