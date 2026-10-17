"""Author ten historical synthetic voice-screen proposals without changing firmware."""
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
# pose: Optional independent animation pose, preserving the surrounding proposal.
# Raises OSError for artifact publication failures.
def proposal(index, pose=None):
    image = Image.new('RGB', (466, 466), '#161e29')
    draw = ImageDraw.Draw(image)
    draw.ellipse((0, 0, 465, 465), fill='black')
    centered(draw, 'Listening', 92, 28, '#dceeff')
    palette = [('#28c8ff', '#9264ff'), ('#28e0d1', '#4c94ff'), ('#6ebeff', '#f090e0'),
               ('#299fff', '#d95fff'), ('#39d8ff', '#848cff')][index % 5]
    for layer in range(4):
        data = samples(((index if pose is None else pose) + layer * 4) % 16)
        for x in range(13, 452):
            position = (x - 13) * 63 / 439
            first = min(62, int(position))
            value = data[first] + (data[first + 1] - data[first]) * (position - first)
            value = value * (1, .75, .5, .875)[layer] + (0, 3, -3, -1)[layer]
            color_a = tuple(int(palette[0][part:part + 2], 16) for part in (1, 3, 5))
            color_b = tuple(int(palette[1][part:part + 2], 16) for part in (1, 3, 5))
            mix = (x - 13) / 439
            color = tuple(round(a * (1 - mix) + b * mix) for a, b in zip(color_a, color_b))
            center = 190 + value
            for y in range(max(170, math.floor(center - 2)), min(210, math.ceil(center + 2))):
                coverage = max(0, min(1, 2 - abs(y + .5 - center)))
                opacity = coverage * (230, 155, 120, 170)[layer] / 255
                background = image.getpixel((x, y))
                image.putpixel((x, y), tuple(round(a * opacity + b * (1 - opacity)) for a, b in zip(color, background)))
    labels = ['model: gpt-6.1', 'cost: Balanced', 'effort: high', 'branch: main', 'Cody']
    centered(draw, labels[index % 5], 300, 23, '#dceeff')
    if index % 2 == 0:
        centered(draw, 'Shake to fade', 365, 21, '#adb9cb')
    else:
        # A native-width clipped row illustrates the long message's measured scroll.
        hint = Image.new('RGB', (320, 28), 'black')
        hint_draw = ImageDraw.Draw(hint)
        hint_draw.text((-((index // 2) * 55), 0),
                       'We will send the audio to Codex for transcription',
                       font=font(21), fill='#adb9cb')
        image.paste(hint, (73, 365))
    used = 1 + index * 5
    centered(draw, f'{used:02}s elapsed / {60 - used:02}s left', 402, 16, '#dceeff')
    return image

# No parameters: publishes historical proposal artwork only.
# Raises OSError for font or output failures.
def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
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
    animation = [proposal(0, pose=frame) for frame in range(16)]
    animation[0].save(OUTPUT / 'voice-animation.gif', save_all=True, append_images=animation[1:],
                      duration=125, loop=0, disposal=2)
    (OUTPUT / 'README.md').write_text('# Voice screen proposals\n\nTen synthetic design studies, not device captures. '
        'Project/model/branch/cost captions are illustrative and never enter firmware snapshots. '
        'Firmware uses its separately maintained procedural ribbon; this generator never overwrites it. The model price is a catalog '
        'cost band, not dollars or a measured session bill. The lower row alternates '
        'Shake to fade with We will send the audio to Codex for transcription; '
        'the long message scrolls within the clipped row. Shake still cancels input.\n', encoding='ascii')
    print(f'Authored ten proposals and contact sheet under {OUTPUT}')

if __name__ == '__main__':
    main()
