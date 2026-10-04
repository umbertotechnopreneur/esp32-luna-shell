# LUNASHELL_boot

Optional startup presentation for the native Luna application.

- [UI](src/ui/README.md): boot-screen presentation and its disabled-feature stub.
- [Audio](src/audio/README.md): boot cue playback and its disabled-feature stub.

The public API stays in `include/`. Root CMake retains feature admission and embeds
the existing generated assets without moving them.
