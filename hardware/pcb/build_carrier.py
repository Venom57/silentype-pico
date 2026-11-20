#!/usr/bin/env python3
"""build_carrier.py — generates the Silentype-Pico carrier PCB (KiCad 10) from code.

    python3 build_carrier.py place   # parts, nets, rules, outline, keepouts -> silentype-carrier.kicad_pcb + .dsn
    python3 build_carrier.py finish  # import Freerouting .ses, pour GND, silkscreen -> silentype-carrier.kicad_pcb

The Makefile runs the whole pipeline (place -> freerouting -> finish -> DRC -> Gerbers).
This script is the single source of truth for parts, nets and placement; see README.md for
the circuit. Coordinates are mm, origin top-left of the board, +y down.
"""
import os
import sys
import pcbnew

FP = "/usr/share/kicad/footprints/"
HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.join(HERE, "silentype-carrier.kicad_pcb")
DSN = os.path.join(HERE, "build", "silentype-carrier.dsn")
SES = os.path.join(HERE, "build", "silentype-carrier.ses")

W, H = 100.0, 56.0            # board outline
PICO_X = W - 21.0 + 1.61      # Pico pin-1 pad (body flush with the right edge)
PICO_Y = 1.37                 # USB end flush with the top edge; antenna end at y = 51
EDGE_CLEARANCE = 0.5

mm = pcbnew.FromMM
def P(x, y): return pcbnew.VECTOR2I(mm(x), mm(y))

# ---------------------------------------------------------------------------------------
# Bill of materials + placement.  ref: (lib, footprint, x, y, rot, value, MPN, {pad: net})
# ---------------------------------------------------------------------------------------
PICO_NETS = {"1": "UART_TX", "2": "UART_RX", "4": "SD_3V", "5": "SC_3V", "6": "XC_3V",
             "7": "MS_IN", "9": "BUS_EN", "29": "BTN_FEED", "31": "LED_ERR",
             "36": "+3V3", "39": "VSYS"}
PICO_NETS.update({str(g): "GND" for g in (3, 8, 13, 18, 23, 28, 33, 38)})

R_AX = ("Resistor_THT", "R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal")
PARTS = {
    # --- connectors -------------------------------------------------------------------
    "U1": ("Module", "RaspberryPi_Pico_Common_THT", PICO_X, PICO_Y, 0,
           "Pico 2 / Pico 2 W", "Raspberry Pi Pico 2 or Pico 2 W, on 2x 1x20 sockets", PICO_NETS),
    "J1": ("Connector_Dsub",
           "DSUB-9_Socket_Horizontal_P2.77x2.84mm_EdgePinOffset7.70mm_Housed_MountingHolesOffset9.12mm",
           10.54, 41.0, 270, "DE-9 female (printer)", "DE-9 socket (female), right-angle PCB, 2.77 x 2.84 mm pitch, 9.12 mm hole-to-edge",
           {"1": "GND", "2": "+5V_PRN", "3": "GND", "4": "SD_5V", "5": "MS_5V", "6": "+12V",
            "7": "GND", "8": "XC_5V", "9": "SC_5V", "SH": "GND"}),
    "J2": ("Connector_BarrelJack", "BarrelJack_CUI_PJ-102AH_Horizontal", 13.7, 8.0, 270,
           "12V DC in", "CUI PJ-102AH", {"1": "+12V_IN", "2": "GND", "3": "GND"}),
    "J3": ("Connector_PinHeader_2.54mm", "PinHeader_1x03_P2.54mm_Vertical", 64.0, 2.2, 90,
           "UART", "1x3 2.54 mm header", {"1": "GND", "2": "UART_RX", "3": "UART_TX"}),
    # --- power ------------------------------------------------------------------------
    "F1": ("Fuse", "Fuse_Bourns_MF-RHT200", 19.0, 3.0, 0, "PTC 2A", "Bourns MF-RHT200",
           {"1": "+12V_IN", "2": "+12V"}),
    "D1": ("Diode_THT", "D_DO-201AD_P15.24mm_Horizontal", 18.0, 14.0, 0, "1N5822",
           "1N5822 (reverse-polarity crowbar)", {"1": "+12V", "2": "GND"}),
    "C2": ("Capacitor_THT", "CP_Radial_D5.0mm_P2.00mm", 21.5, 8.2, 0, "10uF 25V",
           "electrolytic 10uF/25V", {"1": "+12V", "2": "GND"}),
    "U3": ("Converter_DCDC", "Converter_DCDC_RECOM_R-78E-0.5_THT", 31.0, 7.5, 0,
           "R-78E5.0-1.0", "RECOM R-78E5.0-1.0", {"1": "+12V", "2": "GND", "3": "+5V"}),
    "C3": ("Capacitor_THT", "CP_Radial_D5.0mm_P2.00mm", 42.0, 7.5, 0, "22uF 10V",
           "electrolytic 22uF/10V", {"1": "+5V", "2": "GND"}),
    "C1": ("Capacitor_THT", "CP_Radial_D10.0mm_P5.00mm", 17.5, 47.0, 0, "470uF 25V",
           "electrolytic 470uF/25V low-ESR", {"1": "+12V", "2": "GND"}),
    "F2": ("Fuse", "Fuse_Bourns_MF-RHT050", 25.0, 40.0, 180, "PTC 0.5A", "Bourns MF-RHT050",
           {"1": "+5V", "2": "+5V_PRN"}),
    "D2": ("Diode_THT", "D_DO-41_SOD81_P10.16mm_Horizontal", 70.0, 6.5, 180, "1N5817",
           "1N5817 (5V -> Pico VSYS)", {"1": "VSYS", "2": "+5V"}),
    "R13": R_AX + (41.0, 2.6, 0, "1k", "1k 1/4W", {"1": "+5V", "2": "LED_PWR"}),
    "LED2": ("LED_THT", "LED_D3.0mm", 53.5, 2.6, 180, "PWR green", "3 mm green LED",
             {"1": "GND", "2": "LED_PWR"}),
    # --- bus: 3.3 V -> 5 V buffer with hardware enable ----------------------------------
    "U2": ("Package_DIP", "DIP-20_W7.62mm", 64.0, 40.0, 180, "74AHCT541", "SN74AHCT541N",
           {"1": "OE_N", "2": "XC_3V", "3": "SD_3V", "4": "SC_3V",
            "5": "GND", "6": "GND", "7": "GND", "8": "GND", "9": "GND", "10": "GND",
            "16": "SC_B", "17": "SD_B", "18": "XC_B", "19": "OE_N", "20": "+5V"}),
    "C4": ("Capacitor_THT", "C_Disc_D5.0mm_W2.5mm_P5.00mm", 50.5, 44.5, 0, "100nF",
           "ceramic 100nF", {"1": "+5V", "2": "GND"}),
    "R1": R_AX + (44.0, 31.9, 0, "47R", "47R 1/4W", {"1": "SD_5V", "2": "SD_B"}),
    "R2": R_AX + (44.0, 28.6, 0, "47R", "47R 1/4W", {"1": "SC_5V", "2": "SC_B"}),
    "R3": R_AX + (44.0, 35.2, 0, "47R", "47R 1/4W", {"1": "XC_5V", "2": "XC_B"}),
    "R4": R_AX + (16.0, 32.5, 0, "4k7", "4.7k 1/4W", {"1": "SC_5V", "2": "+5V_PRN"}),
    "R5": R_AX + (27.0, 29.0, 0, "4k7", "4.7k 1/4W", {"1": "XC_5V", "2": "+5V_PRN"}),
    "R6": R_AX + (16.0, 36.0, 0, "10k", "10k 1/4W", {"1": "SD_5V", "2": "GND"}),
    "R7": R_AX + (40.0, 45.0, 0, "10k", "10k 1/4W", {"1": "+5V", "2": "OE_N"}),
    "Q1": ("Package_TO_SOT_THT", "TO-92_Inline_Wide", 58.5, 45.5, 0, "2N3904", "2N3904 (E-B-C)",
           {"1": "GND", "2": "Q1_B", "3": "OE_N"}),
    "R8": R_AX + (40.0, 53.0, 0, "4k7", "4.7k 1/4W", {"1": "Q1_B", "2": "BUS_EN"}),
    "R9": R_AX + (40.0, 49.0, 0, "47k", "47k 1/4W", {"1": "GND", "2": "Q1_B"}),
    # --- margin-switch input -----------------------------------------------------------
    "R10": R_AX + (16.0, 29.0, 0, "1k", "1k 1/4W", {"1": "MS_5V", "2": "MS_IN"}),
    "R11": R_AX + (27.0, 32.5, 0, "10k", "10k 1/4W", {"1": "MS_IN", "2": "+3V3"}),
    "D3": ("Diode_THT", "D_DO-35_SOD27_P7.62mm_Horizontal", 27.0, 36.0, 0, "BAT85",
           "BAT85 (clamp MS_IN to 3V3)", {"1": "+3V3", "2": "MS_IN"}),
    # --- user interface ----------------------------------------------------------------
    "SW1": ("Button_Switch_THT", "SW_PUSH_6mm", 66.5, 47.5, 0, "FEED", "6x6 mm tactile",
            {"1": "BTN_FEED", "2": "GND"}),
    "R12": R_AX + (50.0, 49.8, 0, "330R", "330R 1/4W", {"1": "LED_ERR", "2": "LED_ERR_A"}),
    "LED1": ("LED_THT", "LED_D3.0mm", 52.0, 53.7, 0, "ERR red", "3 mm red LED",
             {"1": "GND", "2": "LED_ERR_A"}),
    # --- test points -------------------------------------------------------------------
    "TP1": ("TestPoint", "TestPoint_Keystone_5000-5004_Miniature", 16.0, 24.5, 0, "SD", "Keystone 5000", {"1": "SD_5V"}),
    "TP2": ("TestPoint", "TestPoint_Keystone_5000-5004_Miniature", 19.5, 24.5, 0, "SC", "Keystone 5000", {"1": "SC_5V"}),
    "TP3": ("TestPoint", "TestPoint_Keystone_5000-5004_Miniature", 23.0, 24.5, 0, "XC", "Keystone 5000", {"1": "XC_5V"}),
    "TP4": ("TestPoint", "TestPoint_Keystone_5000-5004_Miniature", 26.5, 24.5, 0, "MS", "Keystone 5000", {"1": "MS_5V"}),
    "TP5": ("TestPoint", "TestPoint_Keystone_5000-5004_Miniature", 30.0, 24.5, 0, "+12V", "Keystone 5000", {"1": "+12V"}),
    "TP6": ("TestPoint", "TestPoint_Keystone_5000-5004_Miniature", 33.5, 24.5, 0, "+5V", "Keystone 5000", {"1": "+5V"}),
    "TP7": ("TestPoint", "TestPoint_Keystone_5000-5004_Miniature", 37.0, 24.5, 0, "GND", "Keystone 5001", {"1": "GND"}),
    # --- mounting ----------------------------------------------------------------------
    "H1": ("MountingHole", "MountingHole_3.2mm_M3", 44.0, 18.0, 0, "M3", "M3 standoff", {}),
    "H2": ("MountingHole", "MountingHole_3.2mm_M3", 71.5, 25.0, 0, "M3", "M3 standoff", {}),
    "H3": ("MountingHole", "MountingHole_3.2mm_M3", 71.5, 39.0, 0, "M3", "M3 standoff", {}),
    "H4": ("MountingHole", "MountingHole_3.2mm_M3", 31.0, 52.0, 0, "M3", "M3 standoff", {}),
}

# 3D models for the renders only (never affect Gerbers). KiCad's library has no models for the
# CUI PJ-102AH jack or the Bourns MF-RHT PTCs, so look-alikes stand in, aligned to these pads.
#   ref: [(model path under ${KICAD10_3DMODEL_DIR}, rot z deg, offset (x, y, z) mm)]
M3D = "${KICAD10_3DMODEL_DIR}/"
RENDER_MODELS = {
    "J2": [(M3D + "Connector_BarrelJack.3dshapes/BarrelJack_Horizontal.step", -90, (0, 0, 0))],
    "F1": [(M3D + "Capacitor_THT.3dshapes/C_Disc_D7.5mm_W2.5mm_P5.00mm.step", 13.2, (0, 0, 0))],
    "F2": [(M3D + "Capacitor_THT.3dshapes/C_Disc_D7.5mm_W2.5mm_P5.00mm.step", 13.2, (0, 0, 0))],
    # KiCad 10.0.7 on aarch64 crashes (SIGBUS/SIGSEGV) loading most CP_Radial/C_Radial STEP
    # files, including CP_Radial_D10.0mm_P5.00mm and every 4-6.3 mm can. The D10 dual-pitch
    # model loads, so it stands in for C1 and, scaled to a 5 mm can, for C2/C3.
    "C1": [(M3D + "Capacitor_THT.3dshapes/CP_Radial_D10.0mm_P5.00mm_P7.50mm.step", 0, (0, 0, 0))],
    "C2": [(M3D + "Capacitor_THT.3dshapes/CP_Radial_D10.0mm_P5.00mm_P7.50mm.step", 0, (0, 0, 0), (0.5, 0.5, 0.6))],
    "C3": [(M3D + "Capacitor_THT.3dshapes/CP_Radial_D10.0mm_P5.00mm_P7.50mm.step", 0, (0, 0, 0), (0.5, 0.5, 0.6))],
}


# The Pico footprint ships several models (with their own offsets); show the 8.5 mm sockets
# and the Pico-with-headers, lifted onto the sockets.
PICO_SHOW = {"RaspberryPi_Pico_PinSockets_H8.50mm.step": 0.0, "RaspberryPi_Pico_H.step": 8.5}


def set_pico_models(fp):
    for m in fp.Models():
        name = m.m_Filename.split("/")[-1]
        m.m_Show = name in PICO_SHOW
        if name in PICO_SHOW:
            m.m_Offset = pcbnew.VECTOR3D(m.m_Offset.x, m.m_Offset.y, m.m_Offset.z + PICO_SHOW[name])


def set_render_models(fp, models):
    fp.Models().clear()
    for path, rotz, (ox, oy, oz), *scale in models:
        m = pcbnew.FP_3DMODEL()
        if scale:
            m.m_Scale = pcbnew.VECTOR3D(*scale[0])
        m.m_Filename = path
        m.m_Show = True
        m.m_Rotation = pcbnew.VECTOR3D(0, 0, rotz)
        m.m_Offset = pcbnew.VECTOR3D(ox, oy, oz)
        fp.Models().append(m)


# Net classes: (track width, clearance) in mm. Everything else uses Default.
NETCLASSES = {
    "Power12": (1.2, 0.3, ["+12V_IN", "+12V"]),
    "Power5":  (0.6, 0.25, ["+5V", "+5V_PRN", "GND"]),   # VSYS stays 0.3 mm: it must pass between Pico header pins
}
DEFAULT_TRACK, DEFAULT_CLEARANCE = 0.3, 0.2
VIA_D, VIA_DRILL = 0.7, 0.35

# Pico 2 W antenna (end opposite USB): no copper on either layer under it, to the board edge.
ANTENNA_KEEPOUT = (W - 21.0, 45.8, W, H)


def build_placement():
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(2)
    nets = {}

    def net(name):
        if name not in nets:
            n = pcbnew.NETINFO_ITEM(board, name)
            board.Add(n)
            nets[name] = n
        return nets[name]

    for ref, (lib, name, x, y, rot, value, _mpn, padnets) in PARTS.items():
        fp = pcbnew.FootprintLoad(FP + lib + ".pretty", name)
        if fp is None:
            sys.exit(f"footprint not found: {lib}:{name}")
        fp.SetReference(ref)
        fp.SetValue(value)
        fp.SetPosition(P(x, y))
        fp.SetOrientationDegrees(rot)
        board.Add(fp)
        if ref in RENDER_MODELS:
            set_render_models(fp, RENDER_MODELS[ref])
        if ref == "U1":
            set_pico_models(fp)
        if ref.startswith("R") and ref[1:].isdigit():
            # banded model for this value (gen_resistor_models.py writes models/R_<value>.wrl)
            set_render_models(fp, [("${KIPRJMOD}/models/R_" + value + ".wrl", 0, (0, 0, 0))])
        for pad in fp.Pads():
            n = padnets.get(pad.GetNumber())
            if n:
                pad.SetNet(net(n))
                # Pico GND header pins sit against the board edge / antenna keepout, where
                # thermal spokes can't all land: connect them solid (fine for 2.54 mm pins).
                if ref == "U1" and n == "GND":
                    pad.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)

    # Board outline
    pts = [(0, 0), (W, 0), (W, H), (0, H)]
    for a, b in zip(pts, pts[1:] + pts[:1]):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(P(*a)); s.SetEnd(P(*b))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
        board.Add(s)

    # Design rules (JLCPCB/PCBWay standard 2-layer capability with margin)
    ds = board.GetDesignSettings()
    ds.m_MinClearance = mm(0.15)
    ds.m_TrackMinWidth = mm(0.15)   # Freerouting necks down to ~0.18 mm between pads; fab min is 0.127
    ds.m_ViasMinSize = mm(0.6)
    ds.m_MinThroughDrill = mm(0.3)
    ds.m_CopperEdgeClearance = mm(EDGE_CLEARANCE)
    ns = ds.m_NetSettings
    dflt = ns.GetDefaultNetclass()
    dflt.SetTrackWidth(mm(DEFAULT_TRACK)); dflt.SetClearance(mm(DEFAULT_CLEARANCE))
    dflt.SetViaDiameter(mm(VIA_D)); dflt.SetViaDrill(mm(VIA_DRILL))
    for cname, (tw, cl, members) in NETCLASSES.items():
        nc = pcbnew.NETCLASS(cname)
        nc.SetTrackWidth(mm(tw)); nc.SetClearance(mm(cl))
        nc.SetViaDiameter(mm(0.9)); nc.SetViaDrill(mm(0.45))
        ns.SetNetclass(cname, nc)
        for m in members:
            ns.SetNetclassPatternAssignment(m, cname)
    ns.RecomputeEffectiveNetclasses() if hasattr(ns, "RecomputeEffectiveNetclasses") else None
    board.SynchronizeNetsAndNetClasses(True)

    # Antenna keepout (rule area, both copper layers, no tracks/vias/pours; pads allowed)
    x0, y0, x1, y1 = ANTENNA_KEEPOUT
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    ls = pcbnew.LSET(); ls.AddLayer(pcbnew.F_Cu); ls.AddLayer(pcbnew.B_Cu)
    z.SetLayerSet(ls)
    z.SetDoNotAllowTracks(True); z.SetDoNotAllowVias(True); z.SetDoNotAllowZoneFills(True)
    z.SetDoNotAllowPads(False); z.SetDoNotAllowFootprints(False)
    z.SetZoneName("PICO_W_ANTENNA_KEEPOUT")
    ol = z.Outline(); ol.NewOutline()
    for (x, y) in [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]:
        ol.Append(mm(x), mm(y))
    board.Add(z)
    return board


def add_gnd_pours(board):
    gnd = board.FindNet("GND")
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(gnd)
        z.SetLocalClearance(mm(0.3))
        z.SetMinThickness(mm(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(mm(0.4))
        z.SetThermalReliefSpokeWidth(mm(0.5))
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        ol = z.Outline(); ol.NewOutline()
        for (x, y) in [(0.3, 0.3), (W - 0.3, 0.3), (W - 0.3, H - 0.3), (0.3, H - 0.3)]:
            ol.Append(mm(x), mm(y))
        board.Add(z)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())


def add_text(board, text, x, y, size=1.0, layer=pcbnew.F_SilkS, rot=0, bold=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text); t.SetPosition(P(x, y)); t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(size * (0.2 if bold else 0.15)))
    t.SetTextAngleDegrees(rot)
    if layer == pcbnew.B_SilkS:
        t.SetMirrored(True)
    board.Add(t)


# Reference-designator placement. "body": centred on the part between its first two pads
# (axial parts: the label sits inside the body outline). (dx, dy): offset from the part origin.
REF_IN_BODY = {"R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11", "R12", "R13",
               "D1", "D2", "D3", "F1", "F2"}
REF_OFFSET = {"Q1": (2.54, 3.6, 0), "C3": (1.0, 3.7, 0), "C2": (-4.3, 0.0, 0), "C1": (8.8, 0.0, 0),
              "LED1": (5.6, 0.0, 0), "LED2": (-1.27, 3.2, 0), "C4": (2.5, 0.0, 0),
              "U3": (2.54, -3.6, 0),
              "U2": (-3.81, -11.43, 90)}


def place_references(board):
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        f = fp.Reference()
        pads = sorted(fp.Pads(), key=lambda p: p.GetNumber())
        if ref in REF_IN_BODY and len(pads) >= 2:
            a, b = pads[0].GetPosition(), pads[-1].GetPosition()
            pos = pcbnew.VECTOR2I((a.x + b.x) // 2, (a.y + b.y) // 2)
            size, angle = 0.85, 0
        elif ref in REF_OFFSET:
            o = fp.GetPosition(); dx, dy, angle = REF_OFFSET[ref]
            pos = pcbnew.VECTOR2I(o.x + mm(dx), o.y + mm(dy)); size = 1.0
        else:
            continue
        f.SetPosition(pos)
        f.SetTextAngleDegrees(angle)
        f.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
        f.SetTextThickness(mm(size * 0.15))


def add_silkscreen(board):
    place_references(board)
    # Back side, between the Pico's pin rows (no pads there on the back).
    add_text(board, "SILENTYPE-PICO CARRIER v1.0", 86.0, 24.0, 1.4, layer=pcbnew.B_SilkS, rot=90, bold=True)
    add_text(board, "Apple Silentype USB controller", 89.2, 24.0, 1.0, layer=pcbnew.B_SilkS, rot=90)
    add_text(board, "Pico 2 / 2 W - 12 V 2 A centre +", 91.8, 24.0, 1.0, layer=pcbnew.B_SilkS, rot=90)
    add_text(board, "v1.0", 36.0, 54.3, 1.0, bold=True)
    add_text(board, "PRINTER", 3.2, 30.0, 1.0, rot=90)
    add_text(board, "12V 2A", 3.2, 8.0, 1.0, rot=90)
    for x, lbl in ((64.0, "G"), (66.54, "RX"), (69.08, "TX")):
        add_text(board, lbl, x, 4.35, 0.8)
    # Bold "+" beside the positive pad of each electrolytic, outside the can outline
    # (in addition to the footprint's shaded negative half).
    # The footprint's own 0.6 mm "+" (two short strokes left of pad 1) is replaced by it.
    for ref, (dx, dy, size) in {"C1": (-2.4, -4.4, 1.6), "C2": (-2.2, 0.0, 1.2), "C3": (-1.85, -1.75, 1.0)}.items():
        fp = board.FindFootprintByReference(ref)
        o = fp.GetPosition()
        for g in list(fp.GraphicalItems()):
            if (g.GetLayer() == pcbnew.F_SilkS and g.GetClass() == "PCB_SHAPE" and g.GetShapeStr() == "Line"
                    and g.GetLength() < mm(1.6) and g.GetBoundingBox().GetRight() < o.x):
                fp.Remove(g)
        add_text(board, "+", pcbnew.ToMM(o.x) + dx, pcbnew.ToMM(o.y) + dy, size, bold=True)
    add_text(board, "USB ^", 89.5, 3.0, 0.9)
    add_text(board, "PICO 2 / 2 W", 89.5, 26.0, 1.0, rot=90)
    add_text(board, "ANTENNA KEEPOUT", 89.5, 53.6, 0.8)


def strip_dsn_outlines(path):
    """Drop (outline ...) silkscreen shapes from the DSN footprint images.

    They only describe silkscreen; the router ignores them, and the Freerouting 2.5.0
    native CLI crashes serializing them (ComponentOutline missing from its native-image
    serialization config)."""
    s = open(path).read()
    out, i = [], 0
    while True:
        j = s.find("(outline", i)
        if j < 0:
            out.append(s[i:]); break
        out.append(s[i:j])
        depth, k = 0, j
        while True:
            if s[k] == "(": depth += 1
            elif s[k] == ")":
                depth -= 1
                if depth == 0: break
            k += 1
        i = k + 1
    open(path, "w").write("".join(out))


def drop_net_from_dsn(path, netname):
    """Remove a net from the DSN network so the router leaves it alone (its pads stay as
    obstacles). The GND pours on both layers connect it after import; DRC verifies that."""
    s = open(path).read()
    j = s.find(f"(net {netname}\n")
    if j < 0:
        j = s.find(f"(net {netname} ")
    depth, k = 0, j
    while True:
        if s[k] == "(": depth += 1
        elif s[k] == ")":
            depth -= 1
            if depth == 0: break
        k += 1
    s = s[:j] + s[k + 1:]
    s = s.replace(f" {netname} ", " ").replace(f" {netname}\n", "\n")
    open(path, "w").write(s)


def bare():
    """A copy of the board with every 3D model removed, for bare-PCB renders."""
    board = pcbnew.LoadBoard(PCB)
    for fp in board.GetFootprints():
        fp.Models().clear()
    board.Save(os.path.join(HERE, "build", "bare", "silentype-carrier.kicad_pcb"))


def place():
    os.makedirs(os.path.dirname(DSN), exist_ok=True)
    board = build_placement()
    board.Save(PCB)
    if not pcbnew.ExportSpecctraDSN(board, DSN):
        sys.exit("DSN export failed")
    strip_dsn_outlines(DSN)
    drop_net_from_dsn(DSN, "GND")             # GND is the copper pour on both layers, not routed
    print(f"placed {len(PARTS)} parts -> {PCB}\nDSN -> {DSN}")


def finish():
    board = pcbnew.LoadBoard(PCB)
    if not pcbnew.ImportSpecctraSES(board, SES):
        sys.exit("SES import failed")
    add_gnd_pours(board)
    add_silkscreen(board)
    board.Save(PCB)
    print(f"routed board -> {PCB}")


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "place"
    {"place": place, "finish": finish, "bare": bare}[cmd]()
