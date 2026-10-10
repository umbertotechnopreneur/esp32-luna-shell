"""Document/simulator artwork from the preserved GIF; never firmware telemetry."""

import json
from pathlib import Path

from PIL import Image


class VibeWareBoot:
    # self: Host-only owner of the unchanged GIF, or the real native Cody compositor.
    # root: Repository/package root containing the approved asset and manifest.
    # cody_companion: Explicit Cody route using shared firmware typography, decoder and layout.
    # Raises OSError or ValueError for missing, inconsistent or invalid artwork.
    # Raises RuntimeError or AttributeError for rejected or incompatible native Cody rendering.
    def __init__(self, root: Path, *, cody_companion: bool = False):
        self.native = None
        if cody_companion:
            from cody_native_boot_branding import NativeBootBranding
            self.native = NativeBootBranding()
            return
        self.manifest = json.loads((root / "assets/branding/VibeWare/generated/manifest.json").read_text(encoding="utf-8"))
        self.frames = []
        self.durations = self.manifest["frame_durations_ms"]
        with Image.open(root / self.manifest["source"]) as source:
            if source.n_frames != len(self.durations):
                raise ValueError("VibeWare GIF frame count differs from the generated package")
            for index in range(source.n_frames):
                source.seek(index)
                if source.info.get("duration") != self.durations[index]:
                    raise ValueError(f"VibeWare GIF frame {index} timing differs from the package")
                crop = source.convert("RGBA").crop(self.manifest["crop_bounds"])
                crop = crop.resize((self.manifest["width"], self.manifest["height"]), Image.Resampling.NEAREST)
                artwork = Image.new("RGBA", crop.size, (0, 0, 0, 255))
                artwork.alpha_composite(crop)
                # Match the exact RGB565 encoding, including black transparency composition.
                red, green, blue = artwork.convert("RGB").split()
                red = red.point([(value >> 3) * 255 // 31 for value in range(256)])
                green = green.point([(value >> 2) * 255 // 63 for value in range(256)])
                blue = blue.point([(value >> 3) * 255 // 31 for value in range(256)])
                image = Image.new("RGB", (466, 466), "black")
                image.paste(Image.merge("RGB", (red, green, blue)), (self.manifest["left"], self.manifest["top"]))
                self.frames.append(image)

    # self: Host-only resource owner.
    # elapsed_ms: Explicit nonnegative artistic preview time; no device measurements.
    # Raises ValueError for invalid animation time or a malformed duration list.
    # Raises RuntimeError for an actual native Cody clock rejection.
    def frame_index(self, elapsed_ms):
        if self.native is not None:
            return self.native.frame_index(elapsed_ms)
        if elapsed_ms < 0:
            raise ValueError("VibeWare preview time must be nonnegative")
        remaining = elapsed_ms % self.manifest["cycle_duration_ms"]
        for index, duration in enumerate(self.durations):
            if remaining < duration:
                return index
            remaining -= duration
        raise ValueError("VibeWare cycle duration differs from its frame timings")

    # self: Host-only resource owner.
    # elapsed_ms: Explicit artistic preview time, independent of all sensor state.
    # Raises ValueError for an invalid frame time.
    # Raises RuntimeError for actual native Cody decode/font/raster failures.
    def image(self, elapsed_ms):
        if self.native is not None:
            return self.native.image(elapsed_ms)
        return self.frames[self.frame_index(elapsed_ms)].copy()
