# Sophia wake-word model

The model directory contains the unmodified Espressif `wn9_sophia_tts_v1`
weights. It recognizes **Sophia**. It does not recognize Cody and is not a
speech-to-text or command-recognition model.

Source: [ESP-SR 2.4.6 source snapshot](https://github.com/espressif/esp-sr/tree/cfdbb52172d4f1b3e5f7469d6c3c6e5dd0738143/model/wakenet_model/wn9_sophia_tts_v1).
The immutable source commit, original file lengths and SHA-256 provenance are
recorded in `source.json`. The original **ESPRESSIF MIT License** is retained
in `LICENSE`, including its Espressif-products condition.

Only directories containing `_MODEL_INFO_` go below `models/`. ESP-SR packs
that directory into the internal `model` partition for a wake-word variant.
Keep README, license and source metadata outside that packer input directory.
Do not select stock WakeNet, MultiNet, NSNet or VadNet model options alongside
this feature: runtime rejects any inventory other than the single reviewed
Sophia model. WebRTC noise suppression and VAD do not add model assets.

The weights total 291,008 bytes before packing. The feature reserves 512 KiB
of internal flash. They are retained for the lifetime of the AFE; they do not
come from SD or a runtime network download.

The generated model image belongs under the selected `artifacts/build-*`
folder and is included by ESP-SR in the normal authorized firmware flash flow.
When intentionally replacing weights, perform an explicitly authorized clean
feature build so the upstream packer cannot retain its previous image.
See [wake-on-word architecture](../../docs/architecture/wake-on-word.md).
