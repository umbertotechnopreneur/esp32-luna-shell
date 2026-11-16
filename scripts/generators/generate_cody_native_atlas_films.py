"""Create mirrored annotation films for a published Cody native atlas PDF."""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from uuid import uuid4
from reportlab.lib.colors import HexColor
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas

ROOT = Path(__file__).resolve().parents[2]
ARTIFACTS = (ROOT / 'artifacts').resolve()
LOGO = ROOT / 'cody-device/simulator/windows/assets/branding/vibeware-logo.png'
RED, STRUCT, PALE, GRID, TEAL, INK = map(HexColor, ['#880808', '#C79595', '#D8B1B1', '#E3E3E3', '#007F83', '#14283E'])

# name: Font name; filename: matching TrueType file in standard or configured locations.
# Raises FileNotFoundError if the documentation-standard font is unavailable.
def register_font(name, filename):
    roots = [Path(os.environ.get('CODY_FILM_FONT_DIR', '')), Path(os.environ.get('WINDIR', 'C:/Windows')) / 'Fonts',
             Path.home() / '.local/share/fonts', Path('/usr/local/share/fonts')]
    path = next((root / filename for root in roots if (root / filename).is_file()), None)
    if not path:
        raise FileNotFoundError(f'Install {filename} or set CODY_FILM_FONT_DIR')
    pdfmetrics.registerFont(TTFont(name, str(path)))

# pdf: Atlas PDF; extracts text geometry using Poppler, without changing the source.
# Raises subprocess or XML errors when geometry cannot be read.
def page_geometry(pdf):
    with tempfile.TemporaryDirectory(dir=ARTIFACTS / 'tmp') as directory:
        out = Path(directory) / 'geometry.html'
        subprocess.run(['pdftotext', '-bbox-layout', str(pdf), str(out)], check=True, timeout=240)
        root = ET.parse(out).getroot()
    ns = {'h': 'http://www.w3.org/1999/xhtml'}
    result = []
    for page in root.findall('.//h:page', ns):
        lines = []
        for line in page.findall('.//h:line', ns):
            words = [word.text or '' for word in line.findall('h:word', ns)]
            lines.append((float(line.get('xMin')), float(line.get('yMin')), float(line.get('xMax')), float(line.get('yMax')), ' '.join(words)))
        result.append((float(page.get('width')), float(page.get('height')), lines))
    return result

# c: Canvas in normal reading orientation; w/h: physical page dimensions in points.
# Draws the reusable frame, rulers, print swatches and registration marks.
def frame(c, w, h):
    x0, x1, y0, y1 = 10*mm, w-10*mm, 36*mm, h-17*mm
    c.setStrokeColor(RED); c.setLineWidth(.5); c.setDash(3, 2); c.rect(x0, y0, x1-x0, y1-y0, stroke=1, fill=0)
    c.setDash(); c.setLineWidth(.25); c.setFont('FilmMono', 4.8); c.setFillColor(RED)
    for i in range(191):
        x=x0+i*mm; length=(1.2 if i%10==0 else .8 if i%5==0 else .4)*mm
        for y, direction in ((y0,-1),(y1,1)): c.line(x,y,x,y+direction*length)
        if i%10==0:
            c.drawCentredString(x,y1+2.1*mm,str(i//10)); c.drawCentredString(x,y0-3.1*mm,str(i//10))
    for i in range(245):
        y=y1-i*mm; length=(1.2 if i%10==0 else .8 if i%5==0 else .4)*mm
        for x, direction in ((x0,-1),(x1,1)): c.line(x,y,x+direction*length,y)
        if i%10==0:
            c.drawRightString(x0-2.0*mm,y-1.5,str(i//10)); c.drawString(x1+2.0*mm,y-1.5,str(i//10))
    c.setStrokeColor(PALE); c.setLineWidth(.3); c.setDash(3,3); c.line(w/2,y0,w/2,y1)
    c.setDash(); c.setStrokeColor(RED); c.setLineWidth(.6)
    c.line(w/2-2.5*mm,h/2,w/2+2.5*mm,h/2); c.line(w/2,h/2-2.5*mm,w/2,h/2+2.5*mm)
    for index, color in enumerate((RED,STRUCT,PALE,GRID,TEAL,INK)):
        c.setFillColor(color); c.setStrokeColor(INK); c.setLineWidth(.15)
        c.rect((w-23*mm)/2+index*4*mm,h-8.5*mm,3*mm,3*mm,fill=1,stroke=1)
    return x0,x1,y0,y1

# c: Canvas; pages: grouped source pages; geometry: source page line data.
# family: Screen for fixed slots or Text for page-specific prose.
def film(c, pages, geometry, family):
    w,h,lines=geometry
    c.saveState(); c.translate(w,0); c.scale(-1,1)
    x0,x1,y0,y1=frame(c,w,h)
    c.setStrokeColor(STRUCT); c.setLineWidth(.34); c.setDash(3,3)
    for x in ([40,200,355] if family=='Screen' else [40, w-40]): c.line(x,y0,x,y1)
    if family=='Screen':
        # Slot boundaries are fixed by the atlas layout; image interiors stay clear.
        for top in (82, 283, 484):
            c.line(40,h-(top+145),w-34,h-(top+145))
    else:
        # Only gaps that exist between all text occupying that y position get a rule.
        intervals=sorted({(round(a,2),round(b,2)) for _,a,_,b,_ in lines if 45 < a < h-120})
        merged=[]
        for a,b in intervals:
            if merged and a <= merged[-1][1]: merged[-1]=(merged[-1][0],max(merged[-1][1],b))
            else: merged.append((a,b))
        c.setStrokeColor(GRID); c.setLineWidth(.16); c.setDash(1.4,2.5)
        for (_,b),(a,_) in zip(merged,merged[1:]):
            if a-b >= 1: c.line(40,h-(a+b)/2,w-40,h-(a+b)/2)
    c.setDash(); c.setFillColor(TEAL); c.setFont('FilmSans',5.5)
    c.drawImage(str(LOGO), 11*mm, 10.2*mm, width=4*mm,height=4*mm,mask='auto',preserveAspectRatio=True)
    c.drawString(16*mm, 11*mm, 'VibeWare / Cody')
    c.setFillColor(INK); c.setFont('FilmMono',5.2)
    label=f'{family} / source pages {pages}'
    c.drawRightString(w-11*mm, 11*mm, label)
    c.restoreState(); c.showPage()

# argv: Source PDF and new artifact-owned film PDF destination.
# Raises on non-A4 input, missing fonts, invalid paths, or generation errors.
def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdf',required=True); parser.add_argument('--output',required=True)
    args=parser.parse_args(argv); source=Path(args.pdf).resolve(strict=True); dest=Path(args.output).resolve()
    if not source.is_relative_to(ARTIFACTS) or not dest.is_relative_to(ARTIFACTS) or dest.suffix.lower()!='.pdf':
        raise ValueError('Source and output must be PDFs beneath artifacts/')
    if dest.exists(): raise FileExistsError(dest)
    register_font('FilmSans','segoeui.ttf'); register_font('FilmMono','consola.ttf')
    pages=page_geometry(source)
    if any(abs(w-595.276)>.1 or abs(h-841.89)>.1 for w,h,_ in pages): raise ValueError('Expected A4 portrait pages')
    screen=[i for i,(_,_,lines) in enumerate(pages,1) if any('Acquisition:' in line[4] for line in lines)]
    if not screen or screen != list(range(screen[0],screen[-1]+1)): raise ValueError('Cannot identify contiguous screen pages')
    for index in screen:
        positions = sorted(float(line[1]) for line in pages[index-1][2] if 'Acquisition:' in line[4])
        if not 1 <= len(positions) <= 3 or any(abs(value - expected) > 1 for value, expected in zip(positions, (82.9, 283.9, 484.9))):
            raise ValueError(f'Screen page {index} does not match the reusable film geometry')
    dest.parent.mkdir(parents=True,exist_ok=True); temporary=dest.with_name('.'+dest.stem+'-'+uuid4().hex+'.pdf')
    c=canvas.Canvas(str(temporary),pagesize=pages[0][:2],pageCompression=1)
    for index,geometry in enumerate(pages,1):
        if index==screen[0]: film(c,f'{screen[0]}-{screen[-1]}',geometry,'Screen')
        elif index not in screen: film(c,str(index),geometry,'Text')
    c.save(); os.link(temporary,dest); temporary.unlink()
    print(f'{dest}\nScreen film: source pages {screen[0]}-{screen[-1]}\nText films: source page 1, then pages {screen[-1]+1}-{len(pages)}')

if __name__=='__main__': main()
