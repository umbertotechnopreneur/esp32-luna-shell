# Purchased Waveshare hardware references

Imported on 2026-10-15 from the two product folders supplied by the owner.
These are reference snapshots, separate from the firmware component roots.
The import does not add support for either board or replace the existing
ESP32-S3-Touch-AMOLED-1.75 GPS profile.

| Product | Original files copied | Original bytes | Local gallery |
| --- | ---: | ---: | --- |
| [ESP32-S3-Touch-AMOLED-1.75C](ESP32-S3-Touch-AMOLED-1.75C/) | 615 | 166268638 | [Gallery](ESP32-S3-Touch-AMOLED-1.75C/gallery.html) |
| [ESP32-S3-Touch-AMOLED-1.43C](ESP32-S3-Touch-AMOLED-1.43C/) | 182 | 174135890 | [Gallery](ESP32-S3-Touch-AMOLED-1.43C/gallery.html) |

The [compatibility assessment](../../../docs/bsp/boards/purchased-amoled-comparison.md)
compares both purchases with the current prototype and records the proposed
porting work. Deferred implementation belongs in the root
[planned-todo.md](../../../planned-todo.md).

The supplied GitHub ZIP comments identify these upstream revisions (archive
metadata, not a separately authenticated release):

| Product | Source revision recorded in ZIP |
| --- | --- |
| 1.75C | [`657452b66d206cff50e6f0712d470f20667ccd30`](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C/tree/657452b66d206cff50e6f0712d470f20667ccd30) |
| 1.43C | [`3ac3f7f5a4f1bf7db35d85bed2bd553cd89d2a16`](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.43C/tree/3ac3f7f5a4f1bf7db35d85bed2bd553cd89d2a16) |

## Preservation and Git storage

The original folders were copied without rewriting their contents. Their
collection notes, original languages, upstream source comments, old framework
examples, firmware packages, drawings and images are retained as supplied.
Their installation, build and flashing instructions are reference material,
not permission to execute them. Project operations remain subject to the root
[AGENTS.md](../../../AGENTS.md) and use `scripts/device.ps1`.

The 1.43C `software/waveshare-source-main.zip` is present locally in full
(134324048 bytes), but is excluded from Git: it exceeds GitHub's ordinary
[100 MiB per-file limit](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github).
The extracted ESP-IDF examples and other supplied resources are versioned.
The complete upstream archive can be obtained from the official
[1.43C revision archive](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.43C/archive/3ac3f7f5a4f1bf7db35d85bed2bd553cd89d2a16.zip).
Regenerating a GitHub ZIP does not guarantee the original container's bytes;
the full original ZIP remains in the local reference folder.
No Git LFS setup or remote publication was performed.

The 1.75C upstream Apache-2.0 license was additionally extracted from its
supplied ZIP into
[software/UPSTREAM-LICENSE.txt](ESP32-S3-Touch-AMOLED-1.75C/software/UPSTREAM-LICENSE.txt).
Existing component licenses are preserved. The 1.43C archive has no top-level
license; this import makes no blanket license claim for it. See the root
[third-party provenance](../../../THIRD_PARTY_NOTICES.md).

The sixteen imported documentation PDFs were also copied without conversion
under the required Obsidian `Cody Companion/atlas/waveshare-hardware/` directory,
using one product subdirectory per board to preserve both `schematic.pdf` files
and their original filenames. Neither schematic was revised.

## Official sources

| Product | Store | Documentation | Source |
| --- | --- | --- | --- |
| 1.75C | [Product](https://www.waveshare.com/esp32-s3-touch-amoled-1.75c.htm) | [Wiki](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75C) | [GitHub](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C) |
| 1.43C | [Product](https://www.waveshare.com/esp32-s3-touch-amoled-1.43c.htm) | [Wiki](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.43C) | [GitHub](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.43C) |
