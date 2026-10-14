# VBWR B
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
# VibeWare: Human intent, AI execution, and plenty of tokens
# Copyright (c) 2026 Umberto Giacobbi
# VBWR E

"""Generate eight reviewable question-flow proposals; no device or Codex traffic."""
from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
import base64
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "docs/cody/ui-proposals/20261014-questions"
FONT = ROOT / "third_party/esp-brookesia/system/brookesia_system_super/resource/fonts/Telex-Regular.ttf"
CLOUD = ROOT / "assets/cody/boot/cody-ready-pearl.png"
SIZE = 466
WHITE = "#eef4f6"
GRAY = "#94a1ab"
MINT = "#58f4d4"
AMBER = "#ffbe55"
BLUE = "#78bbff"


class Screen(Enum):
    REPLY_READY = "01-reply-ready"
    ATTENTION = "02-answer-needed"
    PENDING = "03-pending-chats"
    CHOICES = "04-answers-one-two"
    MORE = "05-answer-three"
    REVIEW = "06-read-full-answer"
    SENDING = "07-sending-reply"
    DELIVERED = "08-reply-received"


@dataclass(frozen=True)
class Proposal:
    screen: Screen
    title: str
    note: str
    previous: Screen | None
    next: Screen | None


PROPOSALS = (
    Proposal(Screen.REPLY_READY, "Another chat has replied", "A background reply is ready. Current work stays visible.", None, Screen.ATTENTION),
    Proposal(Screen.ATTENTION, "A chat needs your answer", "An amber notice names the chat and the pending count.", Screen.REPLY_READY, Screen.PENDING),
    Proposal(Screen.PENDING, "Choose the pending chat", "Two pending chats. Opening one preserves followed activity.", Screen.ATTENTION, Screen.CHOICES),
    Proposal(Screen.CHOICES, "Answers 1 and 2", "Two original choices. Swipe for more; tap to read in full.", Screen.PENDING, Screen.MORE),
    Proposal(Screen.MORE, "Scroll to answer 3", "The next page replaces the pair. No free-text answer.", Screen.CHOICES, Screen.REVIEW),
    Proposal(Screen.REVIEW, "Read, then send", "Number + original label and description. Tap or swipe text.", Screen.CHOICES, Screen.SENDING),
    Proposal(Screen.SENDING, "Wait for the recipient", "One fixed answer. Sending is not proof of receipt.", None, Screen.DELIVERED),
    Proposal(Screen.DELIVERED, "Reply received", "Show only after the matching acknowledgement. One remains.", None, Screen.REPLY_READY),
)
CHOICES = (
    {"id": "device", "number": 1, "label": "Store on device", "description": "Keep preferences on Cody and restore them after a restart."},
    {"id": "pc", "number": 2, "label": "Sync to PC", "description": "Save preferences on this computer and sync them to Cody when connected. The computer keeps a copy, so the same preferences can be restored after a device replacement."},
    {"id": "session", "number": 3, "label": "Keep this session only", "description": "Keep preferences only for this session. Discard them when the session ends."},
)


# size: Logical font size; authoring uses the committed OFL Telex source.
# Exception: Preserves missing/invalid font errors.
def font(size: int):
    return ImageFont.truetype(str(FONT), size)


# text: Exact printable ASCII label or provided description.
# width: Available logical pixels; words are never removed or replaced by ellipses.
# face: The same font used to render the result.
# Exception: Rejects a single word wider than its viewport.
def wrap(text: str, width: int, face):
    result = []
    line = ""
    for word in text.split():
        if face.getlength(word) > width:
            raise ValueError(f"Unrepresentable proposal word: {word}")
        candidate = f"{line} {word}".strip()
        if line and face.getlength(candidate) > width:
            result.append(line)
            line = word
        else:
            line = candidate
    return result + ([line] if line else [])


class Canvas:
    # self: One isolated 466-pixel design surface; not the firmware renderer.
    # Exception: Preserves asset/font read errors.
    def __init__(self):
        self.image = Image.new("RGB", (SIZE, SIZE), "black")
        self.draw = ImageDraw.Draw(self.image)
        self.image.paste(Image.open(CLOUD).convert("RGB").resize((200, 200), Image.Resampling.LANCZOS), (133, -2))

    # self: Logical design surface.
    # text: Complete ASCII caption.
    # y: Logical top edge.
    # size: Logical font size.
    # color: Semantic foreground.
    # x: Horizontal center, defaulting to the circle center.
    # Exception: Rejects text outside the measured circle chord.
    def text(self, text, y, size=20, color=WHITE, x=233):
        face = font(size)
        width = face.getlength(text)
        box = (x - width / 2, y, x + width / 2, y + size + 4)
        self.bounds(box)
        self.draw.text((x, y), text, font=face, fill=color, anchor="mt")

    # self: Logical design surface.
    # rectangle: Complete artwork or hit bounds.
    # Exception: Rejects even a rectangular corner beyond the physical circle.
    def bounds(self, rectangle):
        left, top, right, bottom = rectangle
        if any((x - 233) ** 2 + (y - 233) ** 2 > 233 ** 2 for x in (left, right) for y in (top, bottom)):
            raise ValueError(f"Proposal leaves circular chord: {rectangle}")

    # self: Logical design surface.
    # rectangle: Fully admitted rounded control.
    # color: Semantic stroke/gradient source.
    # radius: Logical corner radius.
    # Exception: Preserves chord and color errors.
    def card(self, rectangle, color=MINT, radius=24):
        self.bounds(rectangle)
        rgb = Image.new("RGB", (1, 1), color).getpixel((0, 0))
        left, top, right, bottom = rectangle
        gradient = Image.new("RGB", (right-left, bottom-top))
        painter = ImageDraw.Draw(gradient)
        for row in range(bottom-top):
            gain = .20 - .08 * row / max(1, bottom-top-1)
            painter.line((0, row, right-left, row), fill=tuple(round(channel*gain) for channel in rgb))
        mask = Image.new("L", gradient.size)
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, gradient.width-1, gradient.height-1), radius=radius, fill=255)
        self.image.paste(gradient, (left, top), mask)
        self.draw.rounded_rectangle(rectangle, radius=radius, outline=tuple(round(channel*.48) for channel in rgb), width=1)

    # self: Logical design surface.
    # label: Complete action caption.
    # color: Semantic action accent.
    # Exception: Preserves chord/text-bound failures.
    def action(self, label, color=MINT):
        self.card((113, 352, 353, 416), color, 30)
        self.text(label, 374, 20, WHITE)

    # self: Logical design surface with a valid previous route.
    # Exception: Preserves rendering errors; circle target remains the repository's full 60 pixels.
    def back(self):
        self.draw.ellipse((10, 203, 70, 263), fill="#11171b")
        self.draw.line((45, 223, 35, 233, 45, 243), fill=GRAY, width=3)

    # self: Logical design surface.
    # heading: Main screen identity.
    # context: Exact sample chat name or queue context.
    # color: Main semantic state accent.
    # Exception: Preserves text-bound errors.
    def header(self, heading, context, color=AMBER):
        self.text(heading, 172, 26, color)
        self.text(context, 211, 17, GRAY)

    # self: Logical design surface.
    # choice: Original, stable provider option label and number.
    # top: Logical top edge of a full-width answer row.
    # color: Semantic choice accent.
    # Exception: Preserves chord/text failures; labels remain on one complete line.
    def choice(self, choice, top, color):
        self.card((73, top, 393, top+60), color)
        self.text(f"{choice['number']}. {choice['label']}", top+21, 19, WHITE)

    # self: Logical design surface.
    # caption: Visible answer range; identity comes from the provider, not the viewport index.
    # Exception: Preserves chord/text-bound failures.
    def pager(self, caption):
        self.text(f"Swipe / {caption}", 397, 15, GRAY)

    # self: Logical design surface.
    # Exception: Preserves conversion errors; outside-circle pixels are transparent.
    def finish(self):
        mask = Image.new("L", (SIZE, SIZE))
        ImageDraw.Draw(mask).ellipse((0, 0, SIZE-1, SIZE-1), fill=255)
        result = self.image.convert("RGBA")
        result.putalpha(mask)
        return result


# proposal: Typed flow identity; never infer semantic meaning from a list position.
# Exception: Rejects unknown screens or invalid geometry instead of silently clipping controls.
def render(proposal):
    canvas = Canvas()
    screen = proposal.screen
    if proposal.previous:
        canvas.back()
    if screen == Screen.REPLY_READY:
        canvas.header("Working", "Firmware / gpt-6-astra", MINT)
        canvas.card((73, 248, 393, 337))
        canvas.text("Website", 260, 17, GRAY)
        canvas.text("Reply ready", 285, 25, MINT)
        canvas.action("Got it")
    elif screen == Screen.ATTENTION:
        canvas.header("Your answer needed", "Firmware / 2 pending")
        canvas.text("Where should", 263, 23)
        canvas.text("preferences live?", 291, 23)
        canvas.action("Open questions", AMBER)
    elif screen == Screen.PENDING:
        canvas.header("Pending questions", "2 chats need you")
        for top, name, topic in ((256, "Firmware", "Storage"), (328, "Website", "Theme")):
            canvas.card((73, top, 393, top+60), AMBER)
            canvas.text(f"{name} / {topic}", top+21, 18, WHITE)
        canvas.text("Tap a chat to reply", 397, 15, GRAY)
    elif screen in (Screen.CHOICES, Screen.MORE):
        canvas.header("Choose a reply", "Firmware")
        canvas.text("Where should preferences live?", 236, 16)
        if screen == Screen.CHOICES:
            canvas.choice(CHOICES[0], 256, MINT)
            canvas.choice(CHOICES[1], 328, BLUE)
            canvas.pager("1-2 / 3")
        else:
            canvas.choice(CHOICES[2], 256, BLUE)
            canvas.pager("3 / 3")
    elif screen == Screen.REVIEW:
        canvas.header("Review your reply", "Firmware")
        canvas.card((73, 245, 393, 331), BLUE, 18)
        canvas.text("2. Sync to PC", 257, 21, BLUE)
        canvas.text("Save preferences on this", 288, 18)
        canvas.text("computer and sync them to Cody", 309, 16)
        canvas.draw.line((379, 263, 379, 314), fill="#29435a", width=3)
        canvas.draw.line((379, 263, 379, 281), fill=BLUE, width=3)
        canvas.text("Tap or swipe text to read", 336, 14, GRAY)
        canvas.action("Send reply")
    elif screen == Screen.SENDING:
        canvas.header("Sending reply", "Firmware", BLUE)
        canvas.card((89, 257, 377, 319), BLUE)
        canvas.text("2. Sync to PC", 276, 22, WHITE)
        canvas.draw.arc((215, 346, 251, 382), start=10, end=295, fill=BLUE, width=3)
        canvas.text("Waiting for Codex", 397, 16, GRAY)
    elif screen == Screen.DELIVERED:
        canvas.header("Reply sent", "Firmware / Answer received", MINT)
        canvas.card((89, 257, 377, 319))
        canvas.text("2. Sync to PC", 276, 22)
        canvas.text("1 question still pending", 336, 14, GRAY)
        canvas.action("Back to work")
    else:
        raise ValueError(f"Unknown question proposal: {screen}")
    return canvas.finish()


# images: One complete logical raster for every typed proposal.
# Exception: Preserves asset/write errors; drawing on this board never changes individual device rasters.
def board(images):
    result = Image.new("RGB", (2112, 1460), "#10151b")
    painter = ImageDraw.Draw(result)
    painter.text((32, 26), "CODY / QUESTIONS ACROSS CHATS", font=font(31), fill=WHITE)
    painter.text((32, 73), "8-screen design proposal / 466 x 466 / two visible choices / sample content", font=font(18), fill=GRAY)
    for position, proposal in enumerate(PROPOSALS):
        left, top = (position % 4)*528+16, (position//4)*650+122
        painter.rounded_rectangle((left, top, left+496, top+628), radius=28, fill="#171e26", outline="#26323d")
        number = proposal.screen.value.split("-")[0]
        painter.text((left+16, top+16), f"{number} / {proposal.title}", font=font(18), fill=AMBER if proposal.screen in (Screen.ATTENTION, Screen.PENDING) else MINT)
        result.paste(images[proposal.screen], (left+15, top+53), images[proposal.screen])
        for line, caption in enumerate(wrap(proposal.note, 455, font(16))):
            painter.text((left+20, top+540+line*24), caption, font=font(16), fill=GRAY)
    painter.text((32, 1430), "DESIGN ONLY / existing assets and documented routes / no live requests, firmware changes or physical validation", font=font(14), fill=GRAY)
    result.save(OUTPUT / "eight-screen-flow.png")


# manifest: Typed screen identities and their links, with original sample option labels.
# Exception: Preserves template/output errors; this offline prototype has no network API.
def prototype(manifest):
    encoded_font = base64.b64encode(FONT.read_bytes()).decode("ascii")
    template = r'''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Cody question flow / design proposal</title>
<style>@font-face{font-family:Telex;src:url(data:font/ttf;base64,__FONT__)}
*{box-sizing:border-box}body{margin:0;background:#10151b;color:#eef4f6;font-family:Telex,Arial,sans-serif}
main{max-width:1240px;margin:auto;padding:28px}h1{font-size:26px}p{color:#94a1ab;line-height:1.6}
.stage{display:flex;gap:32px;align-items:center;flex-wrap:wrap}.device{width:466px;height:466px;position:relative;flex-shrink:0}
.device img{width:100%;height:100%;image-rendering:pixelated}.hit{position:absolute;background:none;border:0;cursor:pointer;border-radius:22px}
.hit:hover,.hit:focus-visible{outline:2px solid #78bbff;outline-offset:2px}.details{max-width:450px}
button.nav{background:#1c2e35;border:1px solid #35535a;border-radius:20px;padding:10px 18px;color:#eef4f6;font:inherit;cursor:pointer;margin:5px}
.strip{display:grid;grid-template-columns:repeat(8,1fr);gap:8px;margin-top:24px}.strip button{padding:7px;background:#171e26;border:1px solid #26323d;border-radius:12px;color:#94a1ab;cursor:pointer}.strip img{width:100%;image-rendering:pixelated}
.read{position:absolute;left:73px;top:245px;width:320px;height:86px;border-radius:18px;background:#0a1724;color:#eef4f6;overflow-y:auto;padding:10px 18px;font-size:18px;line-height:1.35;scrollbar-color:#78bbff #29435a;cursor:pointer}
.read strong{color:#78bbff;display:block;font-weight:normal;font-size:21px;margin-bottom:8px}
.selected{position:absolute;left:89px;top:257px;width:288px;height:62px;border-radius:24px;background:#0a1724;display:flex;align-items:center;justify-content:center;font-size:22px}
.hidden{display:none}
@media(min-width:1800px){.device{transform:scale(2);transform-origin:top left;margin-right:466px;margin-bottom:466px}}
@media(max-width:700px){main{padding:12px}.strip{grid-template-columns:repeat(4,1fr)}}
</style><main><h1>Cody / eight-screen question flow</h1><p>DESIGN PROPOSAL / sample chats and choices. Click the display controls; wheel or swipe for the next answer page.</p>
<div class="stage"><div class="device" id="device"><img id="frame" alt="Current design screen"><div id="hits"></div><div id="read" class="read hidden" tabindex="0"></div><div id="selected" class="selected hidden"></div></div>
<div class="details"><h2 id="title"></h2><p id="note"></p><p id="help"></p><button class="nav" id="previous">Previous mockup</button><button class="nav" id="next">Next mockup</button><p>Only Send reply commits the selected option in this prototype. Receipt is a separate illustrated state, advanced manually. No Codex request is sent.</p></div></div>
<div id="strip" class="strip"></div><p><a href="font-license.txt" style="color:#94a1ab">Telex / SIL Open Font License 1.1</a></p></main><script>
const data=__MANIFEST__,byId=new Map(data.screens.map(s=>[s.id,s]));
let current=data.screens[0].id,chosen=data.choices[1];
const frame=document.getElementById('frame'),hits=document.getElementById('hits'),read=document.getElementById('read');
function go(id){current=id;paint()}
function hit(rect,label,action){const b=document.createElement('button');b.className='hit';b.setAttribute('aria-label',label);b.title=label;b.style.cssText=`left:${rect[0]}px;top:${rect[1]}px;width:${rect[2]}px;height:${rect[3]}px`;b.onclick=action;hits.appendChild(b)}
function review(option){chosen=option;go('06-read-full-answer')}
function paint(){const s=byId.get(current);frame.src=s.id+'.png';document.getElementById('title').textContent=s.id.slice(0,2)+' / '+s.title;document.getElementById('note').textContent=s.note;hits.replaceChildren();read.classList.add('hidden');document.getElementById('selected').classList.add('hidden');
document.getElementById('help').textContent=current==='06-read-full-answer'?'Tap the numbered text to start scrolling; wheel or drag to read. Send stays fixed.':current==='04-answers-one-two'||current==='05-answer-three'?'Two choices per page at most. Wheel or swipe the display to change page.':'Click the visible action, or advance the design with Next mockup.';
if(s.previous)hit([10,203,60,60],'Back without answering',()=>go(s.previous));
if(current==='01-reply-ready')hit([113,352,240,64],'Dismiss the reply notice / preview the next event',()=>go('02-answer-needed'));
if(current==='02-answer-needed')hit([113,352,240,64],'Open pending questions',()=>go('03-pending-chats'));
if(current==='03-pending-chats'){hit([73,256,320,60],'Open Firmware question',()=>go('04-answers-one-two'));hit([73,328,320,60],'Website sample question / no live chat',()=>{document.getElementById('help').textContent='Website is the second pending chat. This storyboard follows the Firmware question.'})}
if(current==='04-answers-one-two'){hit([73,256,320,60],'1. Store on device / read full answer',()=>review(data.choices[0]));hit([73,328,320,60],'2. Sync to PC / read full answer',()=>review(data.choices[1]))}
if(current==='05-answer-three'){hit([73,256,320,60],'3. Keep this session only / read full answer',()=>review(data.choices[2]))}
if(current==='06-read-full-answer'){read.replaceChildren();const strong=document.createElement('strong');strong.textContent=chosen.number+'. '+chosen.label;read.append(strong,document.createTextNode(chosen.description));read.classList.remove('hidden');read.scrollTop=0;hit([113,352,240,64],'Send the selected answer',()=>go('07-sending-reply'))}
if(current==='07-sending-reply'||current==='08-reply-received'){const selected=document.getElementById('selected');selected.textContent=chosen.number+'. '+chosen.label;selected.classList.remove('hidden')}
if(current==='08-reply-received')hit([113,352,240,64],'Return to the illustrated Home context',()=>go('01-reply-ready'));
document.getElementById('previous').disabled=!s.previous;document.getElementById('next').disabled=!s.next;
}
function page(delta){if(current==='04-answers-one-two'&&delta>0)go('05-answer-three');else if(current==='05-answer-three'&&delta<0)go('04-answers-one-two')}
document.getElementById('device').addEventListener('wheel',e=>{if(e.target.closest('.read'))return;if(current==='04-answers-one-two'||current==='05-answer-three'){e.preventDefault();page(e.deltaY)}},{passive:false});
let touchY=0;document.getElementById('device').addEventListener('touchstart',e=>{touchY=e.changedTouches[0].clientY},{passive:true});document.getElementById('device').addEventListener('touchend',e=>{const delta=touchY-e.changedTouches[0].clientY;if(Math.abs(delta)>35&&!e.target.closest('.read'))page(delta)},{passive:true});
read.onclick=()=>read.scrollTo({top:read.scrollTop<10?read.scrollHeight:0,behavior:'smooth'});
document.getElementById('previous').onclick=()=>{const id=byId.get(current).previous;if(id)go(id)};document.getElementById('next').onclick=()=>{const id=byId.get(current).next;if(id)go(id)};
for(const s of data.screens){const b=document.createElement('button'),img=document.createElement('img');img.src=s.id+'.png';img.alt=s.title;b.append(img,document.createTextNode(s.id.slice(0,2)));b.onclick=()=>go(s.id);document.getElementById('strip').appendChild(b)}paint();
</script></html>'''
    (OUTPUT / "prototype.html").write_text(template.replace("__FONT__", encoded_font).replace("__MANIFEST__", json.dumps(manifest)), encoding="utf-8")


# No parameters: fixed project-bound proposal output through scripts/device.ps1 generate.
# Exception: Preserves original source/render/write failures; creates no PDF or firmware state.
def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    images = {proposal.screen: render(proposal) for proposal in PROPOSALS}
    for identity, image in images.items():
        image.save(OUTPUT / f"{identity.value}.png")
        image.resize((932, 932), Image.Resampling.NEAREST).save(OUTPUT / f"{identity.value}-2x.png")
    manifest = {"kind": "DESIGN PROPOSAL / NOT IMPLEMENTED", "logical_size": [466, 466],
        "maximum_visible_answers": 2, "free_text": False, "choices": CHOICES,
        "source_crosswalk": [
            {"pdf": "docs/cody/cody-navigation-atlas-v1.pdf", "pdf_page": 11, "screen_ids": ["26", "27"]},
            {"pdf": "docs/cody/cody-ccp-connection-atlas-v1.pdf", "pdf_page": 13, "screen_ids": ["26", "27", "31"]},
            {"pdf": "docs/cody/cody-planning-atlas-v1.pdf", "pdf_page": 14, "screen_ids": ["26", "27"]}],
        "proposal_change": "At most two full-width numbered single-line choices per viewport; further choices replace the visible pair through swiping. Buttons have no icons; the lateral Back chevron remains. Selection opens numbered full text, and only Send commits. Native approvals stay on the PC.",
        "terminal_states": "Offline preserves identity and original deadline. Expired, cancelled or cleared requests never select an answer. Sent-but-unacknowledged remains unconfirmed. Completion notices never replace an open question.",
        "screens": [{"id": p.screen.value, "title": p.title, "note": p.note,
            "previous": p.previous.value if p.previous else None, "next": p.next.value if p.next else None} for p in PROPOSALS]}
    board(images)
    prototype(manifest)
    (OUTPUT / "flow.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (OUTPUT / "font-license.txt").write_bytes((ROOT / "assets/cody/home-dashboard-fonts/OFL.txt").read_bytes())
    print(f"Eight question-flow proposals generated: {OUTPUT / 'eight-screen-flow.png'}")
    print(f"Offline mouse/touch prototype: {OUTPUT / 'prototype.html'}")


if __name__ == "__main__":
    main()
