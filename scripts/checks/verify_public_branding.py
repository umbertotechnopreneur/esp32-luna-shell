"""Verify that the in-scope public-release text surface stays neutral."""

from __future__ import annotations

import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TEXT_SUFFIXES = {
    ".c",
    ".cmake",
    ".cpp",
    ".defaults",
    ".h",
    ".hpp",
    ".json",
    ".md",
    ".ps1",
    ".py",
    ".txt",
    ".yaml",
    ".yml",
}
TEXT_FILENAMES = {"CMakeLists.txt", "LICENSE"}
EXCLUDED_SEGMENTS = {
    ".git",
    ".venv",
    "__pycache__",
    "dist",
    "managed_components",
    "third-party",
    "third_party",
    "vendor",
    "esp32_s3_touch_amoled_1_75",
}

# Generated app resources remain excluded from this source-focused release scan.
DEFERRED_UI_PATHS = (
    Path("components/LUNASHELL_app"),
    Path("components/LUNASHELL_boot/src/ui/LUNASHELL_boot_ui.c"),
    Path("littlefs/apps"),
)
PERSONAL_TERMS = (
    "Author:" + " Umberto Giacobbi",
    "hello@" + "LUNA" + "SHELL.ai",
    "umbertogiacobbi" + ".biz",
)


def is_deferred_ui(path: Path) -> bool:
    relative = path.relative_to(ROOT)
    return any(
        relative == deferred or deferred in relative.parents
        for deferred in DEFERRED_UI_PATHS
    )


def is_scannable(path: Path) -> bool:
    relative = path.relative_to(ROOT)
    if any(
        part in EXCLUDED_SEGMENTS
        or part == "build"
        or part.startswith("build-")
        or part.startswith("build_")
        for part in relative.parts[:-1]
    ):
        return False
    if is_deferred_ui(path):
        return False
    return path.suffix.lower() in TEXT_SUFFIXES or path.name in TEXT_FILENAMES


def is_excluded_directory(path: Path) -> bool:
    relative = path.relative_to(ROOT)
    if any(
        part in EXCLUDED_SEGMENTS
        or part == "build"
        or part.startswith("build-")
        or part.startswith("build_")
        for part in relative.parts
    ):
        return True
    return any(
        relative == deferred or deferred in relative.parents
        for deferred in DEFERRED_UI_PATHS
    )


def discover_scannable_files() -> list[Path]:
    files: list[Path] = []
    for current_root, directory_names, file_names in os.walk(
        ROOT, topdown=True, followlinks=False
    ):
        current = Path(current_root)
        directory_names[:] = [
            name
            for name in directory_names
            if not is_excluded_directory(current / name)
        ]
        files.extend(
            path
            for name in file_names
            if (path := current / name).is_file() and is_scannable(path)
        )
    return sorted(files)


def main() -> int:
    files = discover_scannable_files()
    hits: list[str] = []
    folded_terms = tuple((term, term.casefold()) for term in PERSONAL_TERMS)
    for path in files:
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError as error:
            hits.append(f"{path.relative_to(ROOT)}: invalid UTF-8 ({error})")
            continue
        folded_text = text.casefold()
        for term, folded_term in folded_terms:
            if folded_term in folded_text:
                hits.append(f"{path.relative_to(ROOT)}: contains {term}")

    if hits:
        print("public branding verification: FAIL")
        for hit in hits:
            print(hit)
        return 1

    deferred = ", ".join(path.as_posix() for path in DEFERRED_UI_PATHS)
    print(
        "public branding verification: PASS "
        f"({len(files)} dynamically discovered text files, "
        f"{len(PERSONAL_TERMS)} personal markers; deferred UI excluded: "
        f"{deferred})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
