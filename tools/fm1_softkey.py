#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Drop a running FM-1 into the chip's ROM USB download mode ("WL82 UBOOT1.00").

  fm1_softkey.py [--port NAME] [--yes]

Sends the stock soft key F0 22 24 35 7D F7 over USB-MIDI. Official V15 and the
Felucca family (Felucca, SLOOP, Dinghy, ...) answer it by resetting into the
mask-ROM download mode, where M-Flasher / jl-uboot-tool can read and write the
whole flash. Nothing is written; switching the FM-1 off and on brings it back.

First the identity is asked with the stock handshake (as fm1_install.py --info
does) and shown, then the key is sent after a typed confirmation (or --yes).
Start M-Flasher within about half a minute: left to macOS's disk driver the
unit may leave download mode again.
"""
import argparse
import os
import sys
import time

import mido

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fm1_install  # noqa: E402  (same directory: the stock handshake and its identity parser)

HS_QUERY = bytes([0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7])
SOFT_KEY = bytes([0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7])


def find_ports(part):
    ins = [n for n in mido.get_input_names() if (part or "FM-1") in n]
    outs = [n for n in mido.get_output_names() if (part or "FM-1") in n]
    if not ins or not outs:
        return None, None
    return ins[0], outs[0]


def identity(in_name, out_name, timeout=2.0):
    """The stock handshake: the first SysEx that fm1_install.parse_identity accepts, as text ("FM-1_015")."""
    with mido.open_input(in_name) as inp, mido.open_output(out_name) as out:
        out.send(mido.Message("sysex", data=HS_QUERY[1:-1]))
        t0 = time.time()
        while time.time() - t0 < timeout:
            for m in inp.iter_pending():
                if m.type == "sysex":
                    ident = fm1_install.parse_identity(bytes([0xF0]) + bytes(m.data) + bytes([0xF7]))
                    if ident:
                        return ident.text
            time.sleep(0.02)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", metavar="NAME", help="MIDI port to use (part of its name, default FM-1)")
    ap.add_argument("--yes", action="store_true", help="send without asking")
    a = ap.parse_args()
    in_name, out_name = find_ports(a.port)
    if not in_name:
        print("FM-1 not found: no MIDI port matching", repr(a.port or "FM-1"), file=sys.stderr)
        return 3
    ident = identity(in_name, out_name)
    print(f"port: {out_name}   identity: {ident or 'no answer (not stock or Felucca-family?)'}")
    if ident is None and not a.yes:
        print("the handshake got no answer; use --yes to send the key anyway", file=sys.stderr)
        return 4
    if not a.yes:
        if input("Send the soft key and reset into ROM download mode? Type UBOOT to confirm: ").strip() != "UBOOT":
            print("not sent")
            return 1
    with mido.open_output(out_name) as out:
        out.send(mido.Message("sysex", data=SOFT_KEY[1:-1]))
    print("sent F0 22 24 35 7D F7: the FM-1 should drop off MIDI and appear as the disk 'WL82 UBOOT1.00'.")
    print("Start M-Flasher now (within ~30 s). Switch the FM-1 off and on to leave download mode.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
