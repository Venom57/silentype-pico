#!/usr/bin/env python3
"""Generate carrier-board schematic sections with schemdraw; stack into one SVG."""
import re, html
import schemdraw
import schemdraw.elements as elm

schemdraw.config(font='sans-serif', fontsize=11)
SVGS = []
def save(d, title):
    fn = f"/tmp/claude-0/-home-claude/73eda34b-f70d-5688-9f44-1a570876f4ef/scratchpad/sec_{len(SVGS)}.svg"
    d.save(fn); SVGS.append((fn, title))

# 1 Power
with schemdraw.Drawing(show=False) as d:
    d.config(unit=2.2)
    d += elm.Dot(open=True).label('J3-1\n+5V IN', 'left')
    d += elm.Fuse().right().label('F2\n500mA', 'top')
    n5 = d.add(elm.Dot()); d += elm.Line().right().length(1.2)
    d += elm.Dot(open=True).label('+5V rail\n(U2/U6 VCC, U3/U4 VCC,\nPico VSYS)', 'right')
    d += elm.Capacitor().at(n5.start).down().label('C8\n100µF', 'right'); d += elm.Ground()
    d.move_from(n5.start, dx=-3.4, dy=-4.2)
    d += elm.Dot(open=True).label('J3-3\nVMOT IN (TBD V)', 'left')
    d += elm.Fuse().right().label('F1\n2A poly', 'top')
    nm = d.add(elm.Dot()); d += elm.Line().right().length(1.2)
    d += elm.Dot(open=True).label('VMOT rail\n(head + motor supply)', 'right')
    d += elm.Capacitor().at(nm.start).down().label('C7\n470µF', 'right')
    d += elm.Ground().label('star GND\n@ power entry', 'bottom', fontsize=9)
save(d, "1 — POWER ENTRY  (bench supply during bring-up; current-limit 200 mA at first power)")

# 2 Monostable head gate
with schemdraw.Drawing(show=False) as d:
    d.config(unit=1.6)
    ic = d.add(elm.Ic(pins=[
        elm.IcPin(name='1B', side='left'), elm.IcPin(name='1A̅', side='left'), elm.IcPin(name='1CLR̅', side='left'),
        elm.IcPin(name='1Q̅', side='right'), elm.IcPin(name='1Q', side='right'),
        elm.IcPin(name='Rx/Cx', side='top'), elm.IcPin(name='Cx', side='top'), elm.IcPin(name='VCC', side='top'),
        elm.IcPin(name='GND', side='bot'),
    ], edgepadW=1.6, edgepadH=.7, pinspacing=1.3, leadlen=.8, label='U5\n74HC123 (½)'))
    d += elm.Line().at(getattr(ic, '1B')).left().length(.6)
    d += elm.Dot(open=True).label('Pico GP9\nHEAD_STROBE ↑', 'left')
    d += elm.Ground().at(getattr(ic, '1A̅'))
    d += elm.Line().at(getattr(ic, '1CLR̅')).left().length(.4); d += elm.Dot(open=True).label('3V3', 'left')
    d += elm.Line().at(getattr(ic, '1Q̅')).right().length(1.0)
    q = d.add(elm.Dot()); d += elm.Line().right().length(.6)
    d += elm.Dot(open=True).label('→ U2 OE̅\n(low = head enabled, ~1.5 ms)', 'right')
    d += elm.Resistor().at(q.start).up().label('R30\n10k', 'left'); d += elm.Dot(open=True).label('3V3', 'top')
    d += elm.Line().at(ic.VCC).up().length(.6); d += elm.Dot(open=True).label('3V3', 'top')
    d += elm.Ground().at(ic.GND)
    rc = getattr(ic, 'Rx/Cx')
    d += elm.Line().at(rc).up().length(.8)
    j = d.add(elm.Dot())
    d += elm.Resistor().up().label('R31 33k', 'left'); d += elm.Dot(open=True).label('3V3', 'top')
    d += elm.Line().at(j.start).right().tox(ic.Cx)
    d += elm.Capacitor().down().toy(ic.Cx).label('C10 100nF', 'right')
save(d, "2 — HEAD ENABLE ONE-SHOT  (hardware burn ceiling ≈ 1.5 ms; U5 at 3V3 so GP9 triggers it directly; firmware re-strobes per column)")

# 3 Head level shift + driver
with schemdraw.Drawing(show=False) as d:
    d.config(unit=1.6)
    ic = d.add(elm.Ic(pins=[
        elm.IcPin(name='A1', side='left'), elm.IcPin(name='…', side='left'), elm.IcPin(name='A7', side='left'),
        elm.IcPin(name='B1', side='right'), elm.IcPin(name='…', side='right'), elm.IcPin(name='B7', side='right'),
        elm.IcPin(name='DIR', side='bot'), elm.IcPin(name='OE̅', side='bot'),
        elm.IcPin(name='VCC', side='top'), elm.IcPin(name='GND', side='bot'),
    ], edgepadW=1.2, edgepadH=.6, pinspacing=1.1, leadlen=.8, label='U2\n74AHCT245'))
    d += elm.Line().at(ic.A1).left().length(.6); d += elm.Dot(open=True).label('Pico GP2…GP8\nHEAD_D1…D7 (count TBD)', 'left')
    d += elm.Line().at(ic.VCC).up().length(.5); d += elm.Dot(open=True).label('+5V', 'top')
    d += elm.Ground().at(ic.GND)
    d += elm.Line().at(ic.DIR).down().length(.7); d += elm.Dot(open=True).label('+5V (A→B)', 'bottom')
    d += elm.Line().at(getattr(ic, 'OE̅')).down().length(1.3); d += elm.Dot(open=True).label('← U5 1Q̅', 'bottom')
    d += elm.Line().at(ic.B1).right().length(1.4)
    n = d.add(elm.Dot())
    d += elm.Resistor().at(n.start).down().label('R8\n10k', 'left'); d += elm.Ground().label('×7 MANDATORY', 'bottom', fontsize=8.5)
    d += elm.Line().at(n.start).right().length(1.0)
    d += elm.Resistor().right().label('R1 100Ω', 'top', fontsize=9)
    d += elm.Line().right().length(1.0)
    fet = d.add(elm.NFet(bulk=False).anchor('gate').label('Q1\nAO3400', 'right', fontsize=9, ofst=.5))
    d += elm.Line().at(fet.source).down().length(.6)
    d += elm.Dot(open=True).label('→ R40 shunt (§5)', 'right', fontsize=9)
    d += elm.Line().at(fet.drain).up().length(1.2)
    d += elm.Dot(open=True).label('J1 TBD → head element 1\n(element return: UNVERIFIED)', 'right', fontsize=9)
save(d, "3 — HEAD LEVEL SHIFT + DRIVER  (B1 channel; ×HEAD_DOTS. Option B parts R1/Q1 shown — Option A sends U2 B1 straight to J1)")

# 4 Motor level shift + ULN
with schemdraw.Drawing(show=False) as d:
    d.config(unit=1.5)
    u6 = d.add(elm.Ic(pins=[
        elm.IcPin(name='A1', side='left'), elm.IcPin(name='…', side='left'), elm.IcPin(name='A8', side='left'),
        elm.IcPin(name='B1', side='right'), elm.IcPin(name='…', side='right'), elm.IcPin(name='B8', side='right'),
        elm.IcPin(name='OE̅', side='bot'), elm.IcPin(name='VCC', side='top'),
    ], edgepadW=1.0, edgepadH=.5, pinspacing=1.0, leadlen=.7, label='U6\n74AHCT245'))
    d += elm.Line().at(u6.A1).left().length(.5); d += elm.Dot(open=True).label('GP10–13 CARR\nGP14–17 FEED', 'left')
    d += elm.Line().at(u6.VCC).up().length(.4); d += elm.Dot(open=True).label('+5V', 'top')
    d += elm.Ground().at(getattr(u6, 'OE̅'))
    d += elm.Line().at(u6.B1).right().length(1.0)
    u3 = d.add(elm.Ic(pins=[
        elm.IcPin(name='1B', side='left'), elm.IcPin(name='…', side='left'), elm.IcPin(name='4B', side='left'),
        elm.IcPin(name='1C', side='right'), elm.IcPin(name='…', side='right'), elm.IcPin(name='4C', side='right'),
        elm.IcPin(name='COM', side='top'), elm.IcPin(name='GND', side='bot'),
    ], edgepadW=1.0, edgepadH=.5, pinspacing=1.0, leadlen=.7, label='U3 (U4)\nULN2003A').anchor('1B'))
    d += elm.Line().at(u3.COM).up().length(.4); d += elm.Dot(open=True).label('VMOT (clamps)', 'top')
    d += elm.Ground().at(u3.GND)
    d += elm.Line().at(getattr(u3, '1C')).right().length(.6); d += elm.Dot(open=True).label('J1 TBD — φA…φD', 'right')
save(d, "4 — STEPPER DRIVE  (5 V logic into ULN via U6 — ULN2003A inputs are marginal at 3.3 V)")

# 5 Sensor + shunt
with schemdraw.Drawing(show=False) as d:
    d.config(unit=1.8)
    d += elm.Dot(open=True).label('J1 TBD\nHOME sensor (5V)', 'left')
    d += elm.Resistor().right().label('R20\n10k', 'top')
    t = d.add(elm.Dot()); d += elm.Line().right().length(1.0)
    d += elm.Dot(open=True).label('Pico GP20\n(3.0V max)', 'right')
    d += elm.Resistor().at(t.start).down().label('R21\n15k', 'left'); d += elm.Ground()
    d.move_from(t.start, dx=-1.8, dy=-5.2)
    d += elm.Dot(open=True).label('Head element return\n(Q1–Q7 sources, Option B)', 'left')
    h = d.add(elm.Line().right().length(.8))
    hs = d.add(elm.Dot())
    d += elm.Resistor().right().label('R40\n0.1Ω 1W', 'bottom')
    d += elm.Ground()
    d += elm.Resistor().at(hs.start).up().length(3.4).label('R41 1k', 'left')
    f = d.add(elm.Dot()); d += elm.Line().right().length(1.2)
    g = d.add(elm.Dot()); d += elm.Line().right().length(.6)
    d += elm.Dot(open=True).label('Pico GP27 / ADC1\nISENSE (1 A → 100 mV)', 'right')
    d += elm.Capacitor().at(g.start).down().length(1.6).label('C11\n10nF', 'left'); d += elm.Ground()
save(d, "5 — SENSOR INPUT (×2) and HEAD CURRENT SHUNT  (shunt is Option B only — impossible in Option A)")

# compose
parts, y, maxw = [], 40, 0
PAD, TITLE_H = 30, 26
for fn, title in SVGS:
    src = open(fn).read()
    m = re.search(r'<svg[^>]*width="([\d.]+)(?:pt|px)?"[^>]*height="([\d.]+)(?:pt|px)?"', src)
    w, h = float(m.group(1)) * 1.25, float(m.group(2)) * 1.25
    inner = re.sub(r'^.*?<svg[^>]*>', '', src, flags=re.S); inner = re.sub(r'</svg>\s*$', '', inner, flags=re.S)
    parts.append(f'<text x="20" y="{y+18}" font-family="sans-serif" font-size="15" font-weight="bold" fill="#1a1a1a">{html.escape(title)}</text>')
    parts.append(f'<g transform="translate(30,{y+TITLE_H}) scale(1.25)">{inner}</g>')
    y += TITLE_H + h + PAD; maxw = max(maxw, w + 60, 1100)
    parts.append(f'<line x1="10" y1="{y-PAD/2}" x2="{maxw-10}" y2="{y-PAD/2}" stroke="#cccccc"/>')
hdr = ('<text x="20" y="0" font-family="sans-serif" font-size="19" font-weight="bold">silentype-pico — Carrier Board Schematic (v0.3)</text>'
       '<text x="20" y="22" font-family="sans-serif" font-size="12" fill="#555">All J1 pin numbers TBD pending docs/signals.md. Head element count unverified. Option A/B decided after Phase 0. USB is CDC serial.</text>')
out = (f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="{int(maxw)}" height="{int(y+60)}" viewBox="0 -40 {int(maxw)} {int(y+60)}">'
       f'<rect x="-10" y="-40" width="110%" height="110%" fill="#ffffff"/>{hdr}' + ''.join(parts) + '</svg>')
open('carrier-schematic.svg', 'w').write(out)
print("wrote carrier-schematic.svg", int(maxw), int(y))
