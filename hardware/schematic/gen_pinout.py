#!/usr/bin/env python3
"""Pinout & connection diagram: Pico 2 GPIO -> driver stages -> J1 26-pin -> Silentype."""
import html

W, H = 1080, 1180
COL = dict(head='#d62828', strobe='#e07b00', carr='#1d6fd6', feed='#0e9594', sens='#2a9d2a',
           isense='#7a5c00', dbg='#888888', misc='#9b5de5')

rows = [
    ('GP0  UART0 TX',  'dbg',   'debug header', None),
    ('GP1  UART0 RX',  'dbg',   'debug header', None),
    ('GP2  HEAD_D1',   'head',  None, 'head element 1'),
    ('GP3  HEAD_D2',   'head',  None, 'head element 2'),
    ('GP4  HEAD_D3',   'head',  None, 'head element 3'),
    ('GP5  HEAD_D4',   'head',  None, 'head element 4'),
    ('GP6  HEAD_D5',   'head',  None, 'head element 5'),
    ('GP7  HEAD_D6',   'head',  None, 'head element 6'),
    ('GP8  HEAD_D7',   'head',  None, 'head element 7 (count?)'),
    ('GP9  HEAD_STROBE','strobe', None, None),
    ('GP10 CARR_PHA',  'carr',  None, 'carriage φA'),
    ('GP11 CARR_PHB',  'carr',  None, 'carriage φB'),
    ('GP12 CARR_PHC',  'carr',  None, 'carriage φC'),
    ('GP13 CARR_PHD',  'carr',  None, 'carriage φD'),
    ('GP14 FEED_PHA',  'feed',  None, 'paper feed φA*'),
    ('GP15 FEED_PHB',  'feed',  None, 'paper feed φB*'),
    ('GP16 FEED_PHC',  'feed',  None, 'paper feed φC*'),
    ('GP17 FEED_PHD',  'feed',  None, 'paper feed φD*'),
    ('GP20 HOME_SENSE','sens',  None, 'carriage home'),
    ('GP21 PAPER_SENSE','sens', None, 'paper-out*'),
    ('GP22 BTN_FEED',  'misc',  'tactile SW → GND', None),
    ('GP26 LED_ERR',   'misc',  'LED + 1k → GND', None),
    ('GP27 ISENSE/ADC1','isense', None, None),
]
TOP, RH = 118, 28
pico_x, pico_w = 70, 230
stage_x, stage_w = 390, 270
j1_x = 770

svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="sans-serif">',
       f'<rect width="{W}" height="{H}" fill="#ffffff"/>',
       '<text x="24" y="34" font-size="20" font-weight="bold">silentype-pico — Pinout &amp; Connection Diagram (v0.3)</text>',
       '<text x="24" y="56" font-size="12" fill="#555">J1 pin numbers TBD until docs/signals.md is continuity-verified — signal CLASSES are fixed, pin ORDER is not. Head element count (7) is unverified.</text>',
       '<text x="24" y="72" font-size="12" fill="#555">* populate only if a separate paper-feed motor / paper-out sensor exists (Phase 0 Decision 2). USB enumerates as CDC serial.</text>']

n = len(rows); pico_h = n * RH + 60
svg.append(f'<rect x="{pico_x}" y="{TOP}" width="{pico_w}" height="{pico_h}" rx="10" fill="#206040" stroke="#0c3320" stroke-width="2"/>')
svg.append(f'<text x="{pico_x+pico_w/2}" y="{TOP+26}" font-size="15" font-weight="bold" fill="#fff" text-anchor="middle">Raspberry Pi Pico 2</text>')
svg.append(f'<text x="{pico_x+pico_w/2}" y="{TOP+43}" font-size="11" fill="#cde" text-anchor="middle">USB CDC serial → host (PAPPL)</text>')

def grp(cls):
    idx = [i for i, r in enumerate(rows) if r[1] == cls]; return idx[0], idx[-1]
stages = [
    ('head',  'U2 74AHCT245 (OE̅ ← U5)\n→ R8–R14 10k pulldowns\n→ Option B: R1–R7 + Q1–Q7 FETs\n→ Option A: straight to J1'),
    ('strobe','U5 74HC123 one-shot @3V3 → U2 OE̅ (≈1.5 ms)'),
    ('carr',  'U6 74AHCT245 → U3 ULN2003A\nCOM → VMOT'),
    ('feed',  'U6 74AHCT245 → U4 ULN2003A *\nCOM → VMOT'),
    ('sens',  '10k/15k dividers (5 V → 3 V)'),
    ('isense','R40 0.1Ω shunt + RC filter (Option B only)'),
]
boxes = {}
for cls, label in stages:
    a, b = grp(cls)
    y0 = TOP + 55 + a * RH - 11; y1 = TOP + 55 + b * RH + 11
    boxes[cls] = (y0, y1)
    svg.append(f'<rect x="{stage_x}" y="{y0}" width="{stage_w}" height="{y1-y0}" rx="6" fill="{COL[cls]}" fill-opacity="0.07" stroke="{COL[cls]}" stroke-width="1.5"/>')
    lines = label.split('\n'); fs = 10.5 if len(lines) > 2 else (10 if len(lines)==1 else 11.5)
    for j, line in enumerate(lines):
        svg.append(f'<text x="{stage_x+stage_w/2}" y="{(y0+y1)/2 + (j-(len(lines)-1)/2)*(fs+2) + 4}" font-size="{fs}" fill="{COL[cls]}" text-anchor="middle" font-weight="bold">{html.escape(line)}</text>')

for i, (pin, cls, stage_txt, j1_txt) in enumerate(rows):
    y = TOP + 55 + i * RH; c = COL[cls]
    svg.append(f'<rect x="{pico_x+pico_w-8}" y="{y-5}" width="16" height="10" fill="#d4af37" stroke="#806000"/>')
    svg.append(f'<text x="{pico_x+pico_w-16}" y="{y+4}" font-size="12" fill="#fff" text-anchor="end" font-family="monospace">{html.escape(pin)}</text>')
    if cls in boxes:
        svg.append(f'<line x1="{pico_x+pico_w+8}" y1="{y}" x2="{stage_x}" y2="{y}" stroke="{c}" stroke-width="2"/>')
        if j1_txt:
            svg.append(f'<line x1="{stage_x+stage_w}" y1="{y}" x2="{j1_x}" y2="{y}" stroke="{c}" stroke-width="2"/>')
            svg.append(f'<circle cx="{j1_x+14}" cy="{y}" r="6" fill="none" stroke="{c}" stroke-width="2"/>')
            svg.append(f'<text x="{j1_x+14}" y="{y+3}" font-size="8" fill="{c}" text-anchor="middle">?</text>')
            svg.append(f'<text x="{j1_x+34}" y="{y+4}" font-size="12" fill="{c}">{html.escape(j1_txt)}</text>')
        else:
            txt = {'strobe': 'internal — gates U2, not a J1 signal', 'isense': 'internal — head return current'}[cls]
            svg.append(f'<text x="{stage_x+stage_w+12}" y="{y+4}" font-size="11" fill="{c}" font-style="italic">{html.escape(txt)}</text>')
    else:
        svg.append(f'<line x1="{pico_x+pico_w+8}" y1="{y}" x2="{stage_x-30}" y2="{y}" stroke="{c}" stroke-width="2" stroke-dasharray="5,4"/>')
        svg.append(f'<text x="{stage_x-24}" y="{y+4}" font-size="11.5" fill="{c}">{html.escape(stage_txt)}</text>')

# J1
j1_top = TOP + 30; j1_h = 13 * 34 + 50
svg.append(f'<rect x="{j1_x+190}" y="{j1_top}" width="100" height="{j1_h}" rx="8" fill="#333" stroke="#000" stroke-width="2"/>')
svg.append(f'<text x="{j1_x+240}" y="{j1_top-10}" font-size="14" font-weight="bold" text-anchor="middle">J1 — 2×13 IDC</text>')
for r in range(13):
    for ci, xo in enumerate((28, 72)):
        pn = r*2+ci+1; cy = j1_top + 36 + r*34
        svg.append(f'<circle cx="{j1_x+190+xo}" cy="{cy}" r="9" fill="#bbb" stroke="#666"/>')
        svg.append(f'<text x="{j1_x+190+xo}" y="{cy+4}" font-size="9.5" text-anchor="middle">{pn}</text>')
svg.append(f'<text x="{j1_x+240}" y="{j1_top+j1_h+20}" font-size="11.5" text-anchor="middle" fill="#555">ribbon → Silentype</text>')
svg.append(f'<text x="{j1_x+240}" y="{j1_top+j1_h+36}" font-size="11.5" text-anchor="middle" fill="#b00">pin map: docs/signals.md</text>')

# power + legend
py = TOP + pico_h + 45
svg.append(f'<text x="{pico_x}" y="{py}" font-size="14" font-weight="bold">Power</text>')
for j, line in enumerate([
    'J3-1  +5V  ──F2 500mA──►  Pico VSYS, U2/U6 VCC, U3/U4 VCC, sensor pull-ups',
    '3V3 (Pico out) ──►  U5 VCC, R30/R31 pull-ups   (U5 at 3V3 so GP9 can trigger it)',
    'J3-3  VMOT (voltage TBD)  ──F1 2A──►  head elements + ULN COM   ── C7 470µF bulk',
    'GND   star point at power entry; logic GND and VMOT return meet only there',
    'J1 power pins (count/order TBD): +5V, VMOT, multiple GND',
]):
    svg.append(f'<text x="{pico_x}" y="{py+20+j*19}" font-size="12.5" font-family="monospace">{html.escape(line)}</text>')
ly = py + 128
svg.append(f'<text x="{pico_x}" y="{ly}" font-size="14" font-weight="bold">Signal classes</text>')
leg = [('head','Thermal head elements — count TBD; 10k pulldowns mandatory'), ('strobe','Head strobe → one-shot (hardware burn ceiling)'),
       ('carr','Carriage stepper ×4'), ('feed','Paper feed ×4 (if fitted)'), ('sens','Sensors (inputs, divided)'),
       ('isense','Head current sense (ADC)'), ('dbg','Debug UART'), ('misc','Local UI')]
for j, (cls, txt) in enumerate(leg):
    yy = ly + 20 + j*19
    svg.append(f'<rect x="{pico_x}" y="{yy-10}" width="26" height="12" fill="{COL[cls]}"/>')
    svg.append(f'<text x="{pico_x+34}" y="{yy}" font-size="12.5">{html.escape(txt)}</text>')
svg.append('</svg>')
open('pinout-connection.svg', 'w').write('\n'.join(svg))
print('wrote pinout-connection.svg')
