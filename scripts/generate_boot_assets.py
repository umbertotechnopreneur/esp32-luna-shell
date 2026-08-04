#!/usr/bin/env python3
"""Generate deterministic boot assets for the 466 px round display."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from PIL import Image, ImageChops, ImageDraw, ImageFont


CANVAS = 466
CENTER = 233
SAFE_X = 73
SAFE_Y = 73
SAFE_SIZE = 320
BRAND_BLUE = (35, 121, 222)
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)


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


def render_pdf(source_pdf: Path) -> Image.Image:
    pdftoppm = find_pdftoppm()
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


def font(size: int) -> ImageFont.ImageFont:
    candidates = [
        Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts/segoeui.ttf",
        Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts/arial.ttf",
    ]
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


def main() -> None:
    parser = argparse.ArgumentParser()
    repo_root = Path(__file__).resolve().parents[1]
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
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    artwork = crop_artwork(render_pdf(args.source))
    boot = make_boot_screen(artwork)
    guide = make_safe_area_guide()

    png_path = args.output_dir / "LUNASHELL-boot-466x466.png"
    raw_path = args.output_dir / "LUNASHELL-boot-466x466.rgb565le.bin"
    guide_path = args.output_dir / "round-466-safe-area-guide.png"
    manifest_path = args.output_dir / "boot-assets.manifest.json"

    boot.save(png_path, format="PNG", optimize=True)
    save_rgb565le(boot, raw_path)
    guide.save(guide_path, format="PNG", optimize=True)

    manifest = {
        "schema_version": 1,
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
                "sha256": sha256(png_path),
            },
            {
                "path": raw_path.relative_to(repo_root).as_posix(),
                "format": "headerless RGB565 little-endian",
                "width_px": CANVAS,
                "height_px": CANVAS,
                "bytes": raw_path.stat().st_size,
                "sha256": sha256(raw_path),
            },
            {
                "path": guide_path.relative_to(repo_root).as_posix(),
                "format": "PNG RGB888 inspection guide",
                "width_px": CANVAS,
                "height_px": CANVAS,
                "sha256": sha256(guide_path),
            },
        ],
    }
    manifest_path.write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )

    expected_raw_bytes = CANVAS * CANVAS * 2
    if raw_path.stat().st_size != expected_raw_bytes:
        raise RuntimeError(
            f"RGB565 output is {raw_path.stat().st_size} bytes; "
            f"expected {expected_raw_bytes}."
        )

    print(f"Generated {png_path}")
    print(f"Generated {raw_path} ({expected_raw_bytes} bytes)")
    print(f"Generated {guide_path}")


if __name__ == "__main__":
    main()
