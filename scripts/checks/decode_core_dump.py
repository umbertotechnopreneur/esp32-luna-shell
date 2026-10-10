# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested VibeWare branding.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""Decode one captured ESP-IDF 6.0.2 UART panic with its matching firmware ELF."""

from __future__ import annotations

import argparse
import base64
import hashlib
import os
from pathlib import Path
import re
import tempfile


ROOT = Path(__file__).resolve().parents[2]
START = "================= CORE DUMP START ================="
END = "================= CORE DUMP END ================="


# capture: Complete serial text containing zero or more panic dump blocks.
# index: One-based requested crash identity, never inferred from file ordering.
# elf: Existing firmware ELF whose bytes must match the panic's printed digest.
# Raises ValueError for missing/incomplete/corrupt blocks or a mismatched ELF.
def select_dump(capture: str, index: int, elf: Path) -> tuple[str, str]:
    matches = list(re.finditer(re.escape(START) + r"(.*?)" + re.escape(END), capture, re.S))
    if index < 1 or index > len(matches):
        raise ValueError(f"Requested crash {index}; found {len(matches)} complete core dumps")
    selected = matches[index - 1]
    previous_end = matches[index - 2].end() if index > 1 else 0
    panic_text = capture[previous_end:selected.start()]
    hashes = re.findall(r"ELF file SHA256:\s*([0-9a-fA-F]{8,64})", panic_text)
    elf_hash = hashlib.sha256(elf.read_bytes()).hexdigest()
    if not hashes or not elf_hash.startswith(hashes[-1].lower()):
        raise ValueError(f"Firmware ELF mismatch or missing panic identity: ELF SHA256={elf_hash}")
    lines = [line.strip() for line in selected.group(1).splitlines() if line.strip()]
    if not lines or any(re.fullmatch(r"[A-Za-z0-9+/]+={0,2}", line) is None for line in lines):
        raise ValueError("Core dump has non-base64 content or interrupted capture")
    # Reject corrupt transfer bytes before passing the selected complete dump to
    # Espressif's checksum/target/ELF validator and GDB task-stack decoder.
    for line in lines:
        # IDF encodes each write independently; a segment's last line can have
        # padding before the next segment begins. Validate the same line framing.
        base64.b64decode(line, validate=True)
    return "\n".join(lines) + "\n", elf_hash


# No parameters: accepts explicit capture, ELF, GDB and output paths via the public CLI.
# Raises file, format, checksum or decoder failures; never opens a serial device.
def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--gdb", type=Path, required=True)
    parser.add_argument("--idf-path", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--crash", type=int, default=1)
    args = parser.parse_args()
    destination = args.output_dir.resolve()
    if not destination.is_relative_to((ROOT / "artifacts").resolve()):
        raise ValueError("Decoder output must remain under this repository's artifacts directory")
    for path in (args.capture, args.elf, args.gdb):
        if not path.is_file():
            raise FileNotFoundError(path)
    if not (args.idf_path / "components/espcoredump").is_dir():
        raise ValueError("Provide the existing official ESP-IDF installation")
    encoded, elf_hash = select_dump(args.capture.read_text(encoding="utf-8"), args.crash, args.elf)
    destination.mkdir(parents=True, exist_ok=True)
    temporary = destination / "temporary"
    temporary.mkdir(exist_ok=True)
    tempfile.tempdir = str(temporary)
    os.environ["IDF_PATH"] = str(args.idf_path.resolve())
    encoded_path = destination / f"crash-{args.crash}.b64"
    encoded_path.write_text(encoded, encoding="ascii")
    print(f"Capture: {args.capture.resolve()}; crash={args.crash}; ELF SHA256={elf_hash}")
    from esp_coredump import CoreDump

    decoder = CoreDump(chip="esp32s3", core_format="b64", core=str(encoded_path),
        prog=str(args.elf.resolve()), gdb=str(args.gdb.resolve()),
        save_core=str(destination / f"crash-{args.crash}.elf"), gdb_timeout_sec=30)
    decoder.info_corefile()


if __name__ == "__main__":
    main()
