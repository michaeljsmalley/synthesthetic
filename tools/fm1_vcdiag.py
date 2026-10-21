#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read the video core's counters from a running fm1drome build: F0 7D 56 43 01 F7 over USB-MIDI, the reply
F0 7D 56 43 02 <32 x 32-bit values as nibbles, low first> F7 (usb.c vcore_diag_send)."""
import sys
import time

import mido

NAMES = ["vcore_running", "vc0.run", "vc0.park_req", "vc0.jpeg_busy", "vc1.parked", "vc1.done", "vc1.jpeg_ready",
         "vc1.jpeg_len[0]", "vc1.jpeg_len[1]", "vc1.frame_ready", "vc1.frames", "vc1.parks", "vc1.drops", "vc1.quality",
         "vc1.us_render", "vc1.us_encode", "uvc.alt", "uvc.starts", "uvc.frames", "uvc.pkts", "uvc.missed", "uvc.stalls",
         "uvc.sets", "uvc.pu_sets", "c1_stack_free", "src_calls", "src_live", "src_bad", "fm1_c1_on", "cpu1_taken", "park_fail", "uvc.pu_gets",
         "uvc.gets", "uvc.lens", "uvc.infos", "uvc.set_if", "uvc.vc_req", "uvc.other", "usb.setups", "usb.resets"]


def main():
    arg = sys.argv[1] if len(sys.argv) > 1 else ""
    style = int(sys.argv[2]) if arg == "style" and len(sys.argv) > 2 else None
    cmd = {"stop": 0x03, "start": 0x04, "style": 0x05}.get(arg, 0x01)
    part = ("Synthesthetic", "Felucca")
    ins = [n for n in mido.get_input_names() if any(p in n for p in part)]
    outs = [n for n in mido.get_output_names() if any(p in n for p in part)]
    if not ins or not outs:
        print("no MIDI port matching", part); return 3
    with mido.open_input(ins[0]) as inp, mido.open_output(outs[0]) as out:
        data = [0x7D, 0x56, 0x43, cmd] + ([style % 31] if cmd == 0x05 else [])
        out.send(mido.Message("sysex", data=data))
        if cmd != 0x01:
            print("sent", {3: "stop", 4: "start", 5: f"style {style}"}[cmd]); return 0
        t0 = time.time()
        while time.time() - t0 < 2.0:
            for m in inp.iter_pending():
                if m.type == "sysex" and tuple(m.data[:4]) == (0x7D, 0x56, 0x43, 0x02):
                    d = list(m.data[4:])
                    vals = []
                    for i in range(0, len(d) - 7, 8):
                        b = [(d[i + 2 * j] & 15) | ((d[i + 2 * j + 1] & 15) << 4) for j in range(4)]
                        vals.append(b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24)
                    for n, v in zip(NAMES, vals):
                        print(f"{n:18s} {v:10d}" )
                    return 0
            time.sleep(0.02)
    print("no reply in 2 s"); return 4


if __name__ == "__main__":
    sys.exit(main())
