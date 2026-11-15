"""Optional real-native PDF acceptance checks, invoked by the Studio public check."""

from copy import deepcopy
from hashlib import sha256
import json
from pathlib import Path
import shutil
import subprocess
import time
import unittest
from uuid import uuid4

from PIL import Image, ImageChops

from cody_native_runtime import ROOT
from cody_studio_atlas import ATLAS_ROOT, AtlasJob, load_atlas
from cody_studio_catalog import load_catalog, pilot_entries
from cody_studio_pdf import export_atlas
from cody_studio_runner import load_capture


class AtlasPdfTests(unittest.TestCase):
    """Verify published native frames, truthful metadata and atomic PDF export."""

    # cls: Test class sharing one isolated native suite and retained artifact directory.
    # Raises native/provenance/timeout failures before any PDF is accepted.
    @classmethod
    def setUpClass(cls):
        cls.directory = ATLAS_ROOT / "pdf-checks" / uuid4().hex
        cls.directory.mkdir(parents=True, exist_ok=False)
        cls.catalog = load_catalog()
        unsupported = next(entry for entry in cls.catalog if entry["availability"] == "unsupported")
        family = next(entry for entry in cls.catalog if entry.get("scenario") and entry.get("inventory_family_id"))
        cls.family = family
        identities = [entry["id"] for entry in pilot_entries()] + [family["id"], unsupported["id"]]
        job = AtlasJob.start(identities)
        deadline = time.monotonic() + 180
        while not job.done and time.monotonic() < deadline:
            job.poll()
            if not job.done:
                time.sleep(.05)
        if not job.done:
            job.cancel()
            raise TimeoutError("Native PDF fixture suite exceeded 180 seconds")
        if job.error or job.result is None:
            raise AssertionError(f"Native PDF fixture suite failed: {job.error}")
        cls.manifest_path = job.result
        cls.manifest = load_atlas(job.result)
        cls.unsupported = unsupported

    # self: Active PDF acceptance case, without generating another document.
    # reader: The complete published PDF, including cover, overflow and register pages.
    # Raises AssertionError for missing/extra rules, cramped writing rows or content in the reserved band.
    def assert_handwriting_space(self, reader):
        from pypdf.generic import ContentStream

        for number, page in enumerate(reader.pages, start=1):
            with self.subTest(notes_page=number):
                width = float(page.mediabox.width)
                segments, rules, start = [], [], None
                for operands, operator in ContentStream(page.get_contents(), reader).operations:
                    if operator == b"n":
                        segments, start = [], None
                    elif operator == b"m":
                        start = tuple(map(float, operands[:2]))
                    elif operator == b"l" and start is not None:
                        end = tuple(map(float, operands[:2]))
                        segments.append((start, end))
                        start = end
                    elif operator in (b"S", b"s"):
                        for first, last in segments:
                            if (abs(first[1] - last[1]) < .02 and 35 < first[1] < 115 and
                                    abs(last[0] - first[0]) > width * .8):
                                rules.append((first, last))
                        segments, start = [], None
                    elif operator in (b"f", b"f*", b"B", b"B*", b"b", b"b*"):
                        segments, start = [], None
                self.assertEqual(len(rules), 3, "Every PDF page needs three blank handwriting lines")
                heights = sorted(first[1] for first, _ in rules)
                for first, last in rules:
                    self.assertAlmostEqual(first[0], 34, delta=.02)
                    self.assertAlmostEqual(last[0], width - 34, delta=.02)
                for lower, upper in zip(heights, heights[1:]):
                    self.assertAlmostEqual((upper - lower) * 25.4 / 72, 7, delta=.02)
                self.assertGreater(heights[0], 40, "Writing space must stay above the existing footer")
                positions = []
                page.extract_text(visitor_text=lambda text, current, matrix, font, size: positions.append(
                    (text, current[1] * matrix[4] + current[3] * matrix[5] + current[5])))
                writing_top = heights[-1] + 7 * 72 / 25.4
                for text, baseline in positions:
                    if text.strip():
                        self.assertFalse(heights[0] - 1 <= baseline <= writing_top,
                                         "Printed content overlaps the blank handwriting space")

    # self: Active PDF acceptance case; no native session is loaded in this process.
    # Raises PDF, image, text or Poppler failures; retained previews permit visual review.
    def test_pdf_keeps_native_pixels_and_inventory_identity(self):
        from pypdf import PdfReader

        destination = export_atlas(self.manifest_path, self.directory / "native-pilot-atlas.pdf")
        reader = PdfReader(destination)
        text = "\n".join(page.extract_text() or "" for page in reader.pages)
        captured = sum(case["acquisition"] == "captured" for case in self.manifest["cases"])
        self.assertEqual(captured, len(pilot_entries()) + 1)
        self.assertIn(f"{captured} acquired native frames / {len(self.manifest['cases'])} selected cases", text)
        self.assertIn(self.family["inventory_family_id"] + " (family)", text)
        self.assertIn(f"catalog contains {len(self.catalog)} entries", text)
        self.assertIn(self.manifest["inventory"]["revision"], text)
        self.assertIn(self.manifest["inventory"]["sha256"], text.replace("\n", ""))
        self.assertIn("FRAME UNAVAILABLE", text)
        self.assertIn(self.unsupported["id"], text)
        self.assertIn("".join(self.unsupported["reason"].split()), "".join(text.split()))
        self.assertIn("INCOMPLETE", text)
        self.assertIn("Corrections register", text)
        self.assertIn("Three blank handwriting lines spaced 7 mm apart", text.replace("\n", " "))
        self.assert_handwriting_space(reader)
        expected_images = []
        for case in self.manifest["cases"]:
            if case["acquisition"] != "captured":
                continue
            metadata = load_capture(ROOT / case["capture"]).metadata
            self.assertIn(case["inventory_id"] or case["inventory_family_id"], text)
            self.assertIn(case["rendering_mode"], text)
            self.assertIn(case["symbol"], text)
            self.assertIn(case["source"], text.replace("\n", ""))
            with Image.open((ROOT / case["capture"]).parent / metadata["png"]) as frame:
                expected_images.append(frame.convert("RGB").copy())
        embedded_images = [image.image.convert("RGB") for page in reader.pages for image in page.images]
        self.assertEqual(len(embedded_images), len(expected_images))
        remaining = list(embedded_images)
        for native in expected_images:
            matches = [index for index, embedded in enumerate(remaining)
                       if embedded.size == native.size and ImageChops.difference(embedded, native).getbbox() is None]
            self.assertTrue(matches, "Export altered a native device frame")
            remaining.pop(matches[0])
        poppler = shutil.which("pdftoppm")
        self.assertIsNotNone(poppler, "PDF visual acceptance requires Poppler pdftoppm")
        subprocess.run([poppler, "-r", "90", "-png", str(destination), str(self.directory / "page")],
                       check=True, cwd=ROOT, capture_output=True, timeout=120)
        previews = sorted(self.directory.glob("page-*.png"))
        self.assertEqual(len(previews), len(reader.pages))
        for path in previews:
            with Image.open(path) as preview:
                self.assertGreater(preview.width, 600)
                self.assertGreater(preview.height, 900)
        print(f"PDF visual review: {destination}")
        print(f"PDF rendered pages: {self.directory / 'page-*.png'}")

    # self: Active export validation case using a preserved valid native run.
    # Raises AssertionError if export overwrites an existing artifact.
    def test_existing_pdf_is_preserved(self):
        destination = self.directory / f"preserved-{uuid4().hex}.pdf"
        destination.write_bytes(b"existing owner baseline")
        with self.assertRaises(FileExistsError):
            export_atlas(self.manifest_path, destination)
        self.assertEqual(destination.read_bytes(), b"existing owner baseline")

    # self: Active export validation case using a preserved valid native run.
    # Raises AssertionError if an output outside artifacts is admitted.
    def test_export_rejects_paths_outside_artifacts(self):
        with self.assertRaisesRegex(ValueError, "beneath artifacts"):
            export_atlas(self.manifest_path, ROOT / f"forbidden-atlas-{uuid4().hex}.pdf")

    # self: Active export validation case; corruption is confined to a new fixture.
    # Raises AssertionError if a stale or substituted metadata file is admitted.
    def test_export_rejects_altered_capture_hash(self):
        manifest = deepcopy(self.manifest)
        selected = next(case for case in manifest["cases"] if case["acquisition"] == "captured")
        selected["metadata_sha256"] = "f" * 64
        path = self.directory / f"tampered-hash-{uuid4().hex}.json"
        path.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "metadata hash mismatch"):
            export_atlas(path, self.directory / f"forbidden-{uuid4().hex}.pdf")

    # self: Active export validation case; corruption is confined to a new fixture.
    # Raises AssertionError if a manifest can relabel a completed frame as another scenario.
    def test_export_rejects_scenario_or_build_substitution(self):
        for field in ("scenario_revision", "build"):
            manifest = deepcopy(self.manifest)
            if field == "build":
                manifest["build"]["library_sha256"] = "f" * 64
            else:
                selected = next(case for case in manifest["cases"] if case["acquisition"] == "captured")
                selected[field] = "f" * 64
            path = self.directory / f"tampered-{field}-{uuid4().hex}.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "identity disagrees"):
                export_atlas(path, self.directory / f"forbidden-{uuid4().hex}.pdf")

    # self: Active export validation case using an immutable historical native run.
    # Raises AssertionError if export changes its manifest, source metadata or capture bytes.
    def test_export_does_not_modify_captures_or_baselines(self):
        files = [self.manifest_path]
        for case in self.manifest["cases"]:
            if case["acquisition"] == "captured":
                metadata_path = ROOT / case["capture"]
                files.extend([metadata_path, load_capture(metadata_path).image_path])
        baselines = ROOT / "artifacts/simulator/studio/baselines"
        files.extend(path for path in baselines.rglob("*") if path.is_file())
        before = {path: sha256(path.read_bytes()).hexdigest() for path in files}
        export_atlas(self.manifest_path, self.directory / "immutable-input-check.pdf")
        self.assertEqual(before, {path: sha256(path.read_bytes()).hexdigest() for path in files})
