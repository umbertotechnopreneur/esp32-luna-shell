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

"""Windows fake output driver for the unchanged native Cody audio modules."""
import array
import ctypes
import io
import logging
from pathlib import Path
import queue
import threading
import time
import wave

LOG = logging.getLogger("CodyFakeAudio")


class FakeAudioDriver:
    # self: Host-only output owner with one bounded playback worker.
    # root: Repository containing the compiled host-only output driver.
    # Raises OSError or RuntimeError for a missing DLL or native preparation failure.
    def __init__(self, root):
        self.enabled = True
        self.last_error = None
        self._closed = threading.Event()
        self._generation = 0
        self._queue = queue.Queue(maxsize=8)
        self._last_glyph_at = 0.0
        library = Path(root) / "artifacts/build-cody-simulator-audio/cody_fake_audio.dll"
        self._native = ctypes.CDLL(str(library))
        callback_type = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(ctypes.c_int16),
                                        ctypes.c_size_t, ctypes.c_uint32, ctypes.c_int)
        self._callback = callback_type(self._accept_pcm)
        self._native.CodySimAudio_prepare.argtypes = [callback_type]
        for name in ("personality", "typewriter"):
            getattr(self._native, "CodySimAudio_" + name).argtypes = [ctypes.c_uint32]
        self._check(self._native.CodySimAudio_prepare(self._callback))
        self._worker = threading.Thread(target=self._playback_worker, name="CodyFakeAudio", daemon=True)
        self._worker.start()

    # self: PC output owner; disabling cancels queued clips and current playback.
    # enabled: Explicit simulator-only sound switch, independent of device settings.
    def set_enabled(self, enabled):
        if self.enabled == enabled:
            return
        self.enabled = enabled
        self._generation += 1
        if not enabled:
            import winsound
            try:
                winsound.PlaySound(None, 0)
            except (OSError, RuntimeError) as error:
                self.last_error = error
                LOG.error("Synthetic PC audio cancellation failed: %r", error)

    # self: PC output owner retaining optional failures without stopping Tk or touch.
    # operation: Export of the unchanged native Cody audio module.
    # arguments: Typed literal arguments required by that export.
    def _request(self, operation, *arguments):
        if not self.enabled:
            return
        error = getattr(self._native, "CodySimAudio_" + operation)(*arguments)
        if error != 0:
            LOG.error("Synthetic PC audio %s rejected: native error=%s", operation, error)
            if self.last_error is None:
                self.last_error = RuntimeError(f"{operation}: native error={error}")

    # self: Host output owner retaining asynchronous playback failures.
    # error: Original native return code, never firmware-device evidence.
    # Raises RuntimeError for native preparation or request failures.
    def _check(self, error):
        if error != 0:
            raise RuntimeError(f"Synthetic PC audio driver rejected operation: error={error}")

    # self: Windows fake codec; copies immutable PCM before the callback returns.
    # samples: Native pointer valid for the real Cody module's lifetime.
    # count: Exact mono sample count from that module.
    # rate: Authored sample rate; no oscillator or recipe is reimplemented.
    # gain: One for effects/text, eight for the existing firmware touch impulse.
    # Returns the real queue/conversion error through the native owner boundary.
    def _accept_pcm(self, samples, count, rate, gain):
        try:
            pcm = array.array("h")
            pcm.frombytes(ctypes.string_at(samples, count * 2))
            if gain != 1:
                pcm = array.array("h", (max(-32768, min(32767, value * gain)) for value in pcm))
            output = io.BytesIO()
            with wave.open(output, "wb") as stream:
                stream.setnchannels(1)
                stream.setsampwidth(2)
                stream.setframerate(rate)
                stream.writeframes(pcm.tobytes())
            self._queue.put_nowait((output.getvalue(), True, time.monotonic(), self._generation))
            return 0
        except Exception as error:
            self.last_error = error
            LOG.error("Synthetic PC audio admission failed: %r", error)
            return -1

    # self: One PC output voice; blocking playback lives only on this worker.
    # Asynchronous Windows failures remain visible in last_error and the host log.
    def _playback_worker(self):
        import winsound
        while not self._closed.is_set():
            try:
                payload, memory, requested_at, generation = self._queue.get(timeout=0.1)
            except queue.Empty:
                continue
            if not self.enabled or self._closed.is_set() or generation != self._generation:
                continue
            if memory and time.monotonic() - requested_at > 0.4:
                LOG.info("Synthetic PC audio request expired before playback")
                continue
            try:
                flags = winsound.SND_MEMORY if memory else winsound.SND_FILENAME
                # SND_MEMORY cannot be asynchronous; the owner thread keeps Tk responsive.
                winsound.PlaySound(payload, flags | winsound.SND_NODEFAULT)
            except (OSError, RuntimeError) as error:
                self.last_error = error
                LOG.error("Synthetic PC audio playback failed: %r", error)

    # self: Host driver sharing the same sound selector and immutable PCM as firmware.
    # cue: Stable manga cue ID from the successfully displayed frame.
    # Rejections are retained and logged without stopping presentation.
    def personality(self, cue):
        self._request("personality", cue)

    # self: Host driver; requests the real firmware whistle PCM.
    # Rejections are retained and logged without stopping presentation.
    def whistle(self):
        self._request("whistle")

    # self: Host driver; one physical pointer press produces one click.
    # Rejections are retained and logged without stopping touch handling.
    def touch(self):
        self._request("touch")

    # self: Host driver; coalesces glyph progress at the firmware's 20/s limit.
    # count: Newly visible characters in a completed preview frame.
    # Rejections are retained and logged without stopping scene progress.
    def typewriter(self, count):
        now = time.monotonic()
        if self.enabled and count > 0 and now - self._last_glyph_at >= 0.05:
            self._last_glyph_at = now
            self._request("typewriter", count)

    # self: Existing PC output owner shared by both manual previews and automatic cues.
    # path: Existing local WAV; no device speaker or firmware snapshot is accessed.
    # Missing assets and busy admission are retained and logged without stopping Tk.
    def preview_file(self, path):
        try:
            if not Path(path).is_file():
                raise FileNotFoundError(path)
            if self.enabled:
                self._queue.put_nowait((str(path), False, time.monotonic(), self._generation))
        except (OSError, queue.Full) as error:
            self.last_error = error
            LOG.error("Synthetic PC audio preview failed: %r", error)

    # self: Host output owner; queued cues cannot survive window destruction.
    def close(self):
        import winsound
        self._closed.set()
        try:
            winsound.PlaySound(None, 0)
        except (OSError, RuntimeError) as error:
            self.last_error = error
            LOG.error("Synthetic PC audio shutdown failed: %r", error)
