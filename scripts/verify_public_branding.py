"""Verify that the public-release text surface stays neutral."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FILES = [ROOT / path for path in (
    "README.md",
    "TODO.md",
    "assets/README.md",
    "assets/sfx/sfx-config.json",
    "assets/sfx/generated/sfx.manifest.json",
    "docs/architecture/hardware-api.md",
    "docs/architecture/startup-and-logger.md",
    "docs/esp-idf-6-migration.md",
    "docs/release/open-source-pre-release-checklist.md",
    "scripts/add-source-banner.ps1",
    "scripts/capture-serial.ps1",
    "scripts/generate_boot_assets.py",
    "scripts/generate-device-assets.ps1",
    "scripts/generate-sfx.ps1",
    "scripts/verify_lazy_runtime_contract.py",
    "scripts/verify_pairing.py",
    "scripts/verify_rtc_contract.py",
    "scripts/verify_snapshot_integrity.py",
    "sdkconfig.defaults",
    "main/CMakeLists.txt",
    "main/idf_component.yml",
    "main/main.cpp",
    "components/qsee_boot/CMakeLists.txt",
    "components/qsee_boot/include/qsee_boot.h",
    "components/qsee_boot/qsee_boot_audio.c",
    "components/qsee_logger/CMakeLists.txt",
    "components/qsee_logger/include/qsee_logger.h",
    "components/qsee_logger/qsee_logger.c",
    "components/qsee_platform/CMakeLists.txt",
    "components/qsee_platform/idf_component.yml",
    "components/qsee_platform/include/qsee_device_snapshot.hpp",
    "components/qsee_platform/include/qsee_http_json.hpp",
    "components/qsee_platform/include/qsee_http_server.hpp",
    "components/qsee_platform/include/qsee_platform.h",
    "components/qsee_platform/include/qsee_provisioning.hpp",
    "components/qsee_platform/include/qsee_settings.hpp",
    "components/qsee_platform/qsee_audio.cpp",
    "components/qsee_platform/qsee_display_settings.cpp",
    "components/qsee_platform/qsee_hardware.cpp",
    "components/qsee_platform/qsee_http_json.cpp",
    "components/qsee_platform/qsee_http_server.cpp",
    "components/qsee_platform/qsee_power.cpp",
    "components/qsee_platform/qsee_provisioning.cpp",
    "components/qsee_platform/qsee_radios.cpp",
    "components/qsee_platform/qsee_settings.cpp",
    "components/qsee_platform/qsee_storage.cpp",
    "components/qsee_platform/qsee_time.cpp",
    "components/qsee_platform/qsee_wifi.cpp",
)]
TERMS = (
    "QSee.ai",
    "QSEE.AI",
    "qsee.ai",
    "hello@qsee.ai",
    "umbertogiacobbi.biz",
)


def main() -> int:
    hits: list[str] = []
    for path in FILES:
        if not path.exists():
            hits.append(f"{path.relative_to(ROOT)}: missing")
            continue
        text = path.read_text(encoding="utf-8")
        for term in TERMS:
            if term in text:
                hits.append(f"{path.relative_to(ROOT)}: contains {term}")

    if hits:
        print("public branding verification: FAIL")
        for hit in hits:
            print(hit)
        return 1

    print(
        "public branding verification: PASS "
        f"({len(FILES)} files, {len(TERMS)} markers)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
