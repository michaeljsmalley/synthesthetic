#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The contact sheets of tests/vrender_test.c's frames (build/host/NN-name-F.ppm, 160 x 90):
  build/host/vrender_sheet.png        every mode's last frame, 2 x, in a grid, labelled
  build/host/vrender_sheet_<a>-<b>.png  modes a..b, all their frames side by side (a closer look)
    python3 tests/vrender_sheet.py build/host [a b]"""
import glob
import os
import sys

from PIL import Image, ImageDraw

d = sys.argv[1] if len(sys.argv) > 1 else "build/host"
files = sorted(glob.glob(os.path.join(d, "[0-9][0-9]-*.ppm")))
if not files:
    sys.exit("no frames in " + d)
modes = {}
for f in files:
    n, name = os.path.basename(f).split("-")[:2]
    modes.setdefault(int(n), (name, []))[1].append(f)
Z, PAD, LAB = 2, 6, 14
W, H = 160 * Z, 90 * Z
if len(sys.argv) > 3:
    a, b = int(sys.argv[2]), int(sys.argv[3])
    rows = [m for m in sorted(modes) if a <= m <= b]
    cols = max(len(modes[m][1]) for m in rows)
    sheet = Image.new("RGB", (cols * (W + PAD) + PAD, len(rows) * (H + PAD + LAB) + PAD), (40, 40, 40))
    draw = ImageDraw.Draw(sheet)
    for r, m in enumerate(rows):
        y = PAD + r * (H + PAD + LAB)
        draw.text((PAD, y), f"{m} {modes[m][0]}", fill=(230, 230, 230))
        for c, f in enumerate(modes[m][1]):
            sheet.paste(Image.open(f).resize((W, H), Image.NEAREST), (PAD + c * (W + PAD), y + LAB))
    out = os.path.join(d, f"vrender_sheet_{a}-{b}.png")
else:
    keys = sorted(modes)
    cols = 5
    rows = (len(keys) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (W + PAD) + PAD, rows * (H + PAD + LAB) + PAD), (40, 40, 40))
    draw = ImageDraw.Draw(sheet)
    for i, m in enumerate(keys):
        x, y = PAD + (i % cols) * (W + PAD), PAD + (i // cols) * (H + PAD + LAB)
        draw.text((x, y), f"{m} {modes[m][0]}", fill=(230, 230, 230))
        sheet.paste(Image.open(modes[m][1][-1]).resize((W, H), Image.NEAREST), (x, y + LAB))
    out = os.path.join(d, "vrender_sheet.png")
sheet.save(out)
print("wrote", out, sheet.size)
