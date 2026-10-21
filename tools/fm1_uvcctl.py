#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Synthesthetic's camera controls from the computer (UVC processing unit 3 on the video control interface):
brightness = AMOUNT, saturation = RATE, contrast = TRAILS, hue = COLOR, each 0..255. What OBS or a webcam
settings app does with its sliders, from the command line (pyusb + libusb).
    .venv/bin/python tools/fm1_uvcctl.py                 read the four
    .venv/bin/python tools/fm1_uvcctl.py hue 200         set one (brightness, saturation, contrast, hue)"""
import array
import sys

import usb.backend.libusb1
import usb.core
import usb.util

VID, PID = 0x1209, 0x0003
SEL = {"brightness": 0x02, "contrast": 0x03, "hue": 0x06, "saturation": 0x07}
KNOB = {"brightness": "AMOUNT", "saturation": "RATE", "contrast": "TRAILS", "hue": "COLOR"}
PU = 3


def vc_interface(dev):
    for cfg in dev:
        for intf in cfg:
            if intf.bInterfaceClass == 0x0E and intf.bInterfaceSubClass == 0x01:
                return intf.bInterfaceNumber
    raise SystemExit("no video control interface")


def main():
    backend = usb.backend.libusb1.get_backend() or usb.backend.libusb1.get_backend(
        find_library=lambda _: "/opt/homebrew/lib/libusb-1.0.dylib")    # (Homebrew's libusb on Apple silicon)
    dev = usb.core.find(idVendor=VID, idProduct=PID, backend=backend)
    if dev is None:
        print("no Synthesthetic camera (1209:0003)"); return 3
    vc = vc_interface(dev)
    windex = PU << 8 | vc
    ctx = dev._ctx                                       # (pyusb would claim the interface for these requests, and
    handle = ctx.managed_open() or ctx.handle            #  macOS's camera driver owns it: send them on the device)

    def xfer(rt, req, val, data_or_len):
        buf = usb.util.create_buffer(data_or_len) if isinstance(data_or_len, int) else array.array("B", data_or_len)
        n = ctx.backend.ctrl_transfer(handle, rt, req, val, windex, buf, 1000)
        return buf[:n]
    if len(sys.argv) == 3:
        name, val = sys.argv[1].lower(), int(sys.argv[2])
        xfer(0x21, 0x01, SEL[name] << 8, bytes([val & 255, 0]))
        print(f"set {name} ({KNOB[name]}) = {val}")
    for name, sel in SEL.items():
        cur = xfer(0xA1, 0x81, sel << 8, 2)
        lo = xfer(0xA1, 0x82, sel << 8, 2)
        hi = xfer(0xA1, 0x83, sel << 8, 2)
        info = xfer(0xA1, 0x86, sel << 8, 1)
        print(f"{name:10s} {KNOB[name]:6s} cur {cur[0] | cur[1] << 8:3d}  range {lo[0]}..{hi[0] | hi[1] << 8}  info {info[0]:#04x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
