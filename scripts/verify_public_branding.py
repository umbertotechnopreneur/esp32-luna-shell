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
    "components/LUNASHELL_boot/CMakeLists.txt",
    "components/LUNASHELL_boot/include/LUNASHELL_boot.h",
    "components/LUNASHELL_boot/LUNASHELL_boot_audio.c",
    "components/LUNASHELL_logger/CMakeLists.txt",
    "components/LUNASHELL_logger/include/LUNASHELL_logger.h",
    "components/LUNASHELL_logger/LUNASHELL_logger.c",
    "components/LUNASHELL_platform/CMakeLists.txt",
    "components/LUNASHELL_platform/idf_component.yml",
    "components/LUNASHELL_platform/include/LUNASHELL_device_snapshot.hpp",
    "components/LUNASHELL_platform/include/LUNASHELL_http_json.hpp",
    "components/LUNASHELL_platform/include/LUNASHELL_http_server.hpp",
    "components/LUNASHELL_platform/include/LUNASHELL_platform.h",
    "components/LUNASHELL_platform/include/LUNASHELL_provisioning.hpp",
    "components/LUNASHELL_platform/include/LUNASHELL_settings.hpp",
    "components/LUNASHELL_platform/LUNASHELL_audio.cpp",
    "components/LUNASHELL_platform/LUNASHELL_display_settings.cpp",
    "components/LUNASHELL_platform/LUNASHELL_hardware.cpp",
    "components/LUNASHELL_platform/LUNASHELL_http_json.cpp",
    "components/LUNASHELL_platform/LUNASHELL_http_server.cpp",
    "components/LUNASHELL_platform/LUNASHELL_power.cpp",
    "components/LUNASHELL_platform/LUNASHELL_provisioning.cpp",
    "components/LUNASHELL_platform/LUNASHELL_radios.cpp",
    "components/LUNASHELL_platform/LUNASHELL_settings.cpp",
    "components/LUNASHELL_platform/LUNASHELL_storage.cpp",
    "components/LUNASHELL_platform/LUNASHELL_time.cpp",
    "components/LUNASHELL_platform/LUNASHELL_wifi.cpp",
)]
TERMS = (
    "LUNASHELL.ai",
    "LUNASHELL.AI",
    "LUNASHELL.ai",
    "hello@LUNASHELL.ai",
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
