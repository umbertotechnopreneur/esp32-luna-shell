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

"""Fail closed when changed repository files contain common secret literals."""

from __future__ import annotations

import re
import argparse
import hashlib
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FIRMWARE_IMAGE_SUFFIXES = frozenset({".bin", ".elf", ".hex", ".uf2", ".dfu"})
SOURCE_SUFFIXES = frozenset({
    ".c", ".cc", ".cpp", ".h", ".hpp", ".s", ".py", ".ps1", ".psm1", ".sh", ".bat", ".cmd",
    ".cs", ".csproj", ".sln", ".js", ".ts", ".vue", ".html", ".css", ".json", ".yml", ".yaml",
    ".toml", ".ini", ".conf", ".config", ".defaults", ".default", ".cmake", ".mk", ".projbuild",
    ".md", ".txt", ".csv", ".patch", ".lock", ".ci", ".key", ".pem",
})
KNOWN_PUBLIC_MARKERS = (
    b"LUNASHELL-SIMULATOR-ONLY-" + b"NOT-A-SECRET",
)
# Read-only vendor references: one public demo network password and one storage-key name.
# Each exception binds the original whole-file bytes AND the exact matched assignment.
# Any changed file, new literal or other signature still requires inspection and fails closed.
REVIEWED_VENDOR_LITERALS = {
    ("hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.43C/software/esp-idf-v5.5.3/"
     "03_WIFI_STA/components/esp_wifi_bsp/esp_wifi_bsp.c"): (
        "06b3f1193e24724eeed99aa1167a79c1c1c13ac73aa5f2be37f6cb1026b3a00c",
        frozenset({("hard-coded credential assignment",
                    "ff5915cb53eb8d7b68dd92f6a1c73e8408ef6412288b8641885b105f82150314")}),
    ),
    ("hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.75C/software/esp-idf-bsp/"
     "03_esp-brookesia/components/brookesia_core/systems/speaker/esp_brookesia_speaker_manager.hpp"): (
        "7b57a65e241b3e5b5bba8b28be189655148f5bc53dcb35bc6ae81eca0620e5c3",
        frozenset({("hard-coded credential assignment",
                    "b729d07e183cc7b90f55d96a799e45125d750dd43eccfcacaaebc85c36ced001")}),
    ),
}
PATTERNS: tuple[tuple[str, re.Pattern[bytes]], ...] = (
    (
        "private key material",
        re.compile(b"-----BEGIN " + b"(?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
    ),
    ("AWS access key", re.compile(rb"\bAKIA[0-9A-Z]{16}\b")),
    ("GitHub token", re.compile(rb"\bgh[pousr]_[A-Za-z0-9]{30,}\b")),
    ("OpenAI token", re.compile(rb"\bsk-(?:proj-)?[A-Za-z0-9_-]{20,}\b")),
    ("Slack token", re.compile(rb"\bxox[baprs]-[A-Za-z0-9-]{20,}\b")),
    ("Google API key", re.compile(rb"\bAIza[0-9A-Za-z_-]{35}\b")),
    ("Stripe live secret", re.compile(rb"\bsk_live_[0-9A-Za-z]{20,}\b")),
    (
        "hard-coded credential assignment",
        re.compile(
            rb"(?:api[_-]?key|password|passwd|client[_-]?secret|"
            rb"access[_-]?token)\s*[:=]\s*[\"'][^\"'\r\n]{8,}[\"']",
            re.IGNORECASE,
        ),
    ),
)


# args: Literal Git arguments for a NUL-delimited file listing.
# Raises subprocess.CalledProcessError when Git cannot resolve the requested listing.
def git_paths(*args: str) -> set[Path]:
    result = subprocess.run(
        ["git", *args],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    return {
        Path(raw.decode("utf-8", errors="surrogateescape"))
        for raw in result.stdout.split(b"\0")
        if raw
    }


# relative: Repository-relative name, inspected without opening media or firmware images.
def is_source_path(relative: Path) -> bool:
    return (not relative.suffix or relative.suffix.lower() in SOURCE_SUFFIXES or
            relative.name.startswith(("sdkconfig.", ".env")))


# base_commit: Optional verified commit covering changes already checkpointed locally.
# Raises subprocess.CalledProcessError for an unreadable index, worktree or comparison.
def changed_paths(base_commit: str | None = None) -> list[Path]:
    paths = set()
    paths.update(git_paths("diff", "--name-only", "-z", "--diff-filter=ACMR"))
    paths.update(
        git_paths(
            "diff", "--cached", "--name-only", "-z", "--diff-filter=ACMR"
        )
    )
    paths.update(git_paths("ls-files", "--others", "--exclude-standard", "-z"))
    if base_commit is not None:
        paths.update(git_paths(
            "diff", "--name-only", "-z", "--diff-filter=ACMR", base_commit, "HEAD", "--"
        ))
    return sorted(paths, key=lambda path: path.as_posix().casefold())


# reference: Caller-supplied revision, resolved to a commit before use as a diff argument.
# Raises subprocess.CalledProcessError for an invalid or ambiguous revision.
def resolve_base_commit(reference: str) -> str:
    result = subprocess.run(
        ["git", "rev-parse", "--verify", "--end-of-options", f"{reference}^{{commit}}"],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    return result.stdout.strip()


# argv: Optional CLI arguments; None reads the command line supplied by device.ps1.
# Raises original Git and filesystem errors instead of claiming a successful empty scan.
def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-ref", help="Also scan current files changed since this verified commit.")
    parser.add_argument("--source-only", action="store_true",
                        help="Inspect changed source/configuration files; exclude imported media and archives.")
    args = parser.parse_args(argv)
    base_commit = resolve_base_commit(args.base_ref) if args.base_ref else None
    findings: list[str] = []
    scanned = 0
    excluded = 0
    reviewed_count = 0
    for relative in changed_paths(base_commit):
        # Publication inspects source literals; firmware images are never a secret-scan input.
        if relative.suffix.lower() in FIRMWARE_IMAGE_SUFFIXES or (args.source_only and not is_source_path(relative)):
            excluded += 1
            continue
        path = ROOT / relative
        if not path.exists():
            continue
        if path.is_symlink():
            findings.append(f"{relative.as_posix()}: unsupported changed symlink")
            continue
        resolved = path.resolve(strict=True)
        try:
            resolved.relative_to(ROOT)
        except ValueError:
            findings.append(f"{relative.as_posix()}: path escapes repository")
            continue
        if not resolved.is_file():
            continue

        content = resolved.read_bytes()
        reviewed = REVIEWED_VENDOR_LITERALS.get(relative.as_posix())
        reviewed_matches = (reviewed[1] if reviewed is not None and
                            hashlib.sha256(content).hexdigest() == reviewed[0] else frozenset())
        for marker in KNOWN_PUBLIC_MARKERS:
            content = content.replace(marker, b"")
        scanned += 1
        for label, pattern in PATTERNS:
            for match in pattern.finditer(content):
                if (label, hashlib.sha256(match.group()).hexdigest()) in reviewed_matches:
                    reviewed_count += 1
                    continue
                findings.append(f"{relative.as_posix()}: {label}")
                break

    if findings:
        print("changed-files secret scan: FAIL")
        for finding in findings:
            print(finding)
        return 1

    print(
        "changed-files secret scan: PASS "
        f"({scanned} changed or untracked files, {len(PATTERNS)} signatures, "
        f"{excluded} assets excluded, {reviewed_count} exact public vendor literals reviewed)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
