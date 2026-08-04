"""Static checks for the Wi-Fi/BLE internal-DMA startup contract."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, fragment: str, source: str) -> None:
    if fragment not in text:
        raise AssertionError(f"{fragment!r} missing from {source}")


def main() -> None:
    memory = read("components/qsee_platform/qsee_radio_memory.hpp")
    radios = read("components/qsee_platform/qsee_radios.cpp")
    provisioning = read("components/qsee_platform/qsee_provisioning.cpp")
    sdkconfig = read("sdkconfig.defaults")

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
