"""Convert the original pretrained HAR example to modern ESP-DL; never train."""
import argparse
import hashlib
import json
import pathlib
import pickle
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
DEST = ROOT / "artifacts/models/activity"
REVISION = "bb27ee3217c384697d1d25bc72c5862337bcafa9"
BASE = f"https://raw.githubusercontent.com/alibukharai/Blogs/{REVISION}/Activity_detection/"
PREPROCESSING_REVISION = "adac87942ef29c7cbbbda47806aab6bd280a9e9b"


# relative: Immutable upstream resource path.
# name: Local artifact filename.
# Exceptions: Network and filesystem errors propagate without substituted resources.
def fetch(relative, name):
    target = DEST / name
    with urllib.request.urlopen(BASE + relative, timeout=60) as response:
        target.write_bytes(response.read())
    return target


class ArrayOnlyUnpickler(pickle.Unpickler):
    # module: Pickle global namespace, admitted only for NumPy array reconstruction.
    # name: Pickle global symbol.
    # Exceptions: Rejects all non-array globals; no arbitrary upstream code executes.
    def find_class(self, module, name):
        import numpy as np
        allowed = {
            ("numpy", "ndarray"): np.ndarray,
            ("numpy", "dtype"): np.dtype,
            ("numpy.core.multiarray", "_reconstruct"): np._core.multiarray._reconstruct,
            ("numpy._core.multiarray", "_reconstruct"): np._core.multiarray._reconstruct,
        }
        if (module, name) not in allowed:
            raise ValueError(f"Unsupported calibration pickle global: {module}.{name}")
        return allowed[module, name]


# calibration_path: Original author's held-out input arrays; no labels or network training.
# manifest: Provenance destination updated only after the exact input contract is recovered.
# Exceptions: Rejects any disagreement with the recovered upstream calibration tensors.
def recover_preprocessing(calibration_path, manifest):
    import numpy as np
    dataset = DEST / "original-wisdm.txt"
    url = ("https://raw.githubusercontent.com/laxmimerit/"
           "Human-Activity-Recognition-Using-Accelerometer-Data-and-CNN/"
           f"{PREPROCESSING_REVISION}/WISDM_ar_v1.1/WISDM_ar_v1.1_raw.txt")
    if not dataset.exists():
        with urllib.request.urlopen(url, timeout=120) as response:
            dataset.write_bytes(response.read())
    order = ["Walking", "Jogging", "Upstairs", "Downstairs", "Sitting", "Standing"]
    groups = {label: [] for label in order}
    # Reproduce the linked notebook's bounded input selection, not its model training.
    with dataset.open(encoding="utf-8") as stream:
        for line in stream:
            fields = line.split(",")
            if len(fields) < 6:
                continue
            z = fields[5].split(";")[0].strip()
            if not z:
                break
            label = fields[1]
            if label in groups and (label == "Standing" or len(groups[label]) < 3555):
                groups[label].append([float(fields[3]), float(fields[4]), float(z)])
    if any(len(groups[label]) != 3555 for label in order):
        raise ValueError("Pinned upstream selection no longer matches 3555 samples per activity")
    selected = np.asarray([row for label in order for row in groups[label]], dtype=np.float64)
    mean = selected.mean(axis=0)
    scale = selected.std(axis=0)
    normalized = (selected - mean) / scale
    windows = np.asarray([normalized[start:start + 80].T.reshape(80, 3, 1)
                          for start in range(0, len(normalized) - 80, 40)], dtype=np.float32)
    with calibration_path.open("rb") as stream:
        original = np.asarray(ArrayOnlyUnpickler(stream).load(), dtype=np.float32)
    if original.shape != (107, 80, 3, 1) or windows.shape != (532, 80, 3, 1):
        raise ValueError("Recovered dataset shapes disagree with the original calibration contract")
    maximum_difference = 0.0
    for row in original:
        difference = float(np.abs(windows - row).max(axis=(1, 2, 3)).min())
        maximum_difference = max(maximum_difference, difference)
    if maximum_difference > 1e-5:
        raise ValueError(f"Preprocessing is not the pretrained contract: mismatch {maximum_difference}")
    header = ("#pragma once\nnamespace LUNASHELL::activity_model {\n"
              "inline constexpr float kMeanMps2[3] = {" + ", ".join(f"{v:.12g}F" for v in mean) + "};\n"
              "inline constexpr float kScaleMps2[3] = {" + ", ".join(f"{v:.12g}F" for v in scale) + "};\n}\n")
    (DEST / "activity_preprocessing.hpp").write_text(header, encoding="ascii")
    manifest.update(preprocessing_source=url, preprocessing_revision=PREPROCESSING_REVISION,
                    preprocessing="axis-major standardized raw m/s2; 80 X,80 Y,80 Z; reshape NHWC",
                    mean_mps2=mean.tolist(), scale_mps2=scale.tolist(),
                    calibration_contract_max_abs_difference=maximum_difference,
                    dataset_sha256=hashlib.sha256(dataset.read_bytes()).hexdigest())
    print(f"Recovered fixed preprocessing: mean={mean}, scale={scale}; calibration mismatch={maximum_difference}")


# No parameters: download immutable pretrained weights and convert without optimization/training.
# Exceptions: Rejects incompatible graph/data contracts and propagates conversion failures.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--download-only", action="store_true")
    parser.add_argument("--recover-preprocessing", action="store_true")
    args = parser.parse_args()
    DEST.mkdir(parents=True, exist_ok=True)
    model_path = fetch("esp_dl_formate_conversion/Activity_model_2.onnx", "original.onnx")
    calibration_path = fetch("model_development/X_test.pkl", "original-calibration.pkl")
    if hashlib.sha256(model_path.read_bytes()).hexdigest() != "5ffdcf5823a6729bd45558e90e4776e3b188e1a609f0859fe3e38c13fd50583d":
        raise ValueError("Pinned pretrained ONNX identity mismatch")
    if hashlib.sha256(calibration_path.read_bytes()).hexdigest() != "8cec1fbbda48215c638540e819df87af99f4547521e367b02c130fccaa2b81c9":
        raise ValueError("Pinned calibration identity mismatch")
    manifest = {
        "source": BASE, "revision": REVISION,
        "license": "No license found upstream; local experiment only; redistribution unresolved",
        "source_sha256": hashlib.sha256(model_path.read_bytes()).hexdigest(),
        "calibration_sha256": hashlib.sha256(calibration_path.read_bytes()).hexdigest(),
        "labels": ["downstairs", "jogging", "sitting", "standing", "upstairs", "walking"],
        "sample_period_us": 50000, "samples": 80,
        "esp_dl": "3.3.13", "esp_ppq": "1.3.11", "trained_here": False,
        "quantization": "w8a16",
        "status": "downloaded; conversion pending",
    }
    (DEST / "provenance.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    if args.download_only:
        print(f"Downloaded original weights and calibration to {DEST}; no inference performed")
        return
    recover_preprocessing(calibration_path, manifest)
    manifest["preprocessing_sha256"] = hashlib.sha256((DEST / "activity_preprocessing.hpp").read_bytes()).hexdigest()
    (DEST / "provenance.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    if args.recover_preprocessing:
        if (DEST / "activity.espdl").exists():
            manifest.update(status="preprocessing recovered; existing converted model retained; runtime validation pending",
                            model_sha256=hashlib.sha256((DEST / "activity.espdl").read_bytes()).hexdigest())
            (DEST / "provenance.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        return
    import numpy as np
    import onnx
    import torch
    from torch.utils.data import DataLoader, TensorDataset
    from esp_ppq.api import espdl_quantize_onnx

    graph = onnx.load(str(model_path))
    shape = [int(d.dim_value) for d in graph.graph.input[0].type.tensor_type.shape.dim]
    with calibration_path.open("rb") as stream:
        calibration = np.asarray(ArrayOnlyUnpickler(stream).load(), dtype=np.float32)
    print(f"Original ONNX input: {shape}; calibration shape: {calibration.shape}")
    if calibration.ndim != 4 or tuple(calibration.shape[1:]) != (80, 3, 1):
        raise ValueError("Original calibration is not NHWC [N,80,3,1]; investigate, never reshape blindly")
    if not np.isfinite(calibration).all():
        raise ValueError("Calibration contains nonfinite measurements")
    shape = [1, 80, 3, 1]
    data = torch.from_numpy(calibration)
    loader = DataLoader(TensorDataset(data), batch_size=1, shuffle=False)
    output = DEST / "activity.espdl"
    espdl_quantize_onnx(
        onnx_import_file=str(model_path), espdl_export_file=str(output),
        calib_dataloader=loader, calib_steps=min(32, len(data)),
        input_shape=shape, inputs=None, target="esp32s3", quant_type="w8a16",
        collate_fn=lambda batch: batch[0], device="cpu", error_report=False,
        skip_export=False, export_test_values=False, verbose=1,
    )
    manifest.update(status="converted; runtime validation pending", input_shape=shape,
                    model_sha256=hashlib.sha256(output.read_bytes()).hexdigest(),
                    calibration_min=float(calibration.min()), calibration_max=float(calibration.max()))
    (DEST / "provenance.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"Converted pretrained model: {output}. No training or validation suite executed.")


if __name__ == "__main__":
    main()
