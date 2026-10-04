# Painted cloud / triangle-mesh appearance study

This opt-in Windows simulator appearance study replaces only the cloud body and its
four vector relief paths. The existing face, navigation, firmware and sensor
consumers remain unchanged. This is not a device-ready texture renderer.

## Artwork and provenance

`cloud-body-master.png` is the unmodified transparent sprite generated on
2026-10-03 with the built-in `image_gen` tool, following the imagegen skill.
It is saved here rather than referenced from the tool's output directory.
The first input was the earlier simulator capture (silhouette/proportions only);
the second was the user's painted cloud reference (shading and texture only).
There is deliberately no face in this asset. The existing face is drawn separately.

Runtime preparation decodes the PNG once, determines visible bounds with alpha
>= 64 and four source pixels of padding, and resizes using premultiplied alpha.
For this master the crop is `(85, 86, 1420, 1003)` and the texture is 320 x 220.
The RGB565 plus alpha8 data representation would use 211,200 bytes (206.25 KiB),
excluding mesh, working buffers and allocator overhead. The Python study does
not have that memory layout or footprint: it retains Pillow RGBA buffers.

## Deformation contract

- 99 named vertices in an 11 x 9 lattice, with 138 retained triangles after
  omitting cells wholly outside source alpha. The mesh is a first trial, not
  a hand-tuned final topology.
- Rest coordinates fit the existing cloud's undeformed bounding envelope;
  the generated artwork approximates that silhouette rather than claiming
  pixel equality with the vector outline.
- Each vertex has fixed UV coordinates and normalized weights to the named
  center and contour-lobe anchors. Weights are computed once. The root influence
  keeps the facial region comparatively quiet.
- Every frame reuses the evaluated rig pose. Triangle inverse mapping samples
  the same texture with premultiplied bilinear filtering; alpha and painted
  shadows therefore move together. There is no independent vector body clip.
- Composition happens once after triangle rasterization, avoiding double-alpha
  darkening at shared triangle edges. Collapsed or flipped triangles are errors.
- The face and navigation are not baked into the PNG. There is no dynamic
  relighting and no procedural cloud-height calculation in this mode.
- Captures retain RGB565-quantized source/output colors. GIF palette reduction
  is only a sharing format; inspect PNG stills for color quality.

## Reproduce through the public simulator CLI

```powershell
.\scripts\device.ps1 simulator -CloudPngMeshStudy -OutputPath artifacts/simulator/cloud-png-mesh-v1.png
.\scripts\device.ps1 simulator -CloudPngMeshStudy -MeshOverlay -OutputPath artifacts/simulator/cloud-png-mesh-wire-v1.png
.\scripts\device.ps1 simulator -CloudPngMeshStudy -OutputPath artifacts/simulator/cloud-png-mesh-motion-v1.gif
```

The GIF is a 32-frame, 3.2-second looping synthetic lobe sweep. Root translation
is deliberately stationary so local deformation is visible. It uses legal
authored contour inputs (amplitude 1, fullness +/-0.8), not recorded IMU samples
or the production response engine. A four-phase full-color pose sheet is saved
beside it. `-AtMs` and `-SafeGuide` are not accepted for this fixed GIF timeline.
The opt-in appearance study currently supports capture mode only.

Only authorized visual preview captures and static source review were performed.
No automated tests, firmware configure/build/flash, live sensors or performance
measurement ran. Native sampling/packing, device integration, CPU cost, extreme
pose/navigation clearance and broader validation remain open.

## Exact image-generation prompt

Use case: style-transfer. Asset type: transparent 2D game character body texture, to be deformed by a triangle mesh on an embedded display. Input image 1 is the silhouette/proportion guide ONLY: the front-facing cloud mascot in the middle of the black simulator screen. Input image 2 is the painted cloud STYLE reference, not its wide triangular silhouette. Create ONE isolated cloud body with real transparent background, no face: REMOVE eyes, eyebrows, nose, mouth, cheeks, buttons, text, black background and all interface. Keep image 1's recognizable outline and aspect ratio approximately 1.43 width/height: one high rounded central crown, mid-height left shoulder, a smaller upper-right shoulder, broad side lobes, a gently scalloped rounded underside with small feet. Subject centered, fully visible, occupying about 82% of square canvas width. Inside that same outline paint soft ivory cloud volume in the style of image 2: warm pale cream sunlight from upper left, luminous rounded bulges, cool desaturated blue-grey shadows underneath and at actual contour lobe junctions, fine painterly grain and a few subtle cloud curls. Shadows MUST describe the actual outline lobes; no tiled spheres, no pearls, no cauliflower, no craters, no repetitive pillows. Every large outside lobe is one coherent shaded cloud volume. Keep the middle face region broad, smooth, light and unmarked for separately rendered animated eyes and mouth. Fine scalloped edges with clean soft alpha, not fuzzy smoke. No drop shadow outside the cloud. No checkerboard pattern baked in. No ground, no props, no lettering, no face whatsoever. This is final painted sprite artwork, not a screenshot.
