# Wake-word models

The model directory contains unmodified Espressif weights for
`wn9_sophia_tts_v1`, `wn9_jarvis_tts` and `wn9_computer_tts`. **Sophia** remains
the only configured and active wake word. Jarvis and Computer are packaged for
a possible later choice; they are not active or exposed to users. None of these
models recognizes Cody or transcribes speech or commands.

Source: [ESP-SR 2.4.6 source snapshot](https://github.com/espressif/esp-sr/tree/cfdbb52172d4f1b3e5f7469d6c3c6e5dd0738143/model/wakenet_model).
The immutable source commit, original file lengths and SHA-256 provenance for
each model are recorded in `source.json`. The original **ESPRESSIF MIT License**
is retained in `LICENSE`, including its Espressif-products condition.

Only directories containing `_MODEL_INFO_` go below `models/`. ESP-SR packs
that directory into the internal `model` partition for a wake-word variant.
Keep README, license and source metadata outside that packer input directory.
Do not select stock WakeNet, MultiNet, NSNet or VadNet model options alongside
this feature: runtime rejects any inventory other than these three reviewed
models. WebRTC noise suppression and VAD do not add model assets.

The three models total 872,894 bytes before packing. The feature reserves
1,024 KiB of internal flash for one packed model image. Only Sophia is selected
by the AFE; Jarvis and Computer remain in flash without inference workers.
Models do not come from SD or a runtime network download.

The generated model image belongs under the selected `artifacts/build-*`
folder and is included by ESP-SR in the normal authorized firmware flash flow.
When intentionally replacing weights, perform an explicitly authorized clean
feature build so the upstream packer cannot retain its previous image.
See [wake-on-word architecture](../../docs/architecture/wake-on-word.md).
