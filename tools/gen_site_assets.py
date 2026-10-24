#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The installer page's pictures, cut from the live video captures in docs/images (every frame a real one the FM-1
sent: 320 x 180 for the visual modes, SCREEN from a 320 x 240 one):
  assets/site/channels.jpg    the hero monitor's channels, one under the other (web/make_site.py lays them out from
                              assets/site/channels.json and copies the picture next to the installer)
  assets/site/title-bg.jpg    the frame behind the title overlay's preview (WARP, the default mode)
  assets/site/modes/NN.jpg    every mode, 01..31, for the installer page's gallery (its names and descriptions come
                              from HANDBOOK.md's table of the modes)
    tools/gen_site_assets.py
Needs Pillow."""
import json
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SHEET = ROOT / "docs" / "images" / "v3-modes-live.png"          # 30 modes, 5 x 6 tiles of 320 x 180 under a label
SCREEN = ROOT / "docs" / "images" / "screen-and-title-4x3.png"   # left: SCREEN on the TRACKS page (320 x 240), 2 x, at 4, 4
OUT = ROOT / "assets" / "site"
NAMES = ["WAVE", "BARS", "HEAT", "XY", "METERS", "RING", "SPIRAL", "GRID", "KIT", "TUNNEL", "MIRROR", "STARS",
         "SQUARES", "SHARDS", "ORBITS", "MOIRE", "WARP", "VORTEX", "RIPPLE", "KALEIDO", "MELT", "SMOKE", "WORMHOLE",
         "PLASMA", "FIRE", "RIDGES", "LAVA", "CELLS", "WIREFRAME", "FRESNEL", "SCREEN"]
CHANNELS = [17, 20, 25, 31, 27, 7, 30, 29, 24, 9, 10, 2]           # mode numbers, 1-based, as the FM-1 shows them


def tile(sheet, n):
    k = n - 1
    x, y = 6 + 326 * (k % 5), 20 + 200 * (k // 5)
    return sheet.crop((x, y, x + 320, y + 180))


def screen_tile():
    """SCREEN in a 320 x 180 tile: the display (240 x 240 at 40, 0 in the 320 x 240 picture) scaled smoothly to
    180 x 180 in the middle (the video itself has it pixel for pixel; dropping rows would merge the letters)"""
    shot = Image.open(SCREEN).convert("RGB").crop((4 + 80, 4, 4 + 80 + 480, 4 + 480))
    out = Image.new("RGB", (320, 180))
    out.paste(shot.resize((180, 180), Image.LANCZOS), (70, 0))
    return out


def main():
    sheet = Image.open(SHEET).convert("RGB")
    screen = screen_tile()
    frames = [screen if n == 31 else tile(sheet, n) for n in CHANNELS]
    strip = Image.new("RGB", (320, 180 * len(frames)))
    for i, f in enumerate(frames):
        strip.paste(f, (0, 180 * i))
    OUT.mkdir(parents=True, exist_ok=True)
    strip.save(OUT / "channels.jpg", quality=86, optimize=True, progressive=True)
    (OUT / "channels.json").write_text(json.dumps([{"n": n, "name": NAMES[n - 1]} for n in CHANNELS], indent=1) + "\n")
    tile(sheet, 17).save(OUT / "title-bg.jpg", quality=86, optimize=True, progressive=True)
    (OUT / "modes").mkdir(exist_ok=True)
    for n in range(1, 32):
        (screen if n == 31 else tile(sheet, n)).save(OUT / "modes" / f"{n:02d}.jpg", quality=82, optimize=True, progressive=True)
    print(f"site assets: {len(frames)} channels ({(OUT / 'channels.jpg').stat().st_size} B), title-bg "
          f"({(OUT / 'title-bg.jpg').stat().st_size} B)")


if __name__ == "__main__":
    main()
