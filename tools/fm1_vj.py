#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Synthesthetic's visual settings over USB-MIDI: the editor protocol's backup object 12 (protocol v13,
web/EDITOR_PROTOCOL.md; firmware/src/vis_set.h vis_set_t). The web editor's VJ page and the installer page set the
same settings; this is the command line's way.
    .venv/bin/python tools/fm1_vj.py                    show them
    .venv/bin/python tools/fm1_vj.py title="LIVE AT NINE" overlay=title mode=SCREEN palette=NEON autopilot=8 \\
        attract=60 midi=16 knobs=128,128,160,0
    .venv/bin/python tools/fm1_vj.py --defaults         back to the defaults
mode: a name (WARP) or 1..31; palette: a name or 1..9; overlay: off info title both; autopilot: off 4 8 16 32 (bars);
attract: off 30 60 120 300 (seconds); midi: off or 1..16; knobs: AMOUNT,RATE,TRAILS,COLOR 0..255."""
import struct
import sys
import time
import zlib

HDR = [0x7D, 0x46, 0x4C]
INFO, BK_LIST, BK_GET, BK_PUT = 1, 34, 35, 36
OBJ, SIZE, MAGIC = 12, 40, 0x31534956
MODES = ["WAVE", "BARS", "HEAT", "XY", "METERS", "RING", "SPIRAL", "GRID", "KIT", "TUNNEL", "MIRROR", "STARS",
         "SQUARES", "SHARDS", "ORBITS", "MOIRE", "WARP", "VORTEX", "RIPPLE", "KALEIDO", "MELT", "SMOKE", "WORMHOLE",
         "PLASMA", "FIRE", "RIDGES", "LAVA", "CELLS", "WIREFRAME", "FRESNEL", "SCREEN"]
PALS = ["SPECTRUM", "NEON", "FIRE", "OCEAN", "CRT", "VAPOR", "ACID", "THERMAL", "MONO"]
OVERLAY = ["off", "info", "title", "both"]
AUTO = ["off", "4", "8", "16", "32"]
ATTRACT = ["off", "30", "60", "120", "300"]
DEFAULTS = {"mode": 16, "palette": 0, "autopilot": 2, "attract": 2, "hud": 0, "midi": 16, "knob": [128, 128, 160, 0],
            "title": "SYNTHESTHETIC"}


def pack7(b):
    out = []
    for i in range(0, len(b), 7):
        g = b[i:i + 7]
        out.append(sum(((x >> 7) & 1) << j for j, x in enumerate(g)))
        out += [x & 0x7F for x in g]
    return out


def unpack7(a):
    out = []
    for i in range(0, len(a), 8):
        top = a[i]
        out += [a[i + 1 + j] | (((top >> j) & 1) << 7) for j in range(min(7, len(a) - i - 1))]
    return bytes(out)


def u35(v):
    return [(v >> (7 * k)) & 0x7F for k in range(5)]


def u35dec(a, i):
    return sum(a[i + k] << (7 * k) for k in range(5)) & 0xFFFFFFFF, i + 5


def clean(t):
    return "".join(c if 0x20 <= ord(c) < 0x7F else "?" for c in str(t).upper())[:19]


def encode(v):
    t = clean(v["title"]).encode("ascii")
    b = struct.pack("<I6B2x4B20s", MAGIC, v["mode"], v["palette"], v["autopilot"], v["attract"], v["hud"], v["midi"],
                    *v["knob"], t)
    return b + struct.pack("<I", zlib.crc32(b) & 0xFFFFFFFF)


def decode(b):
    if len(b) != SIZE:
        return None
    magic, mode, pal, auto, attr, hud, midi, k0, k1, k2, k3, title, crc = struct.unpack("<I6B2x4B20sI", b)
    if magic != MAGIC or crc != zlib.crc32(b[:36]) & 0xFFFFFFFF:
        return None
    return {"mode": mode, "palette": pal, "autopilot": auto, "attract": attr, "hud": hud, "midi": midi,
            "knob": [k0, k1, k2, k3], "title": title.split(b"\0")[0].decode("ascii", "replace")}


class Link:
    def __init__(self):
        import mido
        self.mido = mido
        outs = [n for n in mido.get_output_names() if "Synthesthetic" in n or "Felucca" in n]
        ins = [n for n in mido.get_input_names() if "Synthesthetic" in n or "Felucca" in n]
        if not outs or not ins:
            sys.exit("no FM-1 MIDI port (Synthesthetic / Felucca): connect it by USB")
        self.o, self.i = mido.open_output(outs[0]), mido.open_input(ins[0])

    def req(self, cmd, args, timeout=3.0, tries=3):
        for _ in range(tries):
            for _ in self.i.iter_pending():
                pass
            self.o.send(self.mido.Message("sysex", data=HDR + [cmd] + list(args)))
            t0 = time.time()
            while time.time() - t0 < timeout:
                for m in self.i.iter_pending():
                    if m.type == "sysex" and list(m.data[:4]) == HDR + [cmd]:
                        return list(m.data[4:])
                time.sleep(0.002)
        raise TimeoutError(f"no reply to command {cmd}")


def read(link):
    a = link.req(BK_LIST, [], 8.0, 1)
    if a[0]:
        sys.exit(f"BK_LIST rc {a[0]}")
    i, n = 2, a[1]
    for _ in range(n):
        oid = a[i]
        ln, i = u35dec(a, i + 1)
        _, i = u35dec(a, i)
        if oid == OBJ:
            g = link.req(BK_GET, [OBJ] + u35(0) + [ln & 0x7F, ln >> 7])
            if g[1]:
                sys.exit(f"BK_GET rc {g[1]}")
            return decode(unpack7(g[9:]))
    return None


def write(link, v):
    d = encode(v)
    for op, args in ((0, [OBJ] + u35(len(d)) + u35(zlib.crc32(d) & 0xFFFFFFFF)), (1, [OBJ] + u35(0) + pack7(d)), (2, [OBJ])):
        r = link.req(BK_PUT, [op] + args, 5.0, 1)
        if r[2]:
            return r[2]
    return 0


def show(v):
    print(f"mode      {v['mode'] + 1} {MODES[v['mode']]}")
    print(f"palette   {PALS[v['palette']]}")
    print(f"overlay   {OVERLAY[v['hud']]}")
    print(f"title     {v['title'] or '(none)'}")
    print(f"autopilot {AUTO[v['autopilot']]}{'' if not v['autopilot'] else ' bars'}")
    print(f"attract   {ATTRACT[v['attract']]}{'' if not v['attract'] else ' s'}")
    print(f"midi ch   {v['midi'] or 'off'}")
    print(f"knobs     AMOUNT {v['knob'][0]} RATE {v['knob'][1]} TRAILS {v['knob'][2]} COLOR {v['knob'][3]}")


def apply(v, arg):
    k, _, x = arg.partition("=")
    k, xu = k.lower(), x.strip().upper()
    if k == "title":
        v["title"] = clean(x)
    elif k == "mode":
        v["mode"] = MODES.index(xu) if xu in MODES else int(x) - 1
    elif k == "palette":
        v["palette"] = PALS.index(xu) if xu in PALS else int(x) - 1
    elif k in ("overlay", "hud"):
        v["hud"] = OVERLAY.index(x.lower())
    elif k == "autopilot":
        v["autopilot"] = AUTO.index(x.lower())
    elif k == "attract":
        v["attract"] = ATTRACT.index(x.lower())
    elif k == "midi":
        v["midi"] = 0 if x.lower() == "off" else int(x)
    elif k == "knobs":
        v["knob"] = [int(n) & 255 for n in x.split(",")][:4]
    else:
        sys.exit(f"? {arg}")
    if not (0 <= v["mode"] < len(MODES) and 0 <= v["palette"] < len(PALS) and 0 <= v["midi"] <= 16 and len(v["knob"]) == 4):
        sys.exit(f"out of range: {arg}")


def main():
    args = sys.argv[1:]
    link = Link()
    info = link.req(INFO, [], 1.0)
    if not info or info[-1] < 13:
        sys.exit("the FM-1 does not speak protocol 13: install Synthesthetic first")
    v = read(link)
    if v is None:
        sys.exit("no visual settings object (12) on the FM-1")
    if not args:
        show(v)
        return 0
    v = dict(DEFAULTS, knob=list(DEFAULTS["knob"])) if args == ["--defaults"] else v
    for a in args:
        if a != "--defaults":
            apply(v, a)
    rc = write(link, v)
    if rc:
        sys.exit(f"refused: rc {rc} (2: not valid, 4: flash)")
    back = read(link)
    show(back)
    return 0 if back == v else 1


if __name__ == "__main__":
    sys.exit(main())
