#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Restyle the Voltaire 110 artwork towards the look of a Roland U-220 front panel.

    tools/restyle_panel.py style            rewrite the Inkscape sources in place
    tools/restyle_panel.py centre DIR       centre the labels, from `inkscape --query-all`

Two passes because the second needs the label widths in the NEW face, and only Inkscape
can tell those: run `style`, then for every source `inkscape --query-all X.svg >
DIR/X.svg.query`, then `centre DIR`.  Afterwards export as usual
(`plugin/tools/panel_export.py --text-to-path --background`).

What changes, and why:

  * The background photograph goes.  Its provenance is unknown, and a front panel is a
    flat painted plate anyway: a generated PNG (anthracite plate above, darker well for
    the DIVE pages below) takes its place, so the raster pass and the UI that draws it
    stay exactly as they are.
  * Earth-Mod goes.  It cannot be redistributed (CLONING.md says so at length); Barlow
    Semi Condensed is SIL OFL and lives in resources/fonts/, so anybody can re-export.
  * Labels become off-white capitals, buttons dark grey keys, the LCD a black bezel.
  * Labels are centred on the control they name.  The source placed them by eye for
    Earth-Mod's widths; a narrower face would leave every one of them off to the left.
"""

import os
import re
import struct
import sys
import zlib
import base64
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GFX = os.path.join(ROOT, 'resources', 'graphics')
PANEL = os.path.join(GFX, 'overall_panel_inkscape.svg')
PAGES = ['dive_setup.svg', 'dive_common.svg', 'dive_basic.svg', 'dive_level.svg',
         'dive_pitch.svg', 'dive_LFO.svg']

SVG = 'http://www.w3.org/2000/svg'
INK = 'http://www.inkscape.org/namespaces/inkscape'
SODI = 'http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd'
XLINK = 'http://www.w3.org/1999/xlink'
for p, u in (('', SVG), ('inkscape', INK), ('sodipodi', SODI), ('xlink', XLINK)):
    ET.register_namespace(p, u)

FACE = 'Barlow Semi Condensed'

# The palette.  Named for what they paint, so the next pass changes one line.
LABEL      = '#e9e9e6'      # front-panel printing
LABEL_TAB  = '#c8cbd0'      # DIVE tab captions, a step back from the panel's own
LOGO       = '#f4f4f2'
KEY        = '#4a4c51'      # button caps
KEY_EDGE   = '#121315'
BEZEL      = '#0e0f10'      # LCD surround
KNOB       = '#3b3d42'
LED_OFF    = '#3a1512'
METER      = '#0c0d0e'
TAB        = '#000000'      # tabs stay translucent: the UI darkens a selected one by
TAB_ALPHA  = '0.22'         # raising its opacity, which needs headroom to work with
TAB_EDGE   = '#0b0b0c'

# label -> the control it names.  Labels not listed (MIDI beside its LED, RESET beside
# its button, the arrows) are placed relative to something else and are left alone.
CENTRE_ON = {
    'T_vol': 'KNOB_volume_outline', 'T_filt': 'BUT_filter',
    'T_part': 'BUT_part_jump', 'T_jump': 'BUT_part_jump',
    'T_edit': 'BUT_edit_exit', 'T_exit': 'BUT_edit_exit',
    'T_dec': 'BUT_dec', 'T_inc': 'BUT_inc_exit', 'T_enter': 'BUT_inc_exit',
    'T_Patch': 'BUT_Patch_Menu', 'T_Tone': 'BUT_tone', 'T_Dive': 'BUT_dive',
    'T_Cart': 'BUT_Cart',
}
# Tabs are centred both ways: the caption sits inside its rectangle.
CENTRE_IN = {
    'T_P1': 'BUT_P1', 'T_P2': 'BUT_P2', 'T_P3': 'BUT_P3', 'T_P4': 'BUT_P4',
    'T_P5': 'BUT_P5', 'T_P6': 'BUT_P6', 'T_common': 'BUT_common', 'T_set': 'BUT_set',
    'T_LFO': 'BUT_LFO', 'T_pitch': 'BUT_pitch', 'T_level': 'BUT_level',
    'T_basic': 'BUT_basic',
}


def label(e):
    return e.get(f'{{{INK}}}label') or ''


def set_style(e, **props):
    """Set CSS properties in e's style attribute, keeping the others."""
    style = e.get('style') or ''
    parts = [p for p in style.split(';') if p.strip()]
    keys = {k.replace('_', '-'): v for k, v in props.items()}
    out = []
    for p in parts:
        k = p.split(':', 1)[0].strip()
        if k in keys:
            continue
        out.append(p)
    for k, v in keys.items():
        if v is not None:
            out.append(f'{k}:{v}')
    e.set('style', ';'.join(out))


def restyle_text(e, colour, weight='600'):
    for t in [e] + list(e.iter(f'{{{SVG}}}tspan')):
        set_style(t, font_family=f"'{FACE}'",
                  _inkscape_font_specification=f"'{FACE}, Semi-Bold'",
                  font_weight=weight, fill=colour, stroke='none',
                  letter_spacing='0.04em')
    # Earth-Mod set lower case as small capitals; Barlow would not.
    for t in e.iter():
        if t.text:
            t.text = t.text.upper()


# ------------------------------------------------------------------ background

def png(width, height, rows):
    """A minimal RGB PNG from a list of `height` rows of `width` (r,g,b) tuples."""
    raw = b''.join(b'\x00' + bytes(c for px in row for c in px) for row in rows)

    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data
                + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff))
    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def mix(a, b, t):
    return tuple(int(round(x + (y - x) * t)) for x, y in zip(a, b))


def background(canvas_w, canvas_h, plate_bottom, scale=2.0):
    """Anthracite plate over a darker well, as a PNG of the whole canvas."""
    w, h = int(round(canvas_w * scale)), int(round(canvas_h * scale))
    split = int(round(plate_bottom * scale))
    rows = []
    for y in range(h):
        if y < 3:
            c = (74, 75, 80)                                    # top edge catch-light
        elif y < split - 4:
            c = mix((54, 55, 60), (40, 41, 45), y / max(1, split))
        elif y < split:
            c = (14, 14, 16)                                    # the seam
        else:
            c = mix((31, 32, 35), (22, 23, 25), (y - split) / max(1, h - split))
        rows.append([c] * w)
    return png(w, h, rows)


# ------------------------------------------------------------------ passes

def style_panel():
    tree = ET.parse(PANEL)
    root = tree.getroot()
    vb = [float(v) for v in root.get('viewBox').split()]
    canvas_w, canvas_h = vb[2], vb[3]

    by_label = {label(e): e for e in root.iter() if label(e)}

    # The plate ends where the panel's own outline does: 2 + 209 user units down.
    outline = by_label['overall_outline']
    plate_bottom = 2.0 + float(outline.get('height')) + 2.0

    # Background: same element, new picture, no mirroring, no clip.  The layer is
    # translated, so undo that to land the image on the canvas origin.
    img = by_label['background_image']
    layer = next(g for g in root.iter(f'{{{SVG}}}g') if img in list(g))
    m = re.match(r'translate\(([-\d.e]+),([-\d.e]+)\)', layer.get('transform') or '')
    tx, ty = (float(m.group(1)), float(m.group(2))) if m else (0.0, 0.0)
    data = background(canvas_w, canvas_h, plate_bottom)
    img.set(f'{{{XLINK}}}href', 'data:image/png;base64,' + base64.b64encode(data).decode())
    for a in ('transform', 'clip-path', 'style'):
        img.attrib.pop(a, None)
    img.set('x', f'{-tx:.6f}')
    img.set('y', f'{-ty:.6f}')
    img.set('width', f'{canvas_w:.6f}')
    img.set('height', f'{canvas_h:.6f}')
    img.set('preserveAspectRatio', 'none')

    set_style(outline, fill='none', fill_opacity=None, stroke='#08080a',
              stroke_width='3')

    # The logo: drop the hand-made path, let the text be flattened in the new face.
    for parent in root.iter():
        for child in list(parent):
            if label(child) == 'logo_text_as_path':
                parent.remove(child)
    logo = by_label['logo_text']
    restyle_text(logo, LOGO, weight='700')
    # Upright: only the upright Barlow is checked in, and a slanted roman is worse than
    # none.
    set_style(logo, display='inline', font_style=None)
    logo.attrib.pop(f'{{{SODI}}}insensitive', None)

    for lab, e in by_label.items():
        tag = e.tag.split('}')[1]
        if tag == 'text' and lab.startswith('T_'):
            restyle_text(e, LABEL_TAB if lab in CENTRE_IN else LABEL)
        elif lab.startswith('T_p_'):                            # the two arrows
            set_style(e, fill=LABEL, stroke='none')
        elif lab in CENTRE_IN.values():
            set_style(e, fill=TAB, fill_opacity=TAB_ALPHA, stroke=TAB_EDGE,
                      stroke_width='1.5')
        elif lab.startswith('BUT_'):
            set_style(e, fill=KEY, fill_opacity='1', stroke=KEY_EDGE, stroke_width='2.5')
            e.set('rx', '2.2')
        elif lab == 'LCD_outer':
            set_style(e, fill=BEZEL, stroke='#000000', stroke_width='3')
            e.set('rx', '3')
        elif lab == 'KNOB_volume_outline':
            set_style(e, fill=KNOB, stroke=KEY_EDGE, stroke_width='3')
        elif lab == 'KNOB_volume_pointer':
            set_style(e, stroke=LABEL)
        elif lab.startswith('LED_'):
            set_style(e, fill=LED_OFF, stroke='#000000', stroke_width='1')
        elif lab == 'VU_Horiz_Stereo':
            set_style(e, fill=METER, stroke='#000000', stroke_width='2')

    tree.write(PANEL, encoding='utf-8', xml_declaration=True)


ACCENT_OLD = '#ffa228'          # the slider taps keep it; outlines lose it


def style_pages():
    for name in PAGES:
        path = os.path.join(GFX, name)
        tree = ET.parse(path)
        for e in tree.getroot().iter():
            tag = e.tag.split('}')[1]
            style = e.get('style') or ''
            if tag == 'text':
                restyle_text(e, LABEL)
            elif label(e).startswith('BUT_') or (tag == 'rect' and 'fill:#999999' in style):
                set_style(e, fill=KEY, fill_opacity='1', stroke=KEY_EDGE,
                          stroke_width='2.5')
                if tag == 'rect':
                    e.set('rx', '2.2')
            elif 'fill:none' in style and f'stroke:{ACCENT_OLD}' in style:
                set_style(e, stroke=LABEL_TAB)
        tree.write(path, encoding='utf-8', xml_declaration=True)


def read_query(path):
    boxes = {}
    with open(path) as f:
        for line in f:
            parts = line.strip().split(',')
            if len(parts) == 5:
                boxes[parts[0]] = tuple(float(v) for v in parts[1:])
    return boxes


SLIDER_LABEL_SIZE = '12.7px'


def centre_page(path, boxes):
    """Centre each slider's caption under it, one size for all of them.

    A caption is the text whose box overlaps a slider body (an SB_ group) sideways and
    starts below it.  Captions become middle-anchored, so every line of a two- or
    three-line caption is centred on its own, which moving the block as a whole would
    not do.
    """
    tree = ET.parse(path)
    root = tree.getroot()
    sliders = [e for e in root.iter(f'{{{SVG}}}g')
               if label(e).startswith('SB_') and e.get('id') in boxes]
    # User units per query unit, from the page's own width.
    vb = [float(v) for v in root.get('viewBox').split()]
    k = boxes[root.get('id')][2] / vb[2] if root.get('id') in boxes else 96 / 25.4

    done = 0
    for t in root.iter(f'{{{SVG}}}text'):
        if t.get('id') not in boxes:
            continue
        tx, ty, tw, th = boxes[t.get('id')]
        best = None
        for s in sliders:
            sx, sy, sw, sh = boxes[s.get('id')]
            if ty >= sy + sh * 0.5 and tx < sx + sw and tx + tw > sx:
                gap = ty - (sy + sh)
                if best is None or gap < best[0]:
                    best = (gap, s)
        if best is None:
            continue
        sx, sy, sw, sh = boxes[best[1].get('id')]
        x0 = float(t.get('x'))
        # The text's x is its box's left edge while start-anchored and its centre once a
        # previous run has made it middle-anchored, so a re-run moves nothing.
        anchored = 'text-anchor:middle' in (t.get('style') or '')
        x_new = x0 + ((sx + sw / 2) - (tx + tw / 2 if anchored else tx)) / k
        for e in [t] + list(t.iter(f'{{{SVG}}}tspan')):
            set_style(e, text_anchor='middle', text_align='center',
                      font_size=SLIDER_LABEL_SIZE)
            if e.get('x') is not None:
                e.set('x', f'{x_new:.5f}')
        done += 1
    tree.write(path, encoding='utf-8', xml_declaration=True)
    return done


def centre(query_dir):
    """Move each label so its box is centred on its control's."""
    pages = 0
    for name in PAGES:
        q = os.path.join(query_dir, name + '.query')
        if os.path.exists(q):
            pages += centre_page(os.path.join(GFX, name), read_query(q))
    print(f'restyle_panel: centred {pages} slider captions on the DIVE pages')

    boxes = read_query(os.path.join(query_dir, os.path.basename(PANEL) + '.query'))
    tree = ET.parse(PANEL)
    root = tree.getroot()
    by_label = {label(e): e for e in root.iter() if label(e)}

    # --query-all answers in px; the file is in user units.  Measure the ratio on a
    # rectangle whose size is written down, rather than assume a DPI.
    probe = by_label['BUT_tone']
    k = boxes[probe.get('id')][2] / float(probe.get('width'))

    moved = 0
    for lab, ctrl in list(CENTRE_ON.items()) + list(CENTRE_IN.items()):
        t, c = by_label.get(lab), by_label.get(ctrl)
        if t is None or c is None or t.get('id') not in boxes or c.get('id') not in boxes:
            sys.stderr.write(f'restyle_panel: cannot centre {lab} on {ctrl}\n')
            continue
        tx, ty, tw, th = boxes[t.get('id')]
        cx, cy, cw, ch = boxes[c.get('id')]
        dx = ((cx + cw / 2) - (tx + tw / 2)) / k
        dy = ((cy + ch / 2) - (ty + th / 2)) / k if lab in CENTRE_IN else 0.0
        for e in [t] + list(t.iter(f'{{{SVG}}}tspan')):
            if e.get('x') is not None:
                e.set('x', f'{float(e.get("x")) + dx:.5f}')
            if e.get('y') is not None:
                e.set('y', f'{float(e.get("y")) + dy:.5f}')
        moved += 1

    tree.write(PANEL, encoding='utf-8', xml_declaration=True)
    print(f'restyle_panel: centred {moved} labels')


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == 'style':
        style_panel()
        style_pages()
        print('restyle_panel: styled the panel and %d DIVE pages' % len(PAGES))
    elif len(sys.argv) == 3 and sys.argv[1] == 'centre':
        centre(sys.argv[2])
    else:
        sys.stderr.write(__doc__)
        sys.exit(2)


if __name__ == '__main__':
    main()
