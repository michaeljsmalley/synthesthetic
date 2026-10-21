#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Decode the JPEGs tests/vjpeg_test.c wrote with Pillow (an independent decoder) and compare each with the PPM
of what was encoded. The yardstick is Pillow's own encoder (libjpeg) on the same PPM at the same quality with
4:2:0 subsampling: our file must decode (a wrong Huffman table or marker fails here), have the right size, and
land within PSNR_SLACK dB of libjpeg's PSNR, in RGB and in luma, at no more than SIZE_SLACK times its size.
Exit status: the number of failed checks."""
import glob
import io
import math
import os
import sys

from PIL import Image

LUMA_SLACK = 0.8      # dB below libjpeg in luma: the DCT, quantiser and coder must match it (0.6 dB seen at 50 dB
                      # on a smooth gradient: colour-conversion rounding, invisible)
RGB_SLACK = 4.5       # dB below libjpeg in RGB: our chroma is a 2 x 2 box average, libjpeg's a triangle filter that
                      # pairs with the decoder's fancy upsampling; on a per-pixel rainbow that is worth up to 4 dB,
                      # on shapes, gradients and noise it is 0.0-0.1 dB (see the table the check prints)
SIZE_SLACK = 1.35     # times libjpeg's file size (we have no optimised Huffman tables)


def psnr(a, b, mode):
    pa, pb = a.convert(mode).tobytes(), b.convert(mode).tobytes()
    if len(pa) != len(pb):
        return 0.0
    se = sum((x - y) * (x - y) for x, y in zip(pa, pb))
    mse = se / len(pa)
    return 99.0 if mse == 0 else 10.0 * math.log10(255.0 * 255.0 / mse)


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "build/host"
    bad = 0
    files = sorted(glob.glob(os.path.join(d, "vjpeg-*.jpg")))
    if not files:
        print("vjpeg_check: no JPEG files in", d)
        return 1
    print(f"vjpeg: {'file':30s} {'size':>8s} {'RGB dB':>7s} {'Y dB':>6s} | libjpeg {'size':>8s} {'RGB dB':>7s} {'Y dB':>6s}")
    for f in files:
        base = os.path.basename(f)[len("vjpeg-"):]
        name, q = base.rsplit("-q", 1)
        q = int(q[:-4])
        ref = Image.open(os.path.join(d, f"vjpeg-{name}.ppm"))
        try:
            im = Image.open(f)
            im.load()
        except Exception as e:  # noqa: BLE001
            print(f"vjpeg: {base:30s} decode FAILED: {e}")
            bad += 1
            continue
        buf = io.BytesIO()
        ref.save(buf, "JPEG", quality=q, subsampling="4:2:0", optimize=False)
        lib = Image.open(io.BytesIO(buf.getvalue()))
        ours = (os.path.getsize(f), psnr(im, ref, "RGB"), psnr(im, ref, "L"))
        theirs = (len(buf.getvalue()), psnr(lib, ref, "RGB"), psnr(lib, ref, "L"))
        ok = (im.size == ref.size and ours[1] >= theirs[1] - RGB_SLACK and ours[2] >= theirs[2] - LUMA_SLACK
              and ours[0] <= theirs[0] * SIZE_SLACK)
        print(f"vjpeg: {base:30s} {ours[0]:8d} {ours[1]:7.1f} {ours[2]:6.1f} |         {theirs[0]:8d} {theirs[1]:7.1f} {theirs[2]:6.1f}"
              f"  {'ok' if ok else 'FAIL'}")
        bad += not ok
    print(f"vjpeg_check: {bad} failed")
    return bad


if __name__ == "__main__":
    sys.exit(main())
