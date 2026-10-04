"""Static checks for the Wi-Fi/BLE internal-DMA startup contract."""


from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from common.source_contracts import read_component_source

# Direct command execution resolves the shared repository package explicitly.
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from scripts.common.firmware_profile import read_profile_defaults


ROOT = Path(__file__).resolve().parents[2]


# relative: Requested source; split implementation families are read together.
# Raises OSError or UnicodeError when a required source is unavailable.
def read(relative: str) -> str:
    return read_component_source(ROOT / relative)


def require(text: str, fragment: str, source: str) -> None:
    if fragment not in text:
        raise AssertionError(f"{fragment!r} missing from {source}")


# No parameters: inspect the bounded radio startup contract without device access.
# Raises AssertionError when a required source invariant is absent.
def main() -> None:
    memory = read("components/LUNASHELL_platform/src/network/LUNASHELL_radio_memory.hpp")
    radios = read("components/LUNASHELL_platform/src/network/LUNASHELL_radios.cpp")
    provisioning = read("components/LUNASHELL_platform/src/network/LUNASHELL_provisioning.cpp")
    sdkconfig = read_profile_defaults("connected")

    require(
        memory,
        "kBleControllerDmaHoldBytes = 34 * 1024",
        "aggregate BLE DMA startup budget",
    )
    require(
        memory,
        "kBleControllerDmaChunkBytes = 8 * 1024",
        "bounded chunks instead of one fictitious 30-KiB allocation",
    )
    require(
        memory,
        "chunk_bytes, kBleControllerDmaCaps",
        "BLE controller DMA chunk allocation",
    )
    require(
        memory,
        "reservation = std::move(pending)",
        "all-or-nothing reservation publication",
    )
    require(memory, "pending.reset();", "partial reservation cleanup")
    require(memory, "largest_block < kBleControllerDmaChunkBytes",
            "startup contiguous-block policy")
    require(memory, "free_bytes < required_free", "aggregate budget admission")
    require(
        radios,
        "reserveBleControllerDma(ble_controller_dma)",
        "radio startup DMA reservation",
    )
    require(
        radios,
        'validateBleControllerDma("BLE telemetry")',
        "BLE telemetry DMA preflight",
    )
    require(
        provisioning,
        'validateBleControllerDma("BLE provisioning")',
        "BLE provisioning DMA preflight",
    )
    require(
        sdkconfig,
        "CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=4",
        "bounded Wi-Fi RX DMA buffers",
    )
    require(
        sdkconfig,
        "CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM=4",
        "bounded Wi-Fi TX DMA buffers",
    )
    require(
        sdkconfig,
        "CONFIG_ESP_WIFI_RX_BA_WIN=4",
        "Wi-Fi RX block-ack window",
    )
    require(sdkconfig, "CONFIG_BT_CTRL_BLE_SCAN=n", "unused BLE scan feature")
    require(sdkconfig, "CONFIG_BT_CTRL_DTM_ENABLE=n", "unused BLE test mode")
    require(
        sdkconfig,
        "CONFIG_BT_CTRL_BLE_MAX_ACT=2",
        "advertising plus one peripheral connection activity",
    )
    require(
        sdkconfig,
        "CONFIG_ESP_PHY_MAC_BB_PD=n",
        "unsupported MAC/BB retention allocation",
    )

    print(
        "radio memory static verification: PASS "
        "(34-KiB aggregate BLE DMA budget in bounded chunks, "
        "fail-fast budget admission, "
        "bounded Wi-Fi buffers, two BLE activities, MAC/BB retention disabled)"
    )


if __name__ == "__main__":
    main()
