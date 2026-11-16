"""Host-only regressions for native publication and immutable library loading.

Run through device.ps1 check -Check cody-runtime-publication. Compiler and CDLL
entrypoints are mocked; fixtures never execute native code or access hardware.
"""
from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
import errno
import hashlib
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "cody-device/simulator/windows"))

import cody_native_runtime as runtime
from scripts.generators import generate_cody_simulator_graphics as generator


class RuntimePublicationTests(unittest.TestCase):
    # self: Each case owns a unique artifact tree and fake dependency manifest.
    # Raises filesystem errors; fixture isolation never rewrites real build output.
    def setUp(self):
        self.root = ROOT / "artifacts/checks/cody-runtime-publication" / uuid.uuid4().hex
        self.library = self.root / "artifacts/build-driver/driver.so"
        self.library.parent.mkdir(parents=True)
        self.payload = b"verified native fixture, never executable"
        self.library.write_bytes(self.payload)
        self.inputs = {}
        for relative in (
            "scripts/generators/generate_cody_simulator_graphics.py",
            "cody-device/simulator/native/CODY_host_snapshot.cpp",
            "cody-device/simulator/native/CODY_host_snapshot.hpp",
        ):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"isolated source fixture")
            self.inputs[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
        self.manifest_path = self.library.parent / "manifest.json"
        self.manifest = {"schema": "cody.host-graphics.v1", "inputs": self.inputs,
                         "library_sha256": hashlib.sha256(self.payload).hexdigest()}
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")
        self.api = SimpleNamespace(cody_host_runtime_abi=Mock(return_value=2))
        self.enterContext(patch.object(runtime, "ROOT", self.root))
        self.enterContext(patch.object(runtime, "simulator_library_path", return_value=self.library))
        self.enterContext(patch.object(runtime, "_RUNTIME_LIBRARIES", {}))

    # self: Mutable compiler output changes at the exact library-loading boundary.
    # Raises AssertionError if CDLL receives bytes other than those hashed earlier.
    def test_loader_uses_verified_bytes_after_public_path_changes(self):
        # path: Immutable generation passed to CDLL after reading build output.
        def load(path):
            self.library.write_bytes(b"a later compiler result")
            self.assertNotEqual(Path(path), self.library)
            self.assertEqual(Path(path).read_bytes(), self.payload)
            self.assertIn(self.manifest["library_sha256"], Path(path).parts)
            return self.api

        with patch.object(runtime.ctypes, "CDLL", side_effect=load) as loader:
            self.assertIs(runtime.load_runtime_library(), self.api)
        self.assertEqual(loader.call_count, 1)
        self.assertEqual(self.api._cody_library_sha256, self.manifest["library_sha256"])

    # self: Repeated callers of the same generation share a single loaded handle.
    def test_cached_runtime_does_not_publish_or_load_again(self):
        with patch.object(runtime.ctypes, "CDLL", return_value=self.api) as loader:
            first = runtime.load_runtime_library()
            with patch.object(runtime, "immutable_runtime_copy", side_effect=AssertionError("duplicate copy")):
                self.assertIs(runtime.load_runtime_library(), first)
        self.assertEqual(loader.call_count, 1)

    # self: Mismatched published bytes cannot enter a native loader or its generation cache.
    def test_rejects_mismatched_public_library(self):
        self.library.write_bytes(b"unpublished build")
        with patch.object(runtime.ctypes, "CDLL") as loader:
            with self.assertRaisesRegex(ValueError, "library hash mismatch"):
                runtime.load_runtime_library()
        loader.assert_not_called()
        self.assertFalse((self.root / "artifacts/simulator/runtime-libraries").exists())

    # self: A corrupt existing generation is not overwritten or loaded silently.
    def test_rejects_damaged_immutable_generation(self):
        immutable = runtime.immutable_runtime_copy(self.library, "graphics", self.payload,
                                                   self.manifest["library_sha256"])
        immutable.write_bytes(b"damaged copy")
        with patch.object(runtime.ctypes, "CDLL") as loader:
            with self.assertRaisesRegex(ValueError, "copy hash mismatch"):
                runtime.load_runtime_library()
        loader.assert_not_called()
        self.assertEqual(immutable.read_bytes(), b"damaged copy")

    # self: Simultaneous publishers must converge without replacing a loaded generation.
    def test_concurrent_copy_publication_is_exclusive_and_deduplicated(self):
        # unused: Concurrent caller identity; each caller has the same verified bytes.
        def publish(unused):
            return runtime.immutable_runtime_copy(self.library, "graphics", self.payload,
                                                 self.manifest["library_sha256"])

        with ThreadPoolExecutor(max_workers=4) as workers:
            paths = list(workers.map(publish, range(8)))
        self.assertEqual(len(set(paths)), 1)
        self.assertEqual(paths[0].read_bytes(), self.payload)
        self.assertEqual(list(paths[0].parent.iterdir()), [paths[0]])
        self.assertEqual(list(paths[0].parent.parent.iterdir()), [paths[0].parent])

    # self: An unrelated filesystem failure cannot masquerade as a concurrent publisher.
    def test_copy_publication_preserves_filesystem_error(self):
        with patch.object(runtime.os, "rename", side_effect=PermissionError(errno.EACCES, "denied")), patch.object(
                runtime.ctypes, "CDLL") as loader:
            with self.assertRaises(PermissionError) as caught:
                runtime.load_runtime_library()
        self.assertEqual(caught.exception.errno, errno.EACCES)
        loader.assert_not_called()
        generations = self.root / "artifacts/simulator/runtime-libraries/graphics"
        self.assertEqual(list(generations.iterdir()), [])

    # self: A loaded native state never silently changes generations within its process.
    def test_loaded_runtime_rejects_rebuilt_generation(self):
        with patch.object(runtime.ctypes, "CDLL", return_value=self.api) as loader:
            runtime.load_runtime_library()
            self.library.write_bytes(b"new generation")
            self.manifest["library_sha256"] = hashlib.sha256(self.library.read_bytes()).hexdigest()
            self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "rebuilt while loaded"):
                runtime.load_runtime_library()
        self.assertEqual(loader.call_count, 1)

    # self: Publication swaps complete files; the transient mismatched pair is rejected.
    def test_publication_never_exposes_partial_manifest(self):
        staging = self.library.parent / "staging" / uuid.uuid4().hex
        staging.mkdir(parents=True)
        staged_library = staging / self.library.name
        staged_library.write_bytes(b"new complete native fixture")
        replace = generator.os.replace
        observed = []

        # source: Fully written staging file.
        # destination: Public library or manifest receiving its atomic replacement.
        def observe_replace(source, destination):
            replace(source, destination)
            current_manifest = json.loads(self.manifest_path.read_text(encoding="utf-8"))
            observed.append(current_manifest["library_sha256"])
            if Path(destination) == self.library:
                with self.assertRaisesRegex(ValueError, "library hash mismatch"):
                    runtime.load_runtime_library()

        with patch.object(generator.os, "replace", side_effect=observe_replace), patch.object(
                runtime.ctypes, "CDLL", return_value=self.api) as loader:
            generator.publish_runtime_build(staged_library, self.library, self.manifest_path, self.inputs, [])
            self.assertEqual(loader.call_count, 0)
            runtime.load_runtime_library()
        self.assertEqual(observed, [self.manifest["library_sha256"],
                                   hashlib.sha256(self.library.read_bytes()).hexdigest()])
        self.assertEqual(loader.call_count, 1)

    # self: Mock compiler writes only staged partial output before failing.
    def test_compile_failure_preserves_previous_library_and_manifest(self):
        original_manifest = self.manifest_path.read_bytes()

        # command: Literal linker argument vector; no executable is launched.
        # output: Unique build directory used for compiler diagnostics.
        # name: Stage identity, expected to be the final library link.
        def fail_link(command, output, name):
            target = Path(command[command.index("-o") + 1])
            self.assertEqual(target.parent, output)
            self.assertNotEqual(target, self.library)
            target.write_bytes(b"partial failed output")
            raise RuntimeError("synthetic linker failure")

        with patch.object(generator, "ROOT", self.root), patch.object(
                generator, "simulator_library_path", return_value=self.library), patch.object(
                generator, "prepare_host_compiler", return_value="fixture-g++"), patch.object(
                generator, "source_provenance", return_value=self.inputs), patch.object(
                generator.subprocess, "run", return_value=SimpleNamespace(returncode=0)), patch.object(
                generator, "run_stage", side_effect=fail_link):
            with self.assertRaisesRegex(RuntimeError, "synthetic linker failure"):
                generator.main(["--force"])
        self.assertEqual(self.library.read_bytes(), self.payload)
        self.assertEqual(self.manifest_path.read_bytes(), original_manifest)

    # self: Editing inputs during a successful compile cannot publish mixed provenance.
    def test_source_change_during_compile_preserves_previous_build(self):
        original_manifest = self.manifest_path.read_bytes()
        changed_inputs = {**self.inputs, "changed-during-build.cpp": "different digest"}

        # command: Mock successful linker command with a unique staging output.
        # output: Build-owned diagnostics directory.
        # name: Final link stage; no compiler process runs.
        def complete_link(command, output, name):
            Path(command[command.index("-o") + 1]).write_bytes(b"mixed source fixture")

        with patch.object(generator, "ROOT", self.root), patch.object(
                generator, "simulator_library_path", return_value=self.library), patch.object(
                generator, "prepare_host_compiler", return_value="fixture-g++"), patch.object(
                generator, "source_provenance", side_effect=[self.inputs, changed_inputs]), patch.object(
                generator.subprocess, "run", return_value=SimpleNamespace(returncode=0)), patch.object(
                generator, "run_stage", side_effect=complete_link):
            with self.assertRaisesRegex(ValueError, "inputs changed during compilation"):
                generator.main(["--force"])
        self.assertEqual(self.library.read_bytes(), self.payload)
        self.assertEqual(self.manifest_path.read_bytes(), original_manifest)

    # self: MSVC embeds output paths in options rather than a standalone -o argument.
    def test_msvc_objects_library_and_import_library_stay_in_staging(self):
        # command: Mock MSVC C compilation with an explicit /Fo object filename.
        # kwargs: Real subprocess options retained without executing a compiler.
        def compile_object(command, **kwargs):
            target = Path(next(part[3:] for part in command if part.startswith("/Fo")))
            self.assertIn("staging", target.parts)
            target.write_bytes(b"MSVC object fixture")
            return SimpleNamespace(returncode=0)

        # command: Mock MSVC DLL-link command using /Fo, /Fe and /IMPLIB paths.
        # output: Build-owned staging directory shared by all linker outputs.
        # name: Link-stage identity, expected to be compile.
        def link_library(command, output, name):
            object_directory = Path(next(part[3:] for part in command if part.startswith("/Fo")))
            library = Path(next(part[3:] for part in command if part.startswith("/Fe")))
            import_library = Path(next(part[8:] for part in command if part.startswith("/IMPLIB:")))
            self.assertEqual(object_directory, output)
            self.assertEqual(library, output / self.library.name)
            self.assertEqual(import_library, output / self.library.with_suffix(".lib").name)
            self.assertEqual(name, "compile")
            library.write_bytes(b"MSVC linked fixture")

        with patch.object(generator, "ROOT", self.root), patch.object(
                generator, "simulator_library_path", return_value=self.library), patch.object(
                generator, "prepare_host_compiler", return_value="cl"), patch.object(
                generator, "source_provenance", return_value=self.inputs), patch.object(
                generator.subprocess, "run", side_effect=compile_object), patch.object(
                generator, "run_stage", side_effect=link_library):
            generator.main(["--force"])
        self.assertEqual(self.library.read_bytes(), b"MSVC linked fixture")

    # self: Native-device object placeholders and every compiler output stay in one staging directory.
    def test_device_build_publishes_staged_outputs_and_reuses_stable_cache_key(self):
        compiled = []

        # command: Mock C compiler output command with an artifact-owned object path.
        # kwargs: Real subprocess options are accepted without launching a process.
        def compile_object(command, **kwargs):
            target = Path(command[command.index("-o") + 1])
            self.assertIn("staging", target.parts)
            target.write_bytes(b"object fixture")
            return SimpleNamespace(returncode=0)

        # command: Mock assembler or library-link command.
        # output: Unique staging directory shared by all stages.
        # name: Names the actual stage under review.
        def compile_stage(command, output, name):
            target = Path(command[command.index("-o") + 1])
            self.assertEqual(target.parent, output)
            self.assertFalse(any("{pet-" in part or "{image-" in part for part in command))
            for part in command:
                if part.endswith(".obj"):
                    self.assertEqual(Path(part).parent, output)
            target.write_bytes(b"new linked fixture" if name == "compile" else b"object fixture")
            compiled.append(name)

        # path: Staging assembly target, requiring no access to project assets.
        def write_assembly(path):
            path.write_text("# fixture", encoding="ascii")

        with patch.object(generator, "ROOT", self.root), patch.object(
                generator, "simulator_library_path", return_value=self.library), patch.object(
                generator, "select_pet_compiler", return_value="fixture-g++"), patch.object(
                generator, "source_provenance", return_value=self.inputs), patch.object(
                generator.subprocess, "run", side_effect=compile_object) as compiler, patch.object(
                generator, "run_stage", side_effect=compile_stage), patch.object(
                generator, "write_pet_assembler", side_effect=write_assembly), patch.object(
                generator, "write_device_image_assembler", side_effect=write_assembly):
            generator.main(["--device-runtime", "--force"])
            self.assertEqual(compiled, ["pet-assembly", "image-assembly", "compile"])
            self.assertEqual(compiler.call_count, 2)
            generator.main(["--device-runtime"])
            self.assertEqual(compiler.call_count, 2)
            self.assertEqual(len(compiled), 3)
        self.assertEqual(self.library.read_bytes(), b"new linked fixture")
        published = json.loads(self.manifest_path.read_text(encoding="utf-8"))
        self.assertEqual(published["library_sha256"], hashlib.sha256(self.library.read_bytes()).hexdigest())
        self.assertFalse(any("/staging/" in part or "\\staging\\" in part
                             for command in published["command"] for part in command))


if __name__ == "__main__":
    unittest.main(verbosity=2)
