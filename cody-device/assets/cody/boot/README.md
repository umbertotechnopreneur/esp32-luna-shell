# Cody pearl boot frames

`cody-sleep-pearl.png`, `cody-waking-pearl.png`, and `cody-ready-pearl.png` are the opaque 1254-square Imagegen source frames. The built-in image generator was used in reference-guided edit mode (`transparent_background=false`), starting from the existing pearl Cody body and then editing each face while keeping its silhouette, scale, position, pastel material, soft coral cheeks, and black backdrop consistent. These sources are not decoded on-device.

The final visual briefs were:

- Ready: one front-facing pearl cloud on pure black; rounded asymmetric lobes, turquoise-cream highlights and lavender-blue shaded lower lobes; two clearly open black eyes, small eyebrows, nose, and a gentle smile; subtle blended coral cheeks; no text or device frame.
- Sleeping: preserve the Ready cloud's shape and material; close both eyes with relaxed curved lids, keep a tiny round sleepy mouth and restrained cheeks; pure black backdrop, no additional marks.
- Waking: preserve the Sleeping cloud's shape and material; two drowsy half-open eyes with dark pupils and a small round mouth; pure black backdrop, no additional marks.

Run `scripts/device.ps1 generate -Generator package-cody-boot` after source changes. This produces 466-square, 63-color indexed RGB565 row-RLE tables in `generated/`; the firmware reads these directly from flash without a PNG decoder or full-screen asset buffer. The original generator outputs remain in the local Codex image store; these project PNGs are the selected copies.
