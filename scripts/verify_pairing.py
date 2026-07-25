"""Static, hardware-free checks for the BLE-only Espressif pairing flow."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, fragment: str, source: str) -> None:
    if fragment not in text:
        raise AssertionError(f"{fragment!r} missing from {source}")


def main() -> None:
    provisioning = read("components/qsee_platform/qsee_provisioning.cpp")
    provisioning_header = read("components/qsee_platform/include/qsee_provisioning.hpp")
    platform_header = read("components/qsee_platform/include/qsee_platform.h")
    wifi = read("components/qsee_platform/qsee_wifi.cpp")
    navigation = read("components/qsee_app/qsee_navigation.cpp")
    navigation_header = read("components/qsee_app/include/qsee_navigation.hpp")
    cmake = read("components/qsee_platform/CMakeLists.txt")
    app_cmake = read("components/qsee_app/CMakeLists.txt")
    settings = read("components/qsee_platform/qsee_settings.cpp")

    require(provisioning_header, "class ProvisioningService final", "provisioning header")
    require(provisioning, "wifi_prov_scheme_ble", "provisioning implementation")
    require(provisioning, "WIFI_PROV_SECURITY_1", "provisioning implementation")
    require(provisioning, '\\"pop\\":\\"\\"', "provisioning QR payload")
    require(provisioning, '\\"transport\\":\\"ble\\"', "provisioning QR payload")
    if "wifi_prov_scheme_softap" in provisioning or "WIFI_PROV_SECURITY_0" in provisioning:
        raise AssertionError("SoftAP or unsecured provisioning was reintroduced")

    for screen in (
        "WifiCredentials",
        "PairIntro",
        "PairBluetooth",
        "PairApp",
        "PairQr",
        "AppStoreQr",
        "PairResult",
    ):
        require(navigation_header, screen, "navigation header")
    for fragment in ("startPairing", "showPairingQr", "showAppStoreQr", "renderQr"):
        require(navigation, fragment, "navigation implementation")
    require(navigation, "lv_textarea_set_password_mode", "masked password editor")
    require(navigation, "clearWifiPassword", "password editor cleanup")
    require(platform_header, "qsee_wifi_update_credentials", "platform header")
    require(wifi, "secure_zero", "Wi-Fi secret cleanup")
    require(provisioning, "service.initialize()", "provisioning start wrapper")

    require(cmake, '"qsee_provisioning.cpp"', "platform CMake")
    require(cmake, '"qsee_settings.cpp"', "platform CMake")
    require(app_cmake, "qrcode", "app CMake")
    require(settings, "UserSettingsService::instance", "settings singleton")

    print("pairing static verification: PASS")


if __name__ == "__main__":
    main()
