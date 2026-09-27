#!/usr/bin/env python3
"""Generate deterministic boot assets for the 466 px round display."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import uuid

from PIL import Image, ImageChops, ImageDraw, ImageFont, __version__ as PILLOW_VERSION


CANVAS = 466
CENTER = 233
SAFE_X = 73
SAFE_Y = 73
SAFE_SIZE = 320
BRAND_BLUE = (35, 121, 222)
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
ASSET_LOCK_ENVIRONMENT = "LUNASHELL_ASSET_LOCK_TOKEN"
ASSET_LOCK_DIRECTORY = ".device-asset-generator.lock"


class RollbackIncompleteError(RuntimeError):
    """Canonical files could not be fully restored; staging must be retained."""


def read_asset_lock_owner(owner_path: Path) -> dict[str, object]:
    if not owner_path.is_file() or is_reparse_point(owner_path):
        raise RuntimeError(
            f"Asset-package lock owner metadata is not a regular file: {owner_path}"
        )
    try:
        metadata = json.loads(owner_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise RuntimeError(
            f"Asset-package lock owner metadata is unreadable: {owner_path}: {error}"
        ) from error
    if not isinstance(metadata, dict) or not isinstance(metadata.get("token"), str):
        raise RuntimeError(
            f"Asset-package lock owner metadata is invalid: {owner_path}"
        )
    return metadata


def enter_asset_package_lock(repo_root: Path, owner: str) -> dict[str, object]:
    assets_root = repo_root / "assets"
    if not assets_root.is_dir() or is_reparse_point(assets_root):
        raise RuntimeError(
            f"Canonical assets root is missing or is a reparse point: {assets_root}"
        )
    lock_root = assets_root / ASSET_LOCK_DIRECTORY
    owner_path = lock_root / "owner.json"
    inherited_token = os.environ.get(ASSET_LOCK_ENVIRONMENT)
    if inherited_token:
        if not lock_root.is_dir() or is_reparse_point(lock_root):
            raise RuntimeError(
                f"Inherited asset-package lock is not a regular directory: {lock_root}"
            )
        metadata = read_asset_lock_owner(owner_path)
        if metadata["token"] != inherited_token:
            raise RuntimeError(
                f"Inherited asset-package lock token does not match {owner_path}."
            )
        return {
            "root": lock_root,
            "owner_path": owner_path,
            "token": inherited_token,
            "owned": False,
        }

    try:
        lock_root.mkdir()
    except OSError as error:
        raise RuntimeError(
            "Asset-package lock is already present or cannot be acquired at "
            f"{lock_root}. A stale lock is intentionally retained for inspection. "
            f"Details: {error}"
        ) from error

    token = uuid.uuid4().hex
    metadata = {
        "schema_version": 1,
        "token": token,
        "pid": os.getpid(),
        "owner": owner,
        "created_utc": datetime.now(timezone.utc).isoformat(),
    }
    try:
        with owner_path.open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(metadata, stream, indent=2)
            stream.write("\n")
    except (OSError, TypeError, ValueError) as error:
        raise RuntimeError(
            "Asset-package lock metadata could not be written; the fail-closed "
            f"lock remains visible at {lock_root}: {error}"
        ) from error
    return {
        "root": lock_root,
        "owner_path": owner_path,
        "token": token,
        "owned": True,
    }


def exit_asset_package_lock(lock_state: dict[str, object]) -> None:
    if not lock_state["owned"]:
        return
    lock_root = lock_state["root"]
    owner_path = lock_state["owner_path"]
    token = lock_state["token"]
    assert isinstance(lock_root, Path)
    assert isinstance(owner_path, Path)
    assert isinstance(token, str)
    metadata = read_asset_lock_owner(owner_path)
    if metadata["token"] != token:
        raise RuntimeError(
            f"Asset-package lock ownership changed at {lock_root}; lock retained."
        )
    owner_path.unlink()
    lock_root.rmdir()


def find_pdftoppm() -> Path:
    configured = os.environ.get("PDFTOPPM")
    if configured:
        candidate = Path(configured)
        if candidate.is_file():
            return candidate

    home = Path.home()
    cached = sorted(
        home.glob(
            ".cache/codex-runtimes/**/dependencies/native/poppler/"
            "Library/bin/pdftoppm.exe"
        )
    )
    if cached:
        return cached[-1]

    on_path = shutil.which("pdftoppm")
    if on_path:
        return Path(on_path)

    raise FileNotFoundError(
        "pdftoppm was not found. Set PDFTOPPM to the Poppler executable."
    )


def render_pdf(source_pdf: Path, pdftoppm: Path) -> Image.Image:
    with tempfile.TemporaryDirectory(prefix="boot-assets-") as temp_dir:
        prefix = Path(temp_dir) / "boot-art"
        subprocess.run(
            [
                str(pdftoppm),
                "-png",
                "-r",
                "144",
                "-singlefile",
                str(source_pdf),
                str(prefix),
            ],
            check=True,
        )
        return Image.open(prefix.with_suffix(".png")).convert("RGB")


def crop_artwork(rendered: Image.Image) -> Image.Image:
    # Full-bleed boot artwork already contains the round-display background.
    # Keep it intact instead of reducing it to the legacy white-logo band.
    corner_points = (
        (0, 0),
        (rendered.width - 1, 0),
        (0, rendered.height - 1),
        (rendered.width - 1, rendered.height - 1),
    )
    if all(
        max(rendered.getpixel(point)) < 200
        for point in corner_points
    ):
        return rendered

    difference = ImageChops.difference(
        rendered, Image.new("RGB", rendered.size, WHITE)
    ).convert("L")
    mask = difference.point(lambda value: 255 if value > 18 else 0)
    bounds = mask.getbbox()
    if not bounds:
        raise ValueError("The supplied logo PDF rendered as a blank page.")
    return rendered.crop(bounds)


def fit_inside(image: Image.Image, max_width: int, max_height: int) -> Image.Image:
    copy = image.copy()
    copy.thumbnail((max_width, max_height), Image.Resampling.LANCZOS)
    return copy


def save_rgb565le(image: Image.Image, output: Path) -> None:
    rgb = image.convert("RGB")
    payload = bytearray()
    pixels = rgb.tobytes()
    for offset in range(0, len(pixels), 3):
        red, green, blue = pixels[offset : offset + 3]
        value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        payload.extend(struct.pack("<H", value))
    output.write_bytes(payload)


def validate_boot_outputs(png_path: Path, raw_path: Path, guide_path: Path) -> None:
    expected_raw_bytes = CANVAS * CANVAS * 2
    if raw_path.stat().st_size != expected_raw_bytes:
        raise RuntimeError(
            f"RGB565 output is {raw_path.stat().st_size} bytes; "
            f"expected {expected_raw_bytes}."
        )
    for path in (png_path, guide_path):
        with Image.open(path) as image:
            image.load()
            if image.size != (CANVAS, CANVAS) or image.mode != "RGB":
                raise RuntimeError(
                    f"PNG contract failed for {path}: "
                    f"mode={image.mode} size={image.size}"
                )


def font_candidates() -> list[Path]:
    return [
        Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts/segoeui.ttf",
        Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts/arial.ttf",
    ]


def selected_font_path() -> Path | None:
    for candidate in font_candidates():
        if candidate.is_file():
            return candidate
    return None


def font(size: int) -> ImageFont.ImageFont:
    candidates = font_candidates()
    for candidate in candidates:
        if candidate.is_file():
            return ImageFont.truetype(str(candidate), size)
    return ImageFont.load_default()


def centered_text(
    draw: ImageDraw.ImageDraw,
    box: tuple[int, int, int, int],
    text: str,
    fill: tuple[int, int, int, int],
    text_font: ImageFont.ImageFont,
) -> None:
    bounds = draw.textbbox((0, 0), text, font=text_font)
    width = bounds[2] - bounds[0]
    height = bounds[3] - bounds[1]
    x = box[0] + ((box[2] - box[0] - width) // 2)
    y = box[1] + ((box[3] - box[1] - height) // 2) - bounds[1]
    draw.text((x, y), text, fill=fill, font=text_font)


def make_boot_screen(artwork: Image.Image) -> Image.Image:
    corner_points = (
        (0, 0),
        (artwork.width - 1, 0),
        (0, artwork.height - 1),
        (artwork.width - 1, artwork.height - 1),
    )
    if all(
        max(artwork.getpixel(point)) < 200
        for point in corner_points
    ):
        return artwork.resize((CANVAS, CANVAS), Image.Resampling.LANCZOS)

    image = Image.new("RGB", (CANVAS, CANVAS), BLACK)
    draw = ImageDraw.Draw(image)
    draw.ellipse((0, 0, CANVAS - 1, CANVAS - 1), fill=WHITE)

    # Reserve the lower half of the safe square for the canonical live log.
    logo = fit_inside(artwork, 286, 112)
    x = CENTER - (logo.width // 2)
    y = 146 - (logo.height // 2)
    image.paste(logo, (x, y))
    return image


def make_safe_area_guide() -> Image.Image:
    image = Image.new("RGB", (CANVAS, CANVAS), (11, 19, 32))
    draw = ImageDraw.Draw(image)
    draw.ellipse(
        (0, 0, CANVAS - 1, CANVAS - 1),
        fill=(247, 249, 252),
        outline=BRAND_BLUE,
        width=4,
    )
    draw.rectangle(
        (
            SAFE_X,
            SAFE_Y,
            SAFE_X + SAFE_SIZE - 1,
            SAFE_Y + SAFE_SIZE - 1,
        ),
        fill=(232, 242, 253),
        outline=BRAND_BLUE,
        width=3,
    )

    caps = {
        "CONTEXT": (169, 12, 296, 67),
        "NEXT": (398, 169, 453, 296),
        "ACTION": (169, 398, 296, 453),
        "BACK": (12, 169, 67, 296),
    }
    for label, box in caps.items():
        draw.rounded_rectangle(
            box,
            radius=24,
            fill=(194, 220, 249),
            outline=BRAND_BLUE,
            width=2,
        )
        label_size = 13 if label == "CONTEXT" else 15
        centered_text(draw, box, label, (7, 23, 42), font(label_size))

    centered_text(
        draw,
        (
            SAFE_X,
            SAFE_Y,
            SAFE_X + SAFE_SIZE - 1,
            SAFE_Y + SAFE_SIZE - 1,
        ),
        "SAFE CONTENT\n320 x 320",
        (7, 23, 42),
        font(24),
    )
    return image


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_reparse_point(path: Path) -> bool:
    """Detect POSIX symlinks and Windows junctions/symlinks without following."""

    stat_result = path.lstat()
    file_attributes = getattr(stat_result, "st_file_attributes", 0)
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    return path.is_symlink() or bool(file_attributes & reparse_attribute)


def assert_no_reparse_tree(root: Path, label: str) -> None:
    if not root.is_dir():
        raise FileNotFoundError(f"{label} is unavailable: {root}")
    for current_root, directory_names, file_names in os.walk(
        root, followlinks=False
    ):
        current = Path(current_root)
        for path in [current, *[current / name for name in directory_names],
                     *[current / name for name in file_names]]:
            if is_reparse_point(path):
                raise ValueError(
                    f"{label} contains an unsupported reparse point: {path}"
                )


def tool_version_line(executable: Path) -> str:
    result = subprocess.run(
        [str(executable), "-v"],
        check=True,
        capture_output=True,
        text=True,
    )
    lines = [
        line.strip()
        for line in (result.stdout + "\n" + result.stderr).splitlines()
        if line.strip()
    ]
    if not lines:
        raise RuntimeError(f"Version query returned no output: {executable}")
    return lines[0]


def commit_staged_files(
    entries: list[tuple[Path, Path]], staging_root: Path
) -> None:
    """Rename staged files into place; rollback all completed renames on error."""

    backup_root = staging_root / "backup"
    backup_root.mkdir()
    states: list[dict[str, object]] = []
    try:
        for index, (staged, destination) in enumerate(entries):
            if not staged.is_file():
                raise FileNotFoundError(f"Staged boot asset is missing: {staged}")
            destination.parent.mkdir(parents=True, exist_ok=True)
            backup = backup_root / f"{index:04d}.bak"
            state: dict[str, object] = {
                "destination": destination,
                "backup": backup,
                "backup_moved": False,
                "new_moved": False,
            }
            states.append(state)
            if destination.exists():
                if not destination.is_file():
                    raise RuntimeError(
                        f"Boot asset destination is not a file: {destination}"
                    )
                os.replace(destination, backup)
                state["backup_moved"] = True
            os.replace(staged, destination)
            state["new_moved"] = True
        return
    except BaseException as commit_error:
        rollback_errors: list[str] = []
        for state in reversed(states):
            destination = state["destination"]
            backup = state["backup"]
            assert isinstance(destination, Path)
            assert isinstance(backup, Path)
            try:
                if state["new_moved"] and destination.exists():
                    destination.unlink()
                if state["backup_moved"] and backup.exists():
                    os.replace(backup, destination)
            except BaseException as rollback_error:  # retain every recovery detail
                rollback_errors.append(f"{destination}: {rollback_error}")
        if rollback_errors:
            raise RollbackIncompleteError(
                f"Boot asset commit failed ({commit_error}); rollback incomplete: "
                f"{'; '.join(rollback_errors)}. Recovery files retained at "
                f"{staging_root}"
            ) from commit_error
        raise RuntimeError(
            "Boot asset commit failed; previous canonical files restored: "
            f"{commit_error}"
        ) from commit_error


def run_locked(args: argparse.Namespace, repo_root: Path) -> None:
    source_root_path = repo_root / "assets/branding/LUNASHELL/source"
    generated_root_path = repo_root / "assets/branding/LUNASHELL/generated"
    assert_no_reparse_tree(source_root_path, "Canonical boot source tree")
    assert_no_reparse_tree(generated_root_path, "Canonical boot generated tree")
    source_root = source_root_path.resolve(strict=True)
    generated_root = generated_root_path.resolve(strict=True)
    source = args.source.resolve(strict=True)
    output_dir = args.output_dir.resolve(strict=False)
    if not source.is_file() or source.suffix.lower() != ".pdf":
        raise ValueError(f"Boot source must be one PDF file: {source}")
    if output_dir.exists() and not output_dir.is_dir():
        raise ValueError(f"Boot output path is not a directory: {output_dir}")
    try:
        source.relative_to(source_root)
    except ValueError as error:
        raise ValueError(
            f"Boot source must stay below {source_root}: {source}"
        ) from error
    if output_dir != generated_root:
        raise ValueError(
            f"Boot output must be the canonical directory {generated_root}: "
            f"{output_dir}"
        )
    args.source = source
    args.output_dir = output_dir
    pdftoppm = find_pdftoppm().resolve(strict=True)
    poppler_version = tool_version_line(pdftoppm)
    artwork = crop_artwork(render_pdf(args.source, pdftoppm))
    boot = make_boot_screen(artwork)
    guide = make_safe_area_guide()
    if args.validate_only:
        with tempfile.TemporaryDirectory(prefix="boot-assets-preflight-") as temp:
            temp_root = Path(temp)
            preflight_png = temp_root / "boot.png"
            preflight_raw = temp_root / "boot.rgb565le.bin"
            preflight_guide = temp_root / "guide.png"
            boot.save(preflight_png, format="PNG", optimize=True)
            save_rgb565le(boot, preflight_raw)
            guide.save(preflight_guide, format="PNG", optimize=True)
            validate_boot_outputs(
                preflight_png, preflight_raw, preflight_guide
            )
        print(
            "Boot asset preflight: PASS "
            "(repository-confined paths, no reparse points, all outputs verified)"
        )
        return

    png_path = args.output_dir / "LUNASHELL-boot-466x466.png"
    raw_path = args.output_dir / "LUNASHELL-boot-466x466.rgb565le.bin"
    guide_path = args.output_dir / "round-466-safe-area-guide.png"
    manifest_path = args.output_dir / "boot-assets.manifest.json"
    staging_root: Path | None = None
    preserve_staging = False
    commit_completed = False
    primary_error: BaseException | None = None
    cleanup_errors: list[str] = []
    try:
        staging_root = Path(
            tempfile.mkdtemp(prefix=".boot-assets-staging-", dir=generated_root)
        )
        staged_png = staging_root / png_path.name
        staged_raw = staging_root / raw_path.name
        staged_guide = staging_root / guide_path.name
        staged_manifest = staging_root / manifest_path.name
        boot.save(staged_png, format="PNG", optimize=True)
        save_rgb565le(boot, staged_raw)
        guide.save(staged_guide, format="PNG", optimize=True)

        validate_boot_outputs(staged_png, staged_raw, staged_guide)
        expected_raw_bytes = CANVAS * CANVAS * 2
        selected_font = selected_font_path()
        manifest = {
            "schema_version": 1,
            "generator": {
                "python": sys.version.splitlines()[0],
                "pillow": PILLOW_VERSION,
                "pdftoppm": poppler_version,
                "font": (
                    {
                        "name": selected_font.name,
                        "sha256": sha256(selected_font),
                    }
                    if selected_font is not None
                    else {"name": "Pillow default", "sha256": None}
                ),
            },
            "source": {
                "path": args.source.relative_to(repo_root).as_posix(),
                "sha256": sha256(args.source),
            },
            "outputs": [
                {
                    "path": png_path.relative_to(repo_root).as_posix(),
                    "format": "PNG RGB888",
                    "width_px": CANVAS,
                    "height_px": CANVAS,
                    "sha256": sha256(staged_png),
                },
                {
                    "path": raw_path.relative_to(repo_root).as_posix(),
                    "format": "headerless RGB565 little-endian",
                    "width_px": CANVAS,
                    "height_px": CANVAS,
                    "bytes": staged_raw.stat().st_size,
                    "sha256": sha256(staged_raw),
                },
                {
                    "path": guide_path.relative_to(repo_root).as_posix(),
                    "format": "PNG RGB888 inspection guide",
                    "width_px": CANVAS,
                    "height_px": CANVAS,
                    "sha256": sha256(staged_guide),
                },
            ],
        }
        staged_manifest.write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
        )

        # Catch a junction/symlink introduced after the initial preflight.
        assert_no_reparse_tree(
            generated_root, "Canonical boot generated tree"
        )
        commit_staged_files(
            [
                (staged_png, png_path),
                (staged_raw, raw_path),
                (staged_guide, guide_path),
                (staged_manifest, manifest_path),
            ],
            staging_root,
        )
        commit_completed = True
    except BaseException as error:
        primary_error = error
        if isinstance(error, RollbackIncompleteError):
            preserve_staging = True
    finally:
        if not preserve_staging:
            if staging_root is not None and staging_root.exists():
                try:
                    shutil.rmtree(staging_root)
                except OSError as cleanup_error:
                    cleanup_errors.append(
                        f"staging cleanup failed at {staging_root}: {cleanup_error}"
                    )

    outcome = "committed" if commit_completed else "not committed"
    if primary_error is not None:
        message = f"Boot asset workflow failed ({primary_error}); state={outcome}"
        if cleanup_errors:
            message += f"; cleanup failure(s): {'; '.join(cleanup_errors)}"
        if cleanup_errors:
            raise RuntimeError(message) from primary_error
        raise primary_error.with_traceback(primary_error.__traceback__)
    if cleanup_errors:
        raise RuntimeError(
            f"Boot asset state={outcome}, but cleanup failed: "
            f"{'; '.join(cleanup_errors)}"
        )

    print(f"Generated and committed {png_path}")
    print(f"Generated and committed {raw_path} ({expected_raw_bytes} bytes)")
    print(f"Generated and committed {guide_path}")


def parse_args(repo_root: Path) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source",
        type=Path,
        default=repo_root
        / "assets/branding/LUNASHELL/source/LUNASHELL-ai-logo.pdf",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=repo_root / "assets/branding/LUNASHELL/generated",
    )
    parser.add_argument(
        "--validate-only",
        action="store_true",
        help="Validate paths, dependencies, and source artwork without canonical writes.",
    )
    return parser.parse_args()


def main() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    args = parse_args(repo_root)
    lock_state: dict[str, object] | None = None
    primary_error: BaseException | None = None
    cleanup_errors: list[str] = []
    lock_retained = False
    try:
        lock_state = enter_asset_package_lock(repo_root, "boot")
        run_locked(args, repo_root)
    except BaseException as error:
        primary_error = error
    finally:
        if lock_state is not None and lock_state["owned"]:
            if isinstance(primary_error, RollbackIncompleteError):
                lock_retained = True
            else:
                try:
                    exit_asset_package_lock(lock_state)
                except Exception as cleanup_error:
                    cleanup_errors.append(
                        "asset-package lock cleanup failed at "
                        f"{lock_state['root']}: {cleanup_error}"
                    )

    if primary_error is not None:
        message = f"Boot asset workflow failed: {primary_error}"
        if lock_retained:
            message += (
                "; fail-closed asset-package lock retained at "
                f"{lock_state['root']}"
            )
        if cleanup_errors:
            message += f"; cleanup failure(s): {'; '.join(cleanup_errors)}"
        raise RuntimeError(message) from primary_error
    if cleanup_errors:
        raise RuntimeError(
            "Boot asset workflow completed, but cleanup failed: "
            f"{'; '.join(cleanup_errors)}"
        )


if __name__ == "__main__":
    main()
