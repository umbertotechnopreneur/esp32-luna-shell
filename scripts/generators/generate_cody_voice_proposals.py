"""Author ten synthetic voice-screen proposals and the firmware waveform lookup."""
from pathlib import Path
import math
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'docs/cody/ui-proposals/20261012-voice'

# frame: Authored animation phase; values describe artwork, never measured sound.
def samples(frame):
    return [round(18 * math.sin(index / 63 * math.pi * 5 + frame * math.pi / 8)
                  * math.sin(index / 63 * math.pi) ** 2) for index in range(64)]

# size: Preview font size in logical display pixels.
# Raises OSError if the explicitly selected system font is unavailable.
def font(size):
    return ImageFont.truetype('C:/Windows/Fonts/consola.ttf', size)

# surface: Preview canvas owner.
# label: Synthetic authored caption.
# y: Logical top edge inside the circular display.
# size: Preview font size, not a claim about a retired firmware frame.
# color: Preview foreground.
def centered(surface, label, y, size, color):
    face = font(size)
    width = surface.textbbox((0, 0), label, font=face)[2]
    surface.text(((466 - width) / 2, y), label, font=face, fill=color)

# index: Stable proposal number.
# Raises OSError for artifact publication failures.
def proposal(index):
    image = Image.new('RGB', (466, 466), '#161e29')
    draw = ImageDraw.Draw(image)
    draw.ellipse((0, 0, 465, 465), fill='black')
    centered(draw, 'Listening', 92, 28, '#dceeff')
    palette = [('#28c8ff', '#9264ff'), ('#28e0d1', '#4c94ff'), ('#6ebeff', '#f090e0'),
               ('#299fff', '#d95fff'), ('#39d8ff', '#848cff')][index % 5]
    data = samples(index)
    for layer in range(3):
        for x in range(13, 452):
            position = (x - 13) * 63 / 439
            first = min(62, int(position))
            value = data[first] + (data[first + 1] - data[first]) * (position - first)
            value = value if layer == 0 else -value * .75 + 3 if layer == 1 else value * .55 - 3
            color_a = tuple(int(palette[0][part:part + 2], 16) for part in (1, 3, 5))
            color_b = tuple(int(palette[1][part:part + 2], 16) for part in (1, 3, 5))
            mix = (x - 13) / 439
            shade = 1 if layer == 0 else .55 if layer == 1 else .35
            color = tuple(round((a * (1 - mix) + b * mix) * shade) for a, b in zip(color_a, color_b))
            draw.line((x, 190 + value - 1, x, 190 + value + 1), fill=color)
    labels = ['Cody', 'branch: main', 'model: gpt-6.1', 'Cody', 'branch: codex/voice']
    centered(draw, labels[index % 5], 300, 23, '#dceeff')
    centered(draw, 'Balanced', 333, 18, '#929eb2')
    centered(draw, 'Shake to cancel', 365, 21, '#adb9cb')
    centered(draw, f'00:{max(0, 59 - index * 5):02}', 402, 27, '#dceeff')
    return image

# No parameters: publishes proposal images and a small immutable waveform table.
# Raises OSError for font or output failures.
def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    header = ROOT / 'components/CODY_companion/include/CODY_voice_waveform.hpp'
    rows = ',\n'.join('    {' + ', '.join(str(value) for value in samples(frame)) + '}' for frame in range(16))
    header.write_text('#pragma once\n#include <cstdint>\n\nnamespace CODY {\n'
        '// Authored synthetic ribbon: no runtime trigonometry or audio amplitude claim.\n'
        'inline constexpr unsigned kVoiceWaveformFrameCount = 16;\n'
        'inline constexpr int8_t kVoiceWaveform[kVoiceWaveformFrameCount][64] = {\n' + rows + '\n};\n}\n', encoding='ascii')
    sheet = Image.new('RGB', (5 * 486, 2 * 526 + 84), '#f3f6fa')
    draw = ImageDraw.Draw(sheet)
    draw.text((20, 12), 'CODY VOICE / TEN DESIGN PROPOSALS', font=font(30), fill='#18283e')
    draw.text((20, 50), 'Synthetic artwork and metadata. Not device captures. Wave band: 440 x 40 px.',
              font=font(22), fill='#53647b')
    for index in range(10):
        image = proposal(index)
        image.save(OUTPUT / f'voice-{index + 1:02}.png')
        x, y = (index % 5) * 486 + 10, (index // 5) * 526 + 84
        sheet.paste(image, (x, y))
        draw.text((x + 8, y + 476), f'{index + 1:02} / concept', font=font(20), fill='#18283e')
    sheet.save(OUTPUT / 'contact-sheet.png')
    (OUTPUT / 'README.md').write_text('# Voice screen proposals\n\nTen synthetic design studies, not device captures. '
        'Project/model/branch/cost captions are illustrative and never enter firmware snapshots. '
        'The firmware shares only the sixteen immutable waveform poses. The model price is a catalog '
        'cost band, not dollars or a measured session bill.\n', encoding='ascii')
    print(f'Authored ten proposals and contact sheet under {OUTPUT}')

if __name__ == '__main__':
    main()
