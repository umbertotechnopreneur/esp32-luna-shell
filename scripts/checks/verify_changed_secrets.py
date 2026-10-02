"""Fail closed when changed repository files contain common secret literals."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
KNOWN_PUBLIC_MARKERS = (
    b"LUNASHELL-SIMULATOR-ONLY-" + b"NOT-A-SECRET",
)
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


def changed_paths() -> list[Path]:
    paths = set()
    paths.update(git_paths("diff", "--name-only", "-z", "--diff-filter=ACMR"))
    paths.update(
        git_paths(
            "diff", "--cached", "--name-only", "-z", "--diff-filter=ACMR"
        )
    )
    paths.update(git_paths("ls-files", "--others", "--exclude-standard", "-z"))
    return sorted(paths, key=lambda path: path.as_posix().casefold())


def main() -> int:
    findings: list[str] = []
    scanned = 0
    for relative in changed_paths():
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
        for marker in KNOWN_PUBLIC_MARKERS:
            content = content.replace(marker, b"")
        scanned += 1
        for label, pattern in PATTERNS:
            if pattern.search(content):
                findings.append(f"{relative.as_posix()}: {label}")

    if findings:
        print("changed-files secret scan: FAIL")
        for finding in findings:
            print(finding)
        return 1

    print(
        "changed-files secret scan: PASS "
        f"({scanned} changed or untracked files, {len(PATTERNS)} signatures)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
