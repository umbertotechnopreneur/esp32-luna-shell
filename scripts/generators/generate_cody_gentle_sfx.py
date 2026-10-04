"""Render private Cody listening candidates from a read-only local SFX archive."""

import argparse
from array import array
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import wave


ROOT = Path(__file__).resolve().parents[2]
SELECTION_PATH = ROOT / "assets/cody/audio/gentle-sfx/source-selection.json"
OUTPUT_ROOT = ROOT / "artifacts/audio/cody-gentle-sfx"
SAMPLE_RATE = 16000


# executable: Required media tool name resolved from PATH.
# arguments: Literal CLI tokens, never interpreted by a shell.
# Raises RuntimeError when the tool cannot execute or reports a real failure.
def run_media_tool(executable, arguments):
    tool_path = shutil.which(executable)
    if tool_path is None:
        raise RuntimeError(f"Required tool is not on PATH: {executable}")
    result = subprocess.run(
        [tool_path, *arguments], capture_output=True, check=False, timeout=60
    )
    if result.returncode != 0:
        message = result.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"{executable} failed ({result.returncode}): {message}")
    return result.stdout


# source_path: Existing external sample opened read-only.
# Raises RuntimeError for unsupported or missing audio stream information.
def probe_source(source_path):
    encoded = run_media_tool("ffprobe", [
        "-v", "error", "-select_streams", "a:0", "-show_entries",
        "stream=codec_name,sample_rate,channels:format=duration", "-of", "json",
        str(source_path),
    ])
    info = json.loads(encoded)
    streams = info.get("streams", [])
    if len(streams) != 1 or streams[0].get("channels") not in (1, 2):
        raise RuntimeError(f"Expected one mono/stereo audio stream: {source_path}")
    return {
        "duration_seconds": float(info["format"]["duration"]),
        "sample_rate_hz": int(streams[0]["sample_rate"]),
        "channels": streams[0]["channels"],
        "codec": streams[0]["codec_name"],
    }


# source_path: External sample whose original identity belongs in the manifest.
def source_sha256(source_path):
    digest = hashlib.sha256()
    with source_path.open("rb") as source_stream:
        for block in iter(lambda: source_stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


# cue: Explicit source window and filter authoring values.
# source_path: Read-only mono/stereo archive recording.
# source_info: Probed source format used to select an explicit downmix.
# Raises RuntimeError if the configured passage extends beyond the source.
def decode_window(cue, source_path, source_info):
    start_seconds = cue["source_window_start_ms"] / 1000.0
    end_seconds = cue["source_window_end_ms"] / 1000.0
    if start_seconds < 0 or end_seconds <= start_seconds:
        raise RuntimeError(f"Invalid source window: {cue['id']}")
    if end_seconds > source_info["duration_seconds"]:
        raise RuntimeError(f"Source window exceeds the recording: {cue['id']}")
    downmix = (
        "pan=mono|c0=0.5*c0+0.5*c1" if source_info["channels"] == 2
        else "pan=mono|c0=c0"
    )
    filters = [
        f"atrim=start={start_seconds:.6f}:end={end_seconds:.6f}",
        "asetpts=PTS-STARTPTS", downmix,
        f"highpass=f={cue['highpass_hz']}:p=2",
        f"lowpass=f={cue['lowpass_hz']}:p=2",
        f"aresample={SAMPLE_RATE}",
    ]
    encoded = run_media_tool("ffmpeg", [
        "-hide_banner", "-loglevel", "error", "-nostdin", "-i", str(source_path),
        "-map", "0:a:0", "-af", ",".join(filters), "-ac", "1", "-ar",
        str(SAMPLE_RATE), "-c:a", "pcm_f64le", "-f", "f64le", "pipe:1",
    ])
    samples = array("d")
    samples.frombytes(encoded)
    if sys.byteorder != "little":
        samples.byteswap()
    return samples, filters


# samples: Filtered, mono 16 kHz source-window samples.
# frame_count: Exact requested candidate length.
# Raises RuntimeError for a short or silent/unusable source passage.
def select_active_passage(samples, frame_count):
    if len(samples) < frame_count:
        raise RuntimeError("Decoded source window is shorter than the requested cue.")
    # A fixed 5 ms search step avoids inventing a rhythm or looping the recording.
    step_frames = SAMPLE_RATE // 200
    energy = sum(value * value for value in samples[:frame_count])
    best_energy = energy
    best_start = 0
    previous_start = 0
    candidate_starts = list(range(step_frames, len(samples) - frame_count + 1, step_frames))
    last_start = len(samples) - frame_count
    if last_start > 0 and (not candidate_starts or candidate_starts[-1] != last_start):
        candidate_starts.append(last_start)
    for start in candidate_starts:
        energy -= sum(value * value for value in samples[previous_start:start])
        energy += sum(
            value * value for value in samples[previous_start + frame_count:start + frame_count]
        )
        if energy > best_energy:
            best_energy = energy
            best_start = start
        previous_start = start
    if not math.isfinite(best_energy) or best_energy <= 1e-12:
        raise RuntimeError("Configured source passage contains no usable audio.")
    return samples[best_start:best_start + frame_count], best_start


# samples: Active passage copied from a real source recording.
# cue: Fade lengths authored in milliseconds.
# target_peak_dbfs: Quiet digital peak ceiling for the rendered candidate.
# Raises RuntimeError for impossible fades or a silent passage after shaping.
def shape_candidate(samples, cue, target_peak_dbfs):
    fade_in_frames = round(cue["fade_in_ms"] * SAMPLE_RATE / 1000)
    fade_out_frames = round(cue["fade_out_ms"] * SAMPLE_RATE / 1000)
    if fade_in_frames < 2 or fade_out_frames < 2 or fade_in_frames + fade_out_frames > len(samples):
        raise RuntimeError(f"Invalid fade lengths: {cue['id']}")
    for index in range(fade_in_frames):
        samples[index] *= math.sin(math.pi * index / (2 * (fade_in_frames - 1))) ** 2
    for index in range(fade_out_frames):
        samples[len(samples) - fade_out_frames + index] *= (
            math.cos(math.pi * index / (2 * (fade_out_frames - 1))) ** 2
        )
    peak = max(abs(value) for value in samples)
    if not math.isfinite(peak) or peak <= 1e-10:
        raise RuntimeError(f"Silent or non-finite shaped passage: {cue['id']}")
    gain = 10 ** (target_peak_dbfs / 20.0) / peak
    pcm_samples = array("h", (round(value * gain * 32767) for value in samples))
    # Both boundaries are exactly silent even after quantization to PCM16.
    pcm_samples[0] = 0
    pcm_samples[-1] = 0
    output_peak = max(abs(value) for value in pcm_samples) / 32768.0
    output_rms = math.sqrt(sum(value * value for value in pcm_samples) / len(pcm_samples)) / 32768.0
    metrics = {
        "peak_dbfs": round(20 * math.log10(output_peak), 4),
        "rms_dbfs": round(20 * math.log10(output_rms), 4),
        "linear_gain_after_fades": gain,
        "fade_in_ms": cue["fade_in_ms"],
        "fade_out_ms": cue["fade_out_ms"],
        "fade_curve": "sine-squared",
        "first_sample": 0,
        "last_sample": 0,
    }
    if sys.byteorder != "little":
        pcm_samples.byteswap()
    return pcm_samples.tobytes(), metrics


# output_path: Generator-owned destination for a mono PCM16 WAV.
# pcm_bytes: Already shaped little-endian signed 16-bit samples.
def write_candidate(output_path, pcm_bytes):
    with wave.open(str(output_path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(pcm_bytes)


# archive_root: Explicit external source root, with no output files written there.
# Raises RuntimeError for invalid selection metadata or missing source recordings.
def generate_candidates(archive_root):
    selection_bytes = SELECTION_PATH.read_bytes()
    selection = json.loads(selection_bytes)
    cues = selection["cues"]
    cue_ids = [cue["id"] for cue in cues]
    if selection["schema_version"] != 1 or selection["sample_rate_hz"] != SAMPLE_RATE:
        raise RuntimeError("Unsupported gentle-SFX selection format.")
    if len(cues) != 10 or len(set(cue_ids)) != 10:
        raise RuntimeError("The pack requires ten uniquely named candidates.")
    for cue in cues:
        if not cue["id"].startswith("cody-") or any(character not in "abcdefghijklmnopqrstuvwxyz-" for character in cue["id"]):
            raise RuntimeError(f"Unsafe candidate identity: {cue['id']}")
        if not 100 <= cue["duration_ms"] <= 900:
            raise RuntimeError(f"Candidate duration must be 100–900 ms: {cue['id']}")
    if not -30 <= selection["target_peak_dbfs"] <= -24:
        raise RuntimeError("The pack's quiet target peak must stay between -30 and -24 dBFS.")
    archive_root = archive_root.resolve(strict=True)
    OUTPUT_ROOT.parent.mkdir(parents=True, exist_ok=True)
    manifest = {
        "schema_version": 1,
        "selection_sha256": hashlib.sha256(selection_bytes).hexdigest(),
        "format": {"channels": 1, "sample_rate_hz": SAMPLE_RATE, "encoding": "PCM16 little-endian"},
        "target_peak_dbfs": selection["target_peak_dbfs"],
        "licensing": selection["licensing"],
        "listening_review": "Pending; no perceptual quality or device speaker-volume claim has been made.",
        "runtime_integration": "None; local authoring previews only.",
        "selection_method": selection["selection_method"],
        "cues": [],
    }
    leader = bytes(SAMPLE_RATE * 2)
    gap = bytes(round(selection["audition_gap_ms"] * SAMPLE_RATE / 1000) * 2)
    audition_parts = [leader]
    audition_frames = SAMPLE_RATE
    with tempfile.TemporaryDirectory(prefix="cody-gentle-sfx-", dir=OUTPUT_ROOT.parent) as stage_name:
        stage_root = Path(stage_name)
        for cue_index, cue in enumerate(cues):
            source_path = (archive_root / cue["source"]).resolve(strict=True)
            if not source_path.is_relative_to(archive_root) or not source_path.is_file():
                raise RuntimeError(f"Source escapes the configured archive: {source_path}")
            source_info = probe_source(source_path)
            samples, filters = decode_window(cue, source_path, source_info)
            frame_count = round(cue["duration_ms"] * SAMPLE_RATE / 1000)
            samples, start_frame = select_active_passage(samples, frame_count)
            pcm_bytes, metrics = shape_candidate(samples, cue, selection["target_peak_dbfs"])
            file_name = f"{cue['id']}.wav"
            output_path = stage_root / file_name
            write_candidate(output_path, pcm_bytes)
            trim_start = cue["source_window_start_ms"] / 1000 + start_frame / SAMPLE_RATE
            manifest["cues"].append({
                "id": cue["id"], "intent": cue["intent"], "file": file_name,
                "source": str(source_path), "source_sha256": source_sha256(source_path),
                "source_info": source_info,
                "source_window_start_ms": cue["source_window_start_ms"],
                "source_window_end_ms": cue["source_window_end_ms"],
                "trim_start_seconds": trim_start,
                "trim_end_seconds": trim_start + frame_count / SAMPLE_RATE,
                "ffmpeg_filter_chain": filters, "shaping": metrics,
                "duration_ms": 1000 * frame_count / SAMPLE_RATE,
                "pcm_frames": frame_count, "pcm_bytes": len(pcm_bytes),
                "wav_bytes": output_path.stat().st_size,
                "wav_sha256": hashlib.sha256(output_path.read_bytes()).hexdigest(),
                "audition_start_seconds": audition_frames / SAMPLE_RATE,
                "audition_end_seconds": (audition_frames + frame_count) / SAMPLE_RATE,
            })
            audition_parts.append(pcm_bytes)
            audition_frames += frame_count
            if cue_index < len(cues) - 1:
                audition_parts.append(gap)
                audition_frames += len(gap) // 2
        audition_parts.append(gap)
        audition_frames += len(gap) // 2
        audition_name = "cody-gentle-sfx-audition.wav"
        write_candidate(stage_root / audition_name, b"".join(audition_parts))
        preview_bytes = sum(cue["wav_bytes"] for cue in manifest["cues"])
        audition_bytes = (stage_root / audition_name).stat().st_size
        manifest["storage"] = {
            "ten_preview_wav_bytes": preview_bytes,
            "ten_preview_pcm_bytes": sum(cue["pcm_bytes"] for cue in manifest["cues"]),
            "audition_wav_bytes": audition_bytes,
            "all_audio_wav_bytes": preview_bytes + audition_bytes,
        }
        manifest["audition"] = {
            "file": audition_name, "duration_seconds": audition_frames / SAMPLE_RATE,
            "leader_seconds": 1, "between_cues_seconds": selection["audition_gap_ms"] / 1000,
            "tail_seconds": selection["audition_gap_ms"] / 1000,
        }
        (stage_root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        # Finish every render before replacing the known generated package files.
        OUTPUT_ROOT.mkdir(parents=True, exist_ok=True)
        for output_path in stage_root.iterdir():
            os.replace(output_path, OUTPUT_ROOT / output_path.name)
    print(f"Rendered 10 local candidates: {OUTPUT_ROOT}")
    print(f"Preview storage: {preview_bytes} bytes; all audio: {preview_bytes + audition_bytes} bytes")
    print("Listening review and source redistribution rights remain unverified.")


# No parameters: expose archive selection through the repository device dispatcher.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive-root", type=Path)
    arguments = parser.parse_args()
    selection = json.loads(SELECTION_PATH.read_text(encoding="utf-8"))
    archive_root = arguments.archive_root or Path(selection["archive_root"])
    generate_candidates(archive_root)


if __name__ == "__main__":
    main()
