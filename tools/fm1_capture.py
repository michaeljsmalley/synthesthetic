#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Capture the FM-1's USB video while it plays: frames from the 'Felucca' camera with ffmpeg (avfoundation) into a
directory, a contact sheet of them, and the count of distinct frames (the delivered rate). Meanwhile, unless
--quiet, notes go to the FM-1 over USB-MIDI so there is something to see (a bass line on channel 1, a chord on
channel 2, drums on channel 10); --style N first selects the video's picture (SLOOP's visualiser style, 0..11).
    .venv/bin/python tools/fm1_capture.py build/cap --seconds 4 --style 0
Needs: ffmpeg on the PATH, mido + python-rtmidi + Pillow in the venv, and no other app holding the camera."""
import argparse
import glob
import hashlib
import os
import subprocess
import sys
import threading
import time

STYLES = ["WAVE", "BARS", "HEAT", "XY", "METERS", "RING", "SPIRAL", "GRID", "KIT", "TUNNEL", "MIRROR", "STARS",
          "SQUARES", "SHARDS", "ORBITS", "MOIRE", "WARP", "VORTEX", "RIPPLE", "KALEIDO", "MELT", "SMOKE", "WORMHOLE",
          "PLASMA", "FIRE", "RIDGES", "LAVA", "CELLS", "WIREFRAME", "FRESNEL", "SCREEN"]


def play(port_name, seconds, stop):
    """a bass line, a chord and a beat until stop is set (or the time is up)"""
    import mido
    bass = [36, 36, 43, 36, 41, 36, 43, 46]
    with mido.open_output(port_name) as out:
        t0 = time.time()
        step = 0
        while not stop.is_set() and time.time() - t0 < seconds:
            n = bass[step % len(bass)]
            out.send(mido.Message("note_on", channel=0, note=n, velocity=110))
            if step % 8 == 0:
                for c in (60, 64, 67):
                    out.send(mido.Message("note_on", channel=1, note=c, velocity=90))
            out.send(mido.Message("note_on", channel=9, note=36 if step % 2 == 0 else 42, velocity=120))
            if step % 4 == 2:
                out.send(mido.Message("note_on", channel=9, note=38, velocity=120))
            if step % 8 == 6:
                out.send(mido.Message("note_on", channel=9, note=39, velocity=110))   # clap
            if step % 16 == 0:
                out.send(mido.Message("note_on", channel=9, note=49, velocity=120))   # crash
            if step % 16 == 10:
                out.send(mido.Message("note_on", channel=9, note=46, velocity=100))   # open hat
            if step % 16 in (13, 14, 15):
                out.send(mido.Message("note_on", channel=9, note=(41, 45, 48)[step % 16 - 13], velocity=110))   # toms
            time.sleep(0.11)
            out.send(mido.Message("note_off", channel=0, note=n))
            out.send(mido.Message("note_off", channel=9, note=36 if step % 2 == 0 else 42))
            if step % 4 == 2:
                out.send(mido.Message("note_off", channel=9, note=38))
            if step % 8 == 7:
                for c in (60, 64, 67):
                    out.send(mido.Message("note_off", channel=1, note=c))
            time.sleep(0.11)
            step += 1
        for c in (60, 64, 67):
            out.send(mido.Message("note_off", channel=1, note=c))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", help="directory for the frames (created; existing frames removed)")
    ap.add_argument("--seconds", type=float, default=4.0)
    ap.add_argument("--style", type=int, help="select the mode first (0..15, tools/fm1_vcdiag.py style)")
    ap.add_argument("--quiet", action="store_true", help="send no notes")
    ap.add_argument("--camera", default="Synthesthetic", help="the camera's name (an older build: Felucca)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for f in glob.glob(os.path.join(a.out, "f-*.png")):
        os.remove(f)
    port = None
    if not a.quiet or a.style is not None:
        import mido
        ports = [n for n in mido.get_output_names() if "Synthesthetic" in n or "Felucca" in n]
        if not ports:
            print("no Felucca MIDI port"); return 3
        port = ports[0]
    if a.style is not None:
        import mido
        with mido.open_output(port) as out:
            out.send(mido.Message("sysex", data=[0x7D, 0x56, 0x43, 0x05, a.style % 31]))
        print("style", a.style % 31, STYLES[a.style % 31])
        time.sleep(0.3)
    stop = threading.Event()
    th = None
    if not a.quiet:
        th = threading.Thread(target=play, args=(port, a.seconds + 2.0, stop), daemon=True)
        th.start()
        time.sleep(0.5)                                  # the first notes sound before the first frame
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "avfoundation", "-framerate", "30",
           "-video_size", "320x180", "-pixel_format", "uyvy422", "-i", a.camera, "-t", str(a.seconds),
           "-fps_mode", "passthrough", os.path.join(a.out, "f-%03d.png")]   # passthrough: a file per frame delivered
    t0 = time.time()
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=a.seconds + 20)
        if r.returncode and a.camera == "Synthesthetic" and "not found" in r.stderr.lower():   # an older build's name
            cmd[cmd.index("-i") + 1] = "Felucca"
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=a.seconds + 20)
        if r.returncode and "fps_mode" in r.stderr:          # an older ffmpeg: -vsync
            cmd[cmd.index("-fps_mode")] = "-vsync"
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=a.seconds + 20)
    except subprocess.TimeoutExpired:
        stop.set()
        print("ffmpeg TIMEOUT (another app holding the camera?)"); return 4
    took = time.time() - t0
    stop.set()
    if th:
        th.join(timeout=3)
    if r.returncode:
        print("ffmpeg failed:", r.stderr.strip()[-400:]); return 5
    frames = sorted(glob.glob(os.path.join(a.out, "f-*.png")))
    hashes = [hashlib.md5(open(f, "rb").read()).hexdigest() for f in frames]
    distinct = len(set(hashes))
    changes = sum(1 for i in range(1, len(hashes)) if hashes[i] != hashes[i - 1])
    print(f"{len(frames)} frames in {took:.1f} s, {distinct} distinct, {changes} changes: "
          f"about {changes / a.seconds:.1f} new frames/s through ffmpeg (it keeps one frame at a time and loses some "
          f"above ~22 fps: tools/fm1_avrate.py measures what macOS delivers)")
    try:
        from PIL import Image, ImageDraw
        pick = frames[:: max(1, len(frames) // 12)][:12]
        W, H = Image.open(pick[0]).size
        sheet = Image.new("RGB", (4 * (W + 4) + 4, 3 * (H + 4) + 4), (40, 40, 40))
        d = ImageDraw.Draw(sheet)
        for i, f in enumerate(pick):
            x, y = 4 + (i % 4) * (W + 4), 4 + (i // 4) * (H + 4)
            sheet.paste(Image.open(f).convert("RGB"), (x, y))
            d.text((x + 4, y + 4), os.path.basename(f), fill=(255, 255, 255))
        sheet.save(os.path.join(a.out, "sheet.png"))
        print("sheet:", os.path.join(a.out, "sheet.png"))
    except Exception as e:                               # no Pillow: the frames are still there
        print("no sheet:", e)
    return 0


if __name__ == "__main__":
    sys.exit(main())
