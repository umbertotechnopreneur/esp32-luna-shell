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


def main() -> None:
    memory = read("components/LUNASHELL_platform/LUNASHELL_radio_memory.hpp")
    radios = read("components/LUNASHELL_platform/LUNASHELL_radios.cpp")
    provisioning = read("components/LUNASHELL_platform/LUNASHELL_provisioning.cpp")
    sdkconfig = read_profile_defaults("connected")

    require(
        memory,
        "kBleControllerDmaReservationBytes = 0x7800",
        "BLE controller DMA contract",
    )
    require(
        memory,
        "kBleControllerDmaHeadroomBytes = 4096",
        "BLE controller DMA headroom",
    )
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
        "(0x7800-byte BLE DMA reservation, fail-fast preflight, "
        "bounded Wi-Fi buffers, two BLE activities, MAC/BB retention disabled)"
    )


if __name__ == "__main__":
    main()
