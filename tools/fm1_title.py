#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Set the title card's text on a running Synthesthetic (the video's overlay, menu VJ > OVERLAY = TITLE or
INFO+TITLE): F0 7D 56 43 06 <text> F7 over USB-MIDI. Up to 19 characters; the FM-1 keeps it with its settings.
--overlay off|info|title|both also sets the overlay (F0 7D 56 43 07 n F7). The editor's VJ page and the
installer page set both too.
    .venv/bin/python tools/fm1_title.py "LIVE FROM THE BASEMENT" --overlay title
    .venv/bin/python tools/fm1_title.py ""          (no title)"""
import argparse
import sys

import mido

OVERLAY = ["off", "info", "title", "both"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("text", nargs="?", default="SYNTHESTHETIC")
    ap.add_argument("--overlay", choices=OVERLAY)
    a = ap.parse_args()
    data = [c for c in a.text.upper().encode("ascii", "replace") if 0x20 <= c < 0x7F][:19]
    outs = [n for n in mido.get_output_names() if "Synthesthetic" in n or "Felucca" in n]
    if not outs:
        print("no Synthesthetic MIDI port"); return 3
    with mido.open_output(outs[0]) as out:
        out.send(mido.Message("sysex", data=[0x7D, 0x56, 0x43, 0x06] + data))
        if a.overlay:
            out.send(mido.Message("sysex", data=[0x7D, 0x56, 0x43, 0x07, OVERLAY.index(a.overlay)]))
    print("title:", bytes(data).decode() or "(none)", *(["overlay:", a.overlay] if a.overlay else []))
    return 0


if __name__ == "__main__":
    sys.exit(main())
