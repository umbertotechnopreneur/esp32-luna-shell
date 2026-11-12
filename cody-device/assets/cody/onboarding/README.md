<img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="72">

# Onboarding artwork

The `source` PNGs are the high-resolution generated originals. The native
display uses the `generated` 466 x 466 backgrounds and a 136 x 136 button
illustration. Each contains a 256-entry big-endian RGB565 palette followed by
one byte per pixel. The button illustration preserves the full square image;
it occupies the right side of the button guide, while instructions remain on
the left. Text and the project QR are rendered independently by the firmware.

Regenerate the panel resources from the retained sources with
`scripts/device.ps1 generate -Generator cody-onboarding-art`.
Regenerate the button illustration with
`scripts/device.ps1 generate -Generator cody-onboarding-button`.

The built-in ImageGen prompts for the original images were:

> Use case: illustration-story. Asset type: high resolution atmospheric background for Cody's round 466x466 device onboarding. Create a warm retro-futurist deep indigo star field with a delicate 1980s rainbow light trail and a gentle golden glow suggesting a new companion awakening. Quiet central area reserved for the real animated character and readable UI drawn separately. Rich but restrained, premium playful VibeWare aesthetic. Square composition, no character, no device, no icons, no letters, no text, no logo, no watermark.

> Use case: illustration-story. Asset type: high resolution atmospheric background for Cody's round 466x466 device onboarding. Create a warm retro-futurist night sky with subtle cyan and amber concentric sound ripples and tiny stars, evoking a friendly voice listening experience. Quiet dark central area reserved for accessible UI drawn separately. Cohesive playful 1980s rainbow-accented aesthetic. Square composition, no character, no device, no icons, no letters, no text, no logo, no watermark.

---

**VibeWare** · Created by Umberto Giacobbi · [Cody on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) · [VibeWare](https://github.com/umbertotechnopreneur/VibeWare)
