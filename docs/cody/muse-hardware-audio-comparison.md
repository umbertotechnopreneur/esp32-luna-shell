# Muse hardware and audio comparison

This reference compares Muse Gadgets at commit `d0a65ab` with Cody's maintained
Waveshare ESP32-S3-Touch-AMOLED-1.75C path. It records static source findings,
not measured audio quality, power consumption or physical-device acceptance.
Muse's standard `s3` configuration uses ESP-IDF 6.0.3; Cody retains its own
ESP-IDF 6.0.2, native CODY2D, BSP and acquisition-owner contracts.

## Board and application boundaries

Muse separates board hooks (`muse_board_t`) from shared application, voice,
settings and network code. Its catalog contains **53 hardware entries**, while
the full Muse UI selection contains **38 concrete board profiles**, excluding
the `NONE` option. These are source catalog counts, not 53 fully tested or
feature-equivalent devices. Some entries are experimental or provide only
status, LEDs or voice. LCD/AMOLED, slow e-paper and LED-only devices require
different presentation behavior.

Sources: [device catalog](https://github.com/facebookincubator/muse-gadget-sdk/blob/d0a65ab/esp32/devices/README.md),
[UI profile selection](https://github.com/facebookincubator/muse-gadget-sdk/blob/d0a65ab/esp32/components/muse/Kconfig),
[board contract](https://github.com/facebookincubator/muse-gadget-sdk/blob/d0a65ab/esp32/components/muse/muse_board.h).
Cody already separates [board, interface, optional services and build mode](../bsp/README.md).
Extend those existing boundaries rather than importing Muse's LVGL-facing
board interface into native Cody.

## Startup and power ownership

Muse initializes I2C, buttons and the PMU before its UI, then starts audio after
the boot animation. On 1.75C, `muse_pmu_keep_rails(BIT(0), BIT(0))` preserves
already-enabled DCDC1 and ALDO1 while disabling other rails. It does **not** set
their voltages or enable a rail that was off. PMU failure is logged and startup
continues. The 1.75C board provides neither `codec_power` nor `audio_power` hooks;
generic Muse comments about switched codec supplies do not establish ALDO1
power cycling on this board.

Cody's PMU acquisition owner explicitly configures and reads back DCDC1 at
3.3 V and, when microphone input is selected, ALDO1 at 3.3 V. It preserves
unrelated enable bits. A rail-configuration failure is startup-critical on
1.75C. Keep this ownership and failure behavior.

Sources: [Muse board initialization and hooks](https://github.com/facebookincubator/muse-gadget-sdk/blob/d0a65ab/esp32/components/muse/boards/board_waveshare_s3_175c.c),
[Muse rail masking](https://github.com/facebookincubator/muse-gadget-sdk/blob/d0a65ab/esp32/components/muse/muse_pmu.c#L121),
[Muse application startup](https://github.com/facebookincubator/muse-gadget-sdk/blob/d0a65ab/esp32/components/muse/muse_app.c#L70),
[Cody rail owner](../../cody-device/components/LUNASHELL_platform/src/snapshot/LUNASHELL_acquisition_pmu.cpp),
[Cody hardware orchestration](../../cody-device/components/LUNASHELL_platform/src/snapshot/LUNASHELL_hardware.cpp).

## Audio transport and processing

Both use ES8311 output, ES7210 input and `esp_codec_dev` 1.5.11. The 1.75C
pinout is MCLK 16, BCLK 9, WS 45, output 8, input 10 and amplifier enable 46.
Both create the I2S transport before constructing the speaker and microphone
codec objects, then open the speaker before the microphone.

| Behavior | Muse standard 1.75C | Cody 1.75C |
| --- | --- | --- |
| Initial BSP transport | Philips I2S, PCM16 mono, 22,050 Hz; subsequently reconfigured | Philips I2S, PCM16 stereo, 22,050 Hz |
| Active capture | PCM16 stereo at 16,000 Hz, blocks of 320 frames | PCM16 with selected microphone topology; 22,050 Hz diagnostics or 16,000 Hz with AFE |
| Microphone processing | Averages both channels, then an 80 Hz one-pole high-pass filter | Retains separate channels for measurements and AFE input |
| Output API | Duplicates mono samples into both stereo slots | Mono PCM16; codec data interface coordinates shared transport slots |
| Default gain | 30 dB, restored from settings when available | 24 dB applied by initialization, voice-route preparation and diagnostic capture |
| Output silence | Speaker preference replaces PCM with zeros | Codec mute at idle, unmute for playback, mute afterward |
| Level reporting | RMS-derived dBFS of processed mono | Mean absolute PCM and peak for each microphone; these are not RMS or calibrated SPL |

Application sample depth and I2S slot width are different concepts. The codec
data interface negotiates slot width between mono output and stereo input.
Cody also rejects incompatible peer sample rates explicitly. Do not change
the capture clock to accommodate a playback clip.

Muse's normal voice-note upload stays at 16 kHz. Its stateful linear resampler
serves other paths, including optional dictation and decoded playback. Cody
already converts prerecorded clips to the active 16 kHz voice transport and
feeds the same converted samples to its AEC reference. Its streaming speech
path currently uses a phase-preserving sample-and-hold converter.

Sources: [Muse audio owner](https://github.com/facebookincubator/muse-gadget-sdk/blob/d0a65ab/esp32/components/muse/muse_audio.c),
[Muse rate and chunk size](https://github.com/facebookincubator/muse-gadget-sdk/blob/d0a65ab/esp32/components/muse/muse_audio.h#L31),
[Muse resampler](https://github.com/facebookincubator/muse-gadget-sdk/blob/d0a65ab/esp32/components/muse/muse_chat_session.cpp#L953),
[Cody BSP](../../cody-device/components/esp32_s3_touch_amoled_1_75/esp32_s3_touch_amoled_1_75.c),
[Cody audio owner](../../cody-device/components/LUNASHELL_platform/src/audio/LUNASHELL_audio.cpp),
[Cody acquisition](../../cody-device/components/LUNASHELL_platform/src/snapshot/LUNASHELL_acquisition_audio.cpp).

## Voice services and local inference

The inspected Muse standard path uses `VOICE_NOTE=1`: it streams a WAV file as
base64 to `/chat/stream`, requesting text output. The server transcribes the
note. The alternative `/api/voice/dictation` branch is disabled; its comment
says that the VM's streaming dictation currently lacks ASR. `start_tts()` paces
captions with silence and describes an extension point for a developer-chosen
TTS API. Realtime voice is optional and was disabled in the inspected standard
build.

No local ASR, LLM or TTS model is used by this inspected Muse path, and the
client does not identify the server's model names. Local capture, gain,
filtering, metering and resampling are DSP, not language-model inference.
Cody's optional ESP-SR AFE is a different responsibility: it preserves dual
microphones, uses the actual speaker reference for AEC and can run its selected
local wake-word model. Its acquisition lease prevents competing diagnostic
reads; consumers use the canonical snapshot.

Sources: [voice-note selection](https://github.com/facebookincubator/muse-gadget-sdk/blob/d0a65ab/esp32/components/muse/muse_chat_session.cpp#L145),
[request format](https://github.com/facebookincubator/muse-gadget-sdk/blob/d0a65ab/esp32/components/muse/muse_chat_priv.h#L78),
[silent caption pacing](https://github.com/facebookincubator/muse-gadget-sdk/blob/d0a65ab/esp32/components/muse/muse_chat_session.cpp#L1586),
[optional live voice](https://github.com/facebookincubator/muse-gadget-sdk/blob/d0a65ab/esp32/components/muse/muse_voice_call.cpp#L1094),
[Cody AFE](../../cody-device/components/LUNASHELL_platform/src/audio/LUNASHELL_voice_afe.cpp).

## Reply gestures

Muse composes gestures locally from alternating hand raises, three short beats,
an open presentation and rests. Real playback energy drives articulation and
lets the hands settle during pauses. Text-only replies get their own visual
rhythm. On interruption, the hands blend into the next pose over 280 ms, while
the mouth stops immediately. The controller uses bounded frame time and keeps
the complete moving silhouette inside the sprite.

For Cody, use this as a reference for a future Reply animation with the original
mascot. Pass playback energy or text pacing to the animation layer as separate
inputs. Keep the gesture timing local and independent of the chat provider.
[Muse speaking gestures](https://github.com/facebookincubator/muse-gadget-sdk/blob/d0a65ab/esp32/docs/speaking-animations.md).

## Power profiling reference

Muse publishes a summary CSV with one row per diagnostic state or explicit
skip. It records board-input current, source-setpoint power, capture coverage
and sampled energy. The findings describe the fixture and sampling limits. For
Cody, reuse the method of labeling states and documenting setup. The Watcher
values apply to its measured configuration, including its LCD and power rails.
[Watcher profile](https://github.com/facebookincubator/muse-gadget-sdk/blob/d0a65ab/esp32/docs/power/sensecap-watcher-profile.csv),
[measurement conditions](https://github.com/facebookincubator/muse-gadget-sdk/blob/d0a65ab/esp32/docs/power/sensecap-watcher-findings.md).

## Reuse candidates and limits

- Add volume and gain policy to the existing Cody audio owner, with confirmed
  applied values, explicit failures and settings-owner persistence. A UI-only
  setter would be overwritten by current route preparation.
- Use the codec's actual gain steps. Muse maps a displayed 33 dB request to
  34.5 dB because the driver otherwise maps it to 30 dB. Cody should display
  the applied value instead of copying this mismatch.
- Build two native microphone meters from existing canonical channel samples.
  Add RMS/dBFS at the acquisition owner only if that metric is needed. Do not
  replace dual-channel measurements or AFE input with Muse's mono mix.
- Consider an explicitly scoped mono voice-note projection and high-pass
  filter after preserving the real input. Do not introduce a second RX owner.
- Retain Cody's clock checks, acknowledged AFE pause, standby rollback and
  failure publication. Muse's ignored setter/close results and temporary I2S
  log suppression are not patterns to copy.
- Keep Cody artwork. Muse's Apache-2.0 license explicitly excludes the
  Jollybot avatar; third-party files retain their own licenses, and Apache-2.0
  does not grant trademark rights. See the pinned
  [license boundaries](https://github.com/facebookincubator/muse-gadget-sdk/blob/d0a65ab/README.md#license)
  and [license](https://github.com/facebookincubator/muse-gadget-sdk/blob/d0a65ab/LICENSE).

These candidates require no LVGL dependency or replacement of Cody's BSP.
Physical behavior, noise, clipping, latency and power savings remain unmeasured
by this comparison and require separately authorized device validation.
