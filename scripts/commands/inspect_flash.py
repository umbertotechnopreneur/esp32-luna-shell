"""Inspect ESP32-S3 flash layout in RAM without printing or storing flash bytes.

The device may remain in the serial download loader after this command. No flash
erase, flash write, NVS initialization or automatic reboot is requested.
"""

from __future__ import annotations

import argparse
import binascii
import contextlib
import hmac
import io
import json
import os
import re
import struct
import sys
from pathlib import Path

TABLE_OFFSET = 0x8000
SECTOR_SIZE = 0x1000
TABLE_SIZE = SECTOR_SIZE
OTA_OFFSET = 0x10000
OTA_SIZE = 0x2000
STORE_OFFSET = 0x12000
STORE_SIZE = 0xE000
ERASED_WORD = 0xFFFFFFFF
OTA_VALID = 2
OTA_INVALID = 3
OTA_ABORTED = 4
OTA_UNDEFINED = 0xFFFFFFFF


class InspectionRefusal(ValueError):
    """Static refusal message that contains no device data."""


# message: Static diagnosis containing no flash payload or credential material.
# Raises ValueError to stop the inspection without mutating device storage.
def reject(message: str) -> None:
    raise InspectionRefusal(message)


# build_dir: Existing source-build directory containing ESP-IDF flasher metadata.
# idf_dir: Exact ESP-IDF installation recorded by the source build.
# Raises ValueError for an absent, ambiguous or out-of-build table image.
def table_image_path(build_dir: Path, idf_dir: Path) -> Path:
    description = json.loads((build_dir / "project_description.json").read_text(encoding="utf-8"))
    project = Path(__file__).resolve().parents[2] / "cody-device"
    for field, required in (("build_dir", build_dir), ("project_path", project), ("idf_path", idf_dir)):
        value = description.get(field)
        if not isinstance(value, str) or Path(value).resolve() != required.resolve():
            reject("Selected build identity, source or ESP-IDF path does not match")
    metadata_path = build_dir / "flasher_args.json"
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    images = metadata.get("flash_files")
    if not isinstance(images, dict):
        reject("Source-build flash image inventory is unavailable")
    matches = []
    for address, filename in images.items():
        try:
            offset = int(address, 0)
        except (TypeError, ValueError):
            reject("Source-build flash image inventory has an invalid offset")
        if offset == TABLE_OFFSET:
            if not isinstance(filename, str):
                reject("Source-build partition-table image name is invalid")
            matches.append(filename)
    if len(matches) != 1:
        reject("Source-build partition-table image is missing or ambiguous")
    path = (build_dir / matches[0]).resolve(strict=True)
    if not path.is_relative_to(build_dir) or path.name != "partition-table.bin":
        reject("Partition-table image is not owned by the selected source build")
    return path


# idf_dir: Explicit ESP-IDF installation used by the selected build.
# Raises ValueError if its partition parser cannot be loaded safely.
def load_idf_parser(idf_dir: Path):
    parser_dir = idf_dir / "components" / "partition_table"
    if not (parser_dir / "gen_esp32part.py").is_file():
        reject("The selected ESP-IDF partition parser is unavailable")
    sys.path.insert(0, str(parser_dir))
    import gen_esp32part

    gen_esp32part.offset_part_table = TABLE_OFFSET
    gen_esp32part.md5sum = True
    return gen_esp32part


# parser: Official ESP-IDF partition-table parser.
# data: Borrowed table bytes; never logged or retained beyond this inspection.
# Raises ValueError for malformed entries, missing/invalid checksum or overlaps.
def parse_table(parser, data: bytes):
    if len(data) == 0 or len(data) > TABLE_SIZE or len(data) % 32:
        reject("Partition table has invalid size")
    checksum_seen = False
    for offset in range(0, min(len(data), 0xC00), 32):
        item = data[offset : offset + 32]
        if item == b"\xff" * 32:
            break
        if item[:16] == parser.MD5_PARTITION_BEGIN:
            if checksum_seen:
                reject("Partition table has duplicate checksums")
            checksum_seen = True
    if not checksum_seen:
        reject("Partition table has no checksum")
    try:
        # The IDF parser may diagnose malformed bytes; never put them in a log.
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            table = parser.PartitionTable.from_binary(data)
            table.verify()
    except Exception:
        reject("Partition table checksum, entries or layout are invalid")
    entries = {}
    for index, entry in enumerate(table):
        raw = data[index * 32 : index * 32 + 32]
        # Compare complete original records, including unknown flags and padding.
        if entry.name in entries or len(raw) != 32 or raw[:2] != b"\xaa\x50":
            reject("Partition table has an ambiguous entry")
        entries[entry.name] = (entry, raw)
    return entries


# actual: Checked installed partition entries and their original 32-byte records.
# expected: Checked source-build partition entries and original records.
# Raises ValueError if any existing partition would move, change or disappear.
def validate_layout(actual, expected) -> None:
    store = expected.get("wa_store")
    if store is None:
        reject("Source build does not declare the WhatsApp store")
    item = store[0]
    if (item.type, item.subtype, item.offset, item.size) != (1, 2, STORE_OFFSET, STORE_SIZE):
        reject("Source build declares an unexpected WhatsApp store")
    if struct.unpack_from("<I", store[1], 28)[0] != 0:
        reject("Source build declares unsupported WhatsApp partition flags")
    for name, (_, raw) in actual.items():
        if name not in expected or expected[name][1] != raw:
            reject("An existing partition differs from the source build")
    extra = set(expected) - set(actual)
    if extra not in (set(), {"wa_store"}):
        reject("Source build adds unexpected partitions")
    overlap = [name for name, (entry, _) in actual.items() if name != "wa_store" and
               entry.offset < STORE_OFFSET + STORE_SIZE and entry.offset + entry.size > STORE_OFFSET]
    if overlap:
        reject("WhatsApp range overlaps an installed partition")
    ota = actual.get("otadata")
    if ota is None or (ota[0].type, ota[0].subtype, ota[0].offset, ota[0].size) != (1, 0, OTA_OFFSET, OTA_SIZE):
        reject("Installed OTA metadata partition has an unexpected layout")
    apps = [entry for entry, _ in actual.values() if entry.type == 0]
    if len(apps) != 2 or sorted(entry.subtype for entry in apps) != [0x10, 0x11]:
        reject("Installed application slots are not the expected two OTA slots")


# data: Two physical OTA sectors, read into memory without writing a file.
# Raises ValueError for empty, corrupted, pending or ambiguous OTA metadata.
def selected_ota(data: bytes) -> int:
    if len(data) != OTA_SIZE:
        reject("OTA metadata read was incomplete")
    valid = []
    for offset in (0, SECTOR_SIZE):
        sector = data[offset : offset + SECTOR_SIZE]
        if sector == b"\xff" * SECTOR_SIZE:
            continue
        seq, _label, state, crc = struct.unpack("<I20sII", sector[:32])
        expected_crc = binascii.crc32(struct.pack("<I", seq), 0xFFFFFFFF) & 0xFFFFFFFF
        if seq in (0, ERASED_WORD) or crc != expected_crc:
            reject("OTA metadata contains an invalid or incomplete entry")
        if state in (0, 1):
            reject("OTA metadata is awaiting boot verification")
        if state in (OTA_INVALID, OTA_ABORTED):
            continue
        if state not in (OTA_VALID, OTA_UNDEFINED):
            reject("OTA metadata contains an unsupported state")
        valid.append(seq)
    if not valid or (len(valid) == 2 and valid[0] == valid[1]):
        reject("OTA boot selection is absent or ambiguous")
    return (max(valid) - 1) % 2


# port: Explicit serial device selected through the public CLI.
# baud: Serial baud rate used for the read-only download-loader session.
# parser: Official ESP-IDF partition-table parser.
# expected: Validated partition entries from the selected source build.
# expected_mac: Six-byte identity privately obtained from the paired Cody.
# Raises ValueError or a loader exception if physical state cannot be verified.
def inspect(port: str, baud: int, parser, expected, expected_mac: bytes) -> None:
    import esptool

    with esptool.detect_chip(port=port, baud=baud, trace_enabled=False) as rom:
        if rom.CHIP_NAME != "ESP32-S3":
            reject("Connected chip is not ESP32-S3")
        security = rom.get_security_info()
        flags = security.get("parsed_flags")
        crypt_count = security.get("flash_crypt_cnt")
        if not isinstance(flags, dict) or not isinstance(crypt_count, int):
            reject("Flash security state is unavailable")
        for name in ("SECURE_BOOT_EN", "SECURE_DOWNLOAD_ENABLE"):
            if name not in flags or not isinstance(flags[name], bool) or flags[name]:
                reject("Secure boot or secure download state is unsupported")
        if crypt_count.bit_count() % 2:
            reject("Encrypted flash cannot be inspected by this read-only workflow")
        # ESP-IDF derives the default station MAC from this factory base MAC.
        # Custom base or interface MACs are not assumed and fail the match.
        factory_mac = rom.read_mac("BASE_MAC")
        if factory_mac is None or len(factory_mac) != 6:
            reject("Device factory identity is unavailable")
        if not hmac.compare_digest(bytes(factory_mac), expected_mac):
            reject("Serial device does not match the paired Cody identity")
        print("Serial device identity matches the paired Cody.")
        stub = rom.run_stub()
        esptool.attach_flash(stub)
        installed = parse_table(parser, esptool.read_flash(stub, TABLE_OFFSET, TABLE_SIZE, no_progress=True))
        validate_layout(installed, expected)
        print("Partition layout matches the source build, allowing only the declared WhatsApp gap.")
        print("WhatsApp partition already declared:", "yes" if "wa_store" in installed else "no")
        slot = selected_ota(esptool.read_flash(stub, OTA_OFFSET, OTA_SIZE, no_progress=True))
        print(f"Valid OTA metadata selects ota_{slot}.")
        print("The actual running slot and bootloader fallback remain unverified.")
        gap = esptool.read_flash(stub, STORE_OFFSET, STORE_SIZE, no_progress=True)
        if len(gap) != STORE_SIZE:
            reject("WhatsApp gap read was incomplete")
        blank = all(value == 0xFF for value in gap)
        del gap
        print("WhatsApp range entirely erased:", "yes" if blank else "no")
        if not blank:
            reject("WhatsApp range contains existing data; preserve it and stop")
        print("No flash write or reboot was requested; the device may remain in download mode.")
        print("Recheck physical state in the same session before any later flash operation.")


# argv: Explicit public CLI arguments, without arbitrary shell execution.
# Returns zero only after all inspected conditions have been admitted.
def main(argv: list[str] | None = None) -> int:
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument("--port", required=True)
    arguments.add_argument("--baud", type=int, required=True)
    arguments.add_argument("--build-dir", type=Path, required=True)
    arguments.add_argument("--idf-path", type=Path, required=True)
    args = arguments.parse_args(argv)
    try:
        if not 1 <= args.baud <= 4_000_000:
            reject("Serial baud rate is outside the allowed range")
        expected_id = os.environ.get("CODY_INSPECT_DEVICE_ID", "")
        if re.fullmatch(r"[0-9A-Fa-f]{12}", expected_id) is None:
            reject("A valid paired Cody identity is required in the process environment")
        expected_mac = bytes.fromhex(expected_id)
        build_dir = args.build_dir.resolve(strict=True)
        idf_dir = args.idf_path.resolve(strict=True)
        parser = load_idf_parser(idf_dir)
        expected = parse_table(parser, table_image_path(build_dir, idf_dir).read_bytes())
        inspect(args.port, args.baud, parser, expected, expected_mac)
        return 0
    except InspectionRefusal as error:
        print(f"Flash inspection stopped: {error}", file=sys.stderr)
    except Exception as error:
        # A transport/parser exception may contain raw device bytes. Do not log it.
        print(f"Flash inspection stopped: {type(error).__name__}", file=sys.stderr)
    print("No flash write was requested; device may remain in download mode.", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
