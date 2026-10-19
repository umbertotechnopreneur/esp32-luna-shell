"""Export complete labeled real-sensor windows from an explicitly captured device log."""
import argparse
import hashlib
import json
import math
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
PATTERN = re.compile(r"ACTIVITY_SAMPLE,(\d+),(\d+),(\d+),([-+\d.eE]+),([-+\d.eE]+),([-+\d.eE]+)")
LABELS = ("stationary", "handled", "walking", "running", "transport", "unknown")


# No parameters: consume an explicit capture, preserving original measured timestamps and units.
# Exceptions: Rejects incomplete/gapped/nonfinite windows and output paths outside artifacts.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--label", choices=LABELS, required=True)
    parser.add_argument("--session", required=True)
    parser.add_argument("--placement", choices=("desk", "hand", "pocket", "bag", "other"), required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to((ROOT / "artifacts").resolve()):
        raise ValueError("Activity exports must remain under artifacts/")
    if output.exists():
        raise FileExistsError(output)
    if output == args.input.resolve():
        raise ValueError("Output cannot replace the original capture")
    windows = []
    samples = []
    generation = None
    rejected = 0
    for line in args.input.read_text(encoding="utf-8", errors="strict").splitlines():
        match = PATTERN.search(line)
        if match is None:
            continue
        gen, index, timestamp = map(int, match.groups()[:3])
        xyz = list(map(float, match.groups()[3:]))
        if index == 0:
            if samples:
                rejected += 1
            samples = []
            generation = gen
        if gen != generation or index != len(samples) or not all(map(math.isfinite, xyz)):
            samples = []
            rejected += 1
            continue
        if samples and not 45000 <= timestamp - samples[-1][0] <= 75000:
            samples = []
            rejected += 1
            continue
        samples.append([timestamp, *xyz])
        if len(samples) == 80:
            span = samples[-1][0] - samples[0][0]
            if 3700000 <= span <= 4200000:
                windows.append({"label": args.label, "placement": args.placement,
                                "session": args.session, "samples": samples})
            else:
                rejected += 1
            samples = []
    if samples:
        rejected += 1
    if not windows:
        raise ValueError("No complete real 80-sample windows; enable ActivityCapture and capture a fresh log")
    document = {"schema": 1, "units": "timestamp_us,ax_mps2,ay_mps2,az_mps2",
                "source_sha256": hashlib.sha256(args.input.read_bytes()).hexdigest(),
                "labels_are_user_annotations": True, "rejected_fragments": rejected,
                "windows": windows}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(document, indent=2), encoding="utf-8")
    print(f"Exported {len(windows)} complete windows; rejected {rejected} fragments: {output}")


if __name__ == "__main__":
    main()
