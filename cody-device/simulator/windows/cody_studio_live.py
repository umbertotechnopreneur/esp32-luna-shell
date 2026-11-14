"""Internal live native playback process for Studio's boot animations.

Frames are transient previews, not capture evidence. Each session owns its
native runtime, real-time clock, audio backend, settings and artifact directory.
"""

from copy import deepcopy
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
from uuid import uuid4

from cody_native_runtime import ROOT
from cody_studio_catalog import entry_for
from cody_studio_scenario import ARTIFACT_ROOT, scenario_revision, unique_object, validate_scenario

SESSION_ROOT = ARTIFACT_ROOT / "studio/live-sessions"


def start_live(scenario):
    value = validate_scenario(scenario)
    entry = entry_for(value["context"]["target"])
    if entry["native_target"] not in ("cody.boot.branding", "cody.boot.birth"):
        raise ValueError("Live playback is available only for the two native boot animations")
    if getattr(sys, "frozen", False):
        raise ValueError("Live playback is unavailable in packaged simulator builds")
    directory = (SESSION_ROOT / uuid4().hex).resolve()
    if not directory.is_relative_to(SESSION_ROOT.resolve()):
        raise ValueError("Live session escaped simulator artifacts")
    directory.mkdir(parents=True, exist_ok=False)
    (directory / "scenario.json").write_text(json.dumps(value, ensure_ascii=True), encoding="utf-8")
    with (directory / "stdout.log").open("w", encoding="utf-8") as stdout, \
            (directory / "stderr.log").open("w", encoding="utf-8") as stderr:
        process = subprocess.Popen([sys.executable, "-B", str(Path(__file__).resolve()), str(directory)],
                                   cwd=ROOT, stdin=subprocess.PIPE, stdout=stdout, stderr=stderr)
    return process, directory


def main():
    # A native call can hang before the UI's own timeout runs. The child is
    # disposable, so stop it if the parent disappears or the hard limit expires.
    parent_fd = sys.stdin.fileno()

    def watch_parent():
        # Use an unbuffered OS read: a daemon holding Python's buffered stdin
        # lock during interpreter shutdown can abort an otherwise successful replay.
        os.read(parent_fd, 1)
        os._exit(125)

    def watch_timeout():
        time.sleep(30)
        os._exit(124)

    threading.Thread(target=watch_parent, daemon=True, name="studio-live-parent-watch").start()
    threading.Thread(target=watch_timeout, daemon=True, name="studio-live-timeout").start()
    if len(sys.argv) != 2:
        raise ValueError("Live playback requires one artifact session directory")
    directory = Path(sys.argv[1]).resolve(strict=True)
    if not directory.is_dir() or not directory.is_relative_to(SESSION_ROOT.resolve()):
        raise ValueError("Invalid live playback artifact directory")
    value = validate_scenario(json.loads((directory / "scenario.json").read_text(encoding="utf-8"),
                                         object_pairs_hook=unique_object))
    entry = entry_for(value["context"]["target"])
    if entry["native_target"] not in ("cody.boot.branding", "cody.boot.birth"):
        raise ValueError("Unsupported live animation")
    requested = value["common"]["settings"]
    sounds = requested.get("sounds_enabled", False)
    volume = requested.get("playback_volume", 90)
    initial = deepcopy(value)
    initial["time"] = {"mode": "controlled-static", "initial_ms": 0, "advances_ms": [], "capture_at_ms": [0]}
    initial["page"]["sample_at_ms"] = 0
    initial["common"]["settings"].update(sounds_enabled=False, playback_volume=volume)
    initial = validate_scenario(initial)
    from cody_native_device import NativeDevice
    from cody_studio_runner import StudioRunner, verify_current_build

    build = verify_current_build()
    device = NativeDevice({"reason": "SIMULATED-STUDIO-LIVE"}, directory / "session.nvs", 80, True, 1,
                          deterministic_clock=True, audio_mode="host-output")
    device.seed_studio_boot_replay()
    device.seed_studio_voice_stopped()
    device.start()
    device.verify_studio_boot_replay()
    runner = StudioRunner(device)
    runner.prepare(initial)
    audio = device.audio_status()
    if sounds and (audio["mode"] != "host-output" or not audio["attempted"] or audio["backend_result"] != 0):
        raise RuntimeError(f"Live audio backend unavailable: {audio}")
    device.start_studio_live(sounds, volume)
    status = {"scenario_revision": scenario_revision(value), "audio": audio,
              "sounds_enabled": sounds, "playback_volume": volume,
              "library_sha256": build["library_sha256"]}
    last_revision = None
    previous_status = None
    while True:
        playback = device.studio_live_status()
        if playback["worker_error"] != 0:
            raise RuntimeError(f"Live animation worker failed: {playback['worker_error']}")
        status["playback"] = playback
        encoded = json.dumps(status)
        if encoded != previous_status:
            (directory / "status.tmp.json").write_text(encoded, encoding="utf-8")
            (directory / "status.tmp.json").replace(directory / "status.json")
            previous_status = encoded
        image, revision = device.frame()
        if revision != last_revision:
            temporary = directory / "frame.tmp.png"
            image.save(temporary, format="PNG")
            temporary.replace(directory / "frame.png")
            last_revision = revision
        if playback["animation_complete"] and playback["audio_complete"]:
            break
        time.sleep(0.04)
    if verify_current_build() != build:
        raise ValueError("Native build changed during live playback")


if __name__ == "__main__":
    main()
