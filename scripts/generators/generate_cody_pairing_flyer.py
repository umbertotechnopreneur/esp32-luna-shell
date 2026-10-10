"""Create the four-page A5 Cody pairing flyer without technical appendices.

Artwork is preserved from the original 4 October flyer (Git fa284003).
The instructions reflect the current local-plugin and retained-pairing flow.
"""

from pathlib import Path

from reportlab.lib.colors import HexColor
from reportlab.lib.pagesizes import A5
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "docs/cody/cody-ccp-pairing-quickstart-a5-v1.pdf"
ARTWORK = ROOT / "assets/cody/docs/pairing-companion-pc.png"
WIDTH, HEIGHT = A5
MARGIN = 27
CONTENT = WIDTH - MARGIN * 2
INK = HexColor("#132A38")
MUTED = HexColor("#536D7D")
TEAL = HexColor("#008D87")
BLUE = HexColor("#188BC4")
PALE = HexColor("#EFF6F8")
RULE = HexColor("#D4E3E9")
WHITE = HexColor("#FFFFFF")


# No parameters.
# Raises OSError or ReportLab font errors when a required typeface is unavailable.
def register_fonts():
    """Keep the typography consistent with the original Windows flyer."""
    font_dir = Path("C:/Windows/Fonts")
    pdfmetrics.registerFont(TTFont("CodyBody", str(font_dir / "segoeui.ttf")))
    pdfmetrics.registerFont(TTFont("CodyBold", str(font_dir / "segoeuib.ttf")))
    pdfmetrics.registerFont(TTFont("CodyCode", str(font_dir / "consola.ttf")))


# pdf: Destination ReportLab canvas.
# value: Complete printable ASCII text.
# x: Left coordinate in points.
# y: Baseline coordinate in points.
# size: Font size in points.
# font: Registered font identity.
# color: Foreground color.
def text(pdf, value, x, y, size=11, font="CodyBody", color=INK):
    pdf.setFont(font, size)
    pdf.setFillColor(color)
    pdf.drawString(x, y, value)


# pdf: Destination ReportLab canvas.
# value: Complete paragraph text.
# x: Left coordinate in points.
# y: First baseline coordinate in points.
# width: Maximum line width in points.
# size: Font size in points.
# leading: Baseline spacing in points.
# color: Foreground color.
# font: Registered font identity.
def paragraph(pdf, value, x, y, width=CONTENT, size=11, leading=16, color=MUTED, font="CodyBody"):
    """Wrap by measured glyph width and return the next free baseline."""
    words = value.split()
    line = ""
    for word in words:
        candidate = f"{line} {word}".strip()
        if line and pdfmetrics.stringWidth(candidate, font, size) > width:
            text(pdf, line, x, y, size, font, color)
            y -= leading
            line = word
        else:
            line = candidate
    if line:
        text(pdf, line, x, y, size, font, color)
        y -= leading
    return y


# pdf: Destination ReportLab canvas.
# number: One-based page position in the four-page flyer.
# section: Short navigation label.
# title: Display heading.
# subtitle: One-line introduction.
def page(pdf, number, section, title, subtitle):
    pdf.setFillColor(WHITE)
    pdf.rect(0, 0, WIDTH, HEIGHT, fill=1, stroke=0)
    text(pdf, "CODY / GET CONNECTED", MARGIN, HEIGHT - 30, 8.5, "CodyBold", TEAL)
    pdf.setFont("CodyBody", 8.5)
    pdf.setFillColor(MUTED)
    pdf.drawRightString(WIDTH - MARGIN, HEIGHT - 30, f"0{number} / {section}")
    for index in range(4):
        pdf.setFillColor(TEAL if index == number - 1 else RULE)
        pdf.roundRect(MARGIN + index * 19, HEIGHT - 47, 13, 3, 1.5, fill=1, stroke=0)
    text(pdf, title, MARGIN, HEIGHT - 84, 26, "CodyBold")
    text(pdf, subtitle, MARGIN, HEIGHT - 107, 10.5, color=MUTED)
    pdf.setStrokeColor(RULE)
    pdf.setLineWidth(0.5)
    pdf.line(MARGIN, 43, WIDTH - MARGIN, 43)
    text(pdf, "Cody AI Companion / Windows + Codex", MARGIN, 29, 8, color=MUTED)
    text(pdf, "Umberto Giacobbi / umbertogiacobbi.biz", MARGIN, 17, 7.5, color=MUTED)
    pdf.setFont("CodyBold", 8)
    pdf.setFillColor(TEAL)
    pdf.drawRightString(WIDTH - MARGIN, 24, f"{number} / 4")


# pdf: Destination ReportLab canvas.
# number: Step number within this page.
# title: Short action heading.
# body: Practical action instructions.
# y: Heading baseline coordinate in points.
def step(pdf, number, title, body, y):
    pdf.setFillColor(TEAL)
    pdf.circle(MARGIN + 10, y + 3, 10, fill=1, stroke=0)
    pdf.setFont("CodyBold", 11)
    pdf.setFillColor(WHITE)
    pdf.drawCentredString(MARGIN + 10, y - 1, str(number))
    text(pdf, title, MARGIN + 31, y, 13, "CodyBold")
    return paragraph(pdf, body, MARGIN + 31, y - 22, CONTENT - 31) - 19


# pdf: Destination ReportLab canvas.
# title: Short callout heading.
# body: Practical explanation.
# y: Upper edge in points.
# height: Reserved callout height in points.
def callout(pdf, title, body, y, height):
    pdf.setFillColor(PALE)
    pdf.roundRect(MARGIN, y - height, CONTENT, height, 10, fill=1, stroke=0)
    text(pdf, title, MARGIN + 14, y - 23, 11.5, "CodyBold", TEAL)
    paragraph(pdf, body, MARGIN + 14, y - 43, CONTENT - 28, size=10.5, leading=15)


# pdf: Destination ReportLab canvas.
def prepare_page(pdf):
    page(pdf, 1, "GET READY", "Your quieter sidekick.", "Four small steps from first setup to everyday use.")
    pdf.drawImage(str(ARTWORK), 165, 325, 231, 154, preserveAspectRatio=True, mask="auto")
    text(pdf, "BEFORE YOU START", MARGIN, 455, 9, "CodyBold", TEAL)
    paragraph(pdf, "A Windows PC with Codex and its command-line app.", MARGIN, 432, 143, size=11)
    paragraph(pdf, "Python available on the PC.", MARGIN, 370, 137, size=11)
    y = step(pdf, 1, "Connect Cody to Wi-Fi", "Follow the Wi-Fi setup shown on Cody. Keep Cody and your PC on the same reachable local network.", 299)
    step(pdf, 2, "Keep your PC nearby", "Open Codex on the PC you want Cody to remember. You will enter the code from Cody in a chat.", y)
    callout(pdf, "Already set up?", "If CCP is already enabled in Codex, skip to page 3. Pairing uses your local network; it does not need public Internet.", 133, 76)
    pdf.showPage()


# pdf: Destination ReportLab canvas.
def plugin_page(pdf):
    page(pdf, 2, "ON YOUR PC", "Enable Cody in Codex.", "CCP is Cody's Codex Plugin. Install it once.")
    y = step(pdf, 1, "Add the local plugin source", "Open Terminal in the Cody repository folder supplied with your setup. Run this one command:", 454)
    pdf.setFillColor(INK)
    pdf.roundRect(MARGIN + 31, y - 8, CONTENT - 31, 32, 6, fill=1, stroke=0)
    text(pdf, "codex plugin marketplace add .", MARGIN + 42, y + 4, 10, "CodyCode", WHITE)
    y -= 54
    y = step(pdf, 2, "Install and enable CCP", "Restart Codex. Open Plugins, find Cody local plugins, then install and enable CCP - Cody's Codex Plugin.", y)
    y = step(pdf, 3, "Allow its activity hooks", "In a local Codex session, open /hooks. Review and approve the CCP hooks. They let Cody show what your chat is doing.", y)
    callout(pdf, "If Windows asks about the network", "Allow Python on Private networks only. Use your home or trusted office network, rather than a public or guest Wi-Fi.", min(y + 7, 145), 78)
    pdf.showPage()


# pdf: Destination ReportLab canvas.
def pairing_page(pdf):
    page(pdf, 3, "PAIR CODY", "One code. Your PC.", "Use the live code on the Cody you are holding.")
    y = step(pdf, 1, "Open Connect Codex", "Finish Wi-Fi setup and choose Connect Codex. To open Setup later, hold the still character for 1.5 seconds, then release.", 454)
    y = step(pdf, 2, "Read the six-character code", "Keep the code visible on Cody. In a new Codex chat with CCP enabled, ask:", y)
    pdf.setFillColor(PALE)
    pdf.roundRect(MARGIN + 31, y - 27, CONTENT - 31, 52, 8, fill=1, stroke=0)
    text(pdf, '"Discover my Cody and pair it', MARGIN + 44, y + 6, 12, "CodyBold", TEAL)
    text(pdf, 'using this code: ..."', MARGIN + 44, y - 13, 12, "CodyBold", TEAL)
    text(pdf, "Replace ... with the code on Cody.", MARGIN + 31, y - 46, 10, color=MUTED)
    y -= 79
    y = step(pdf, 3, "Wait for confirmation", "Wait for You're all set on Cody, then tap Let's go. Cody looks for your PC straight away. Connected means it has found CCP.", y)
    callout(pdf, "If Cody already remembers a PC", "Choose Keep pairing to reconnect. Use Pair again only when you intend to replace it. A temporary disconnection needs no new code.", min(y + 5, 144), 78)
    pdf.showPage()


# pdf: Destination ReportLab canvas.
def everyday_page(pdf):
    page(pdf, 4, "EVERY DAY", "Back to what matters.", "Keep Codex running. Let Cody bring you the useful bits.")
    y = step(pdf, 1, "Start a chat in Codex", "Send a message in your chat. Cody follows the chat you most recently messaged and shows its current activity.", 454)
    y = step(pdf, 2, "Read, choose, then send", "For a question on Cody, tap Open, read the choices and review your selection. Tap Send to answer. Reply sent confirms delivery.", y)
    y = step(pdf, 3, "Make it yours in the chat", 'Try "Call me Alex", "Set quiet hours from 22:00 to 07:00", or "Disable quiet hours". Your choice keeps the other settings.', y)
    text(pdf, "YOUR SCHEDULE. YOUR CHOICE.", MARGIN, 205, 9, "CodyBold", TEAL)
    paragraph(pdf, "Quiet hours are optional. Ask Codex to check Cody's timezone before you set a schedule.", MARGIN, 183, size=10.5, leading=15)
    callout(pdf, "Not connecting?", "Keep CCP enabled and both devices on the same private network. If a pairing code expires, use the new code shown on Cody. If Cody is connected but idle, send a new chat message and check /hooks.", min(y + 6, 152), 91)
    text(pdf, "Approvals requested by Codex are answered on your PC.", MARGIN, 53, 9.2, "CodyBold", MUTED)
    pdf.showPage()


# No parameters.
# Raises OSError when artwork, fonts or the output path cannot be read or written.
def main():
    register_fonts()
    pdf = canvas.Canvas(str(OUTPUT), pagesize=A5, pageCompression=1)
    pdf.setTitle("Cody - Get connected in four steps")
    pdf.setAuthor("Umberto Giacobbi")
    pdf.setSubject("Four-page A5 pairing flyer for nontechnical Cody users")
    prepare_page(pdf)
    plugin_page(pdf)
    pairing_page(pdf)
    everyday_page(pdf)
    pdf.save()
    print(f"Cody pairing flyer: {OUTPUT} (4 A5 pages)")


if __name__ == "__main__":
    main()
