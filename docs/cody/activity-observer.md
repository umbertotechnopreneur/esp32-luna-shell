# Experimental pretrained activity observer

This is an observational ESP-DL inference path, not an automatic power policy.
It uses actual pretrained weights and original model labels: downstairs, jogging,
sitting, standing, upstairs, walking. It does not claim to recognize a bag, handling,
vehicle travel, or the desired future Cody context classes. No training is performed.

## Model and implementation provenance

- Original [Espressif article](https://developer.espressif.com/blog/esp32-s3-edge-ai-human-activity-recognition-using-accelerometer-data-and-esp-dl/)
  and [author repository](https://github.com/alibukharai/Blogs/tree/bb27ee3217c384697d1d25bc72c5862337bcafa9/Activity_detection).
- Original `Activity_model_2.onnx`, SHA256
  `5ffdcf5823a6729bd45558e90e4776e3b188e1a609f0859fe3e38c13fd50583d`.
- Conversion uses ESP-PPQ 1.3.11, original calibration arrays, `w8a16`, ESP32-S3,
  and exports the modern `.espdl` format. This is quantization calibration of fixed
  weights, not training or personalization. No upstream Python script is executed;
  calibration pickle loading admits only the required NumPy array globals.
- [ESP-DL 3.3.13 manifest](https://github.com/espressif/esp-dl/blob/v3.3.13/esp-dl/idf_component.yml)
  admits ESP32-S3 with IDF >=5.3; IDF 6.0.2 satisfies that declared constraint.
  This is dependency compatibility evidence, not build or physical validation.
  ESP-DL itself is MIT licensed. No IDF 4.4 code or old ESP-DL API is imported.
- Runtime dependency is the official September 30, 2026 `v3.3.13` release,
  commit `6d3c4da1087d35916858dd0f9b3e9d6a85186e11`. Its supported
  [compile registration API](https://github.com/espressif/esp-dl/blob/v3.3.13/esp-dl/cmake/compile_register.cmake)
  receives `components/LUNASHELL_platform/activity.compile_req.yml`: seven graph operators
  plus `RequantizeLinear` required by the application's `TensorBase::assign` calls,
  and four ESP32-S3 W8A16 convolution kernels. Both aligned and unaligned
  2x2 bias+ReLU convolutions and packed-MatMul 1x1 linear kernels are retained.
  Other primitive dependencies come from the official operator specification.
  The same pretrained source was requantized with 8-bit weights and 16-bit
  activations: 172,304 bytes, SHA256
  `b5333dafe62835cc9999fe27ea652802fcdcaa5250604c5136087d1c1044d4e0`.
  The former 334,528-byte W16A16 artifacts are preserved locally under
  `artifacts/models/activity/w16a16-3880fc61/`. This saves 162,224 model bytes;
  it does not establish classification accuracy or final application size.
  CMake binds the reviewed requirements to the current exact hash.
  Re-exporting a model requires reviewing and rebinding these requirements.
  This selection changes neither partitions nor Sophia and edits no managed source.
  For this activity-enabled build only, ESP-DL C/C++ uses `-Os` after upstream
  `-O3`. Platform control, startup, logging and Cody's coordinators, policies and diagnostic
  panels also use `-Os`. Separate drawing kernels, the geometric rig, benchmarks,
  Sophia and assembly retain their existing optimization.
  Inference and control-loop latency still require physical observation.
  Final linked size and physical inference remain separately verifiable outcomes.
- No license was found for the original author's model weights. They remain in
  ignored `artifacts/models/activity/` for the authorized local experiment and are
  not checked into this repository. Distribution of firmware containing these
  weights remains unresolved. The root project license does not cover those weights.

Other researched candidates were not silently substituted: ST `st_ign`/`st_gmp`
weights use SLA0044 with ST hardware restrictions; MIT `raadsr15/TinyML-HAR` has
actual pretrained weights but consumes gravity-removed UCI body acceleration at
50 Hz and an axis-major reshape, not this raw-acceleration contract; EdgeHAR and
the inspected multihead attention repository did not contain pretrained weights.

## Actual input contract and limitations

The linked [training notebook](https://github.com/laxmimerit/Human-Activity-Recognition-Using-Accelerometer-Data-and-CNN/blob/adac87942ef29c7cbbbda47806aab6bd280a9e9b/Human%20Activity%20Recognition.ipynb)
standardizes raw m/s2 values using fixed per-axis means and scales, then concatenates
80 X, 80 Y and 80 Z values before reshaping to `[1,80,3,1]`. The original deployed
MPU6050 example instead feeds interleaved raw g and omits the scaler: that is a real
preprocessing mismatch, not the contract adopted here.

The generator reconstructs the notebook's fixed input selection from its pinned WISDM
file without training the network. All 107 upstream calibration tensors match the
recovered 532 windows with maximum float32 difference **0.0**. The recovered mean is
`[0.84258519, 8.51718746, 1.65515833]` m/s2 and scale is
`[3.72718356, 5.12574007, 3.36460742]` m/s2; full precision is recorded in provenance.
Cody applies these constants to the acquisition owner's bias-corrected acceleration
before EMA and preserves the original axis-major tensor order.

No second I2C reader, smoothing reversal, interpolation, repeated snapshots,
zero filling or fabricated samples are involved. Each window preserves all 80 original
acquisition timestamps. Per-sample spacing must be 45..75 ms and the whole 79-interval
span must be 3.7..4.2 seconds; otherwise the observer rejects the window.

The exact recovered tensor preprocessing does not establish real-world accuracy on
Cody. QMI8658 placement, bias, orientation,
sampling/filter response and device transport differ from the author's dataset and
MPU6050 demo. `domain_validated=false` remains explicit even when inference produces
finite scores. Scores are softmax outputs, not calibrated probabilities. There is
no guessed confidence cutoff that turns these outputs into reliable Cody activities.

A stationary bag and a desk can produce indistinguishable acceleration. Walking
with Cody in a bag and walking with Cody in hand are different placement domains.
Constant-speed car/plane travel cannot reliably be inferred from acceleration alone.

## Ownership, energy and errors

An explicit build option admits the observer. Default builds retain existing
acquisition behavior. The existing acquisition monitor requests 50 ms sampling only
while active; standby disables that request. A running inference may finish while
the standby notification propagates, but its generation is rejected after the owner
observes suspension. No independent sensor task or wake lease is introduced. No
classifier runs after PMU shutdown. Radio and voice policy are unchanged.
Only successful acquisitions explicitly scheduled for the model enter its windows;
unrelated UI/full-refresh measurements do not shift that phase. Their real read
errors still invalidate the observer. Deadlines start at the acquisition attempt
after owner locks and include processing time in the remaining wait.
Activity-enabled acquisition uses CPU1 at its existing priority 3, separate from
CPU0's priority-4 voice workers. Other builds retain unpinned acquisition. This
also avoids incidental affinity from IDF's first floating-point use; the running
owner reports its actual core. Voice/render priorities are unchanged.

Three bounded windows consume 4800 bytes in explicitly placed PSRAM component storage;
normal acquisition/inference tasks own access, never ISR, DMA or flash callbacks.
The flash model base is aligned to 16 bytes. Tensor memory belongs
to ESP-DL, with zero preferred internal tensor budget and model parameters kept in
flash. The worker uses an 8192-byte PSRAM stack and single-core inference. No window
or full canonical snapshot is copied onto that stack. The canonical result adds
approximately 104 bytes to the full snapshot; Cody uses `readActivity()` as a bounded
projection. Existing full-snapshot callers and compiled stack frames/watermarks still
require final validation; these source budgets do not prove runtime headroom.

Acquisition gaps, calibration transitions, queue pressure, model/tensor errors and
standby invalidate usable results and reset stability. Missing engine initialization
stops the extra acquisition request instead of continuously spending energy on a
failed model. Canonical status contains original labels, scores, validity, error,
window times, model ID, sequence, inference duration and consecutive-window stability.
Results age out after six seconds from the window's last actual measurement.
Diagnostics logs explicitly mark every successful result `domain=UNVALIDATED`.

## Public commands

All operations enter through `scripts/device.ps1`. Run only the operations currently
authorized by the user; tests still require their individual announced commands and
fresh approval under AGENTS.md.

```powershell
./scripts/device.ps1 generate -Generator activity-environment
./scripts/device.ps1 generate -Generator activity-model -PythonExecutable 'E:\ESP32 Luna Shell\artifacts\python\activity\env\Scripts\python.exe'
./scripts/device.ps1 update-lock -Profile cody-companion -Feature wake-word -ActivityClassifier
./scripts/device.ps1 build -Profile cody-companion -Feature wake-word -ActivityClassifier -Jobs 8
```

The activity switch currently requires Cody's `wake-word` feature so the experimental
build retains Sophia. It isolates sdkconfig/build outputs under
`artifacts/build-cody-companion-wake-word-activity-debug` and a separate dependency
lock. `-ActivityCapture` additionally isolates a capture build and logs real samples;
it is optional and adds logging load. Local model artifacts survive shared transient
cleanup. A selected build with missing model artifacts fails explicitly.

For an already built/flashed capture variant, select the currently identified port
and capture without reset. Annotate one known activity/placement per capture; never
claim a capture's user label is a model prediction:

```powershell
./scripts/device.ps1 capture -Port COM_SELECTED -DurationSeconds 30 -OutputPath artifacts/serial/activity.log
./scripts/device.ps1 generate -Generator activity-export -ToolArguments @('--input','artifacts/serial/activity.log','--output','artifacts/activity/session-01.json','--label','walking','--placement','hand','--session','session-01')
```

The exporter rejects incomplete/gapped windows, preserves original timestamps and
m/s2 units, records source hash and user annotations, and does not train anything.
SD storage is not required. Exported data and models stay under `artifacts/`.

Before any eventual automatic shutdown policy, measured false-shutdown rates,
placement evaluation, persistent evidence, lack of recent interaction and a cancellable
warning are still required. The observer currently cannot request standby or shutdown.

## Current evidence

On 2026-10-12 the isolated conversion environment was provisioned and actual fixed
pretrained weights were converted successfully. Original scaler and axis ordering
were recovered without training, with all 107 provided calibration inputs matched
exactly at float32 precision. The first W16A16 model hash was
`3880fc61b887283f785028f310930b629c6adeb9dba1519e98bb62d1a7f7af89`.
The final W8A16 conversion and its different hash are recorded above. Source
integration and static review are separate from firmware compilation, flash,
runtime inference, physical classification accuracy and energy measurements; consult
the canonical `done.md` and `TODO.md` records for subsequent evidence.
