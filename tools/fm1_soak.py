#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A soak of the video build on the unit: rounds of (a Program Change on channel 1, so the project changes and
autosaves once quiet; notes for a while; a short webcam capture; silence long enough for the autosave), with
the video core's counters read after each round; every third round on SCREEN (the display's copy changes hands
between the cores going in and out). Watches for: parks that fail (park_fail), the stream dying
(no distinct frames), the MIDI side not answering (--info), USB resets.
    .venv/bin/python tools/fm1_soak.py build/soak --rounds 25 --round-seconds 60"""
import argparse
import os
import re
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable


def diag():
    out = subprocess.run([PY, os.path.join(HERE, "fm1_vcdiag.py")], capture_output=True, text=True, timeout=10).stdout
    d = {}
    for line in out.splitlines():
        m = re.match(r"(\S+)\s+(\d+)", line)
        if m:
            d[m.group(1)] = int(m.group(2))
    return d


def info():
    out = subprocess.run([PY, os.path.join(HERE, "fm1_install.py"), "--info"], capture_output=True, text=True, timeout=20).stdout
    return out.strip().splitlines()[-1] if out.strip() else "no answer"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("out")
    ap.add_argument("--rounds", type=int, default=25)
    ap.add_argument("--round-seconds", type=float, default=60.0)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    import mido
    ports = [n for n in mido.get_output_names() if "Felucca" in n or "Synthesthetic" in n]
    if not ports:
        print("no FM-1 MIDI port"); return 3
    log = open(os.path.join(a.out, "soak.log"), "a")
    def say(s):
        line = time.strftime("%H:%M:%S ") + s
        print(line, flush=True); log.write(line + "\n"); log.flush()
    d0 = diag()
    say(f"start: {info()} parks={d0.get('vc1.parks')} park_fail={d0.get('park_fail')} frames={d0.get('vc1.frames')} resets={d0.get('usb.resets')}")
    bad = 0
    for r in range(a.rounds):
        t0 = time.time()
        frames = distinct = 0
        stop = threading.Event()
        def play():
            with mido.open_output(ports[0]) as out:
                out.send(mido.Message("program_change", channel=0, program=r % 8))   # track 1's sound: the project changes
                time.sleep(0.2)
                bass = [36, 36, 43, 36, 41, 36, 43, 46]                               # a bass line, a chord, drums
                step = 0
                while not stop.is_set() and time.time() - t0 < 18.0:
                    n = bass[step % 8]
                    out.send(mido.Message("note_on", channel=0, note=n, velocity=110))
                    out.send(mido.Message("note_on", channel=9, note=36 if step % 2 == 0 else 42, velocity=120))
                    if step % 8 == 0:
                        for c in (60, 64, 67):
                            out.send(mido.Message("note_on", channel=1, note=c, velocity=90))
                    time.sleep(0.11)
                    out.send(mido.Message("note_off", channel=0, note=n))
                    out.send(mido.Message("note_off", channel=9, note=36 if step % 2 == 0 else 42))
                    if step % 8 == 7:
                        for c in (60, 64, 67):
                            out.send(mido.Message("note_off", channel=1, note=c))
                    time.sleep(0.11)
                    step += 1
                for c in (60, 64, 67):
                    out.send(mido.Message("note_off", channel=1, note=c))
        th = threading.Thread(target=play, daemon=True)
        th.start()
        time.sleep(4)                                    # a capture while it plays (the camera opened and closed each round)
        cap = subprocess.run([PY, os.path.join(HERE, "fm1_capture.py"), os.path.join(a.out, "cap"), "--seconds", "2",
                              "--quiet", "--style", str(30 if r % 3 == 2 else r % 30)], capture_output=True, text=True, timeout=40).stdout
        m = re.search(r"(\d+) frames in [\d.]+ s, (\d+) distinct", cap)
        frames, distinct = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
        th.join(timeout=20)
        stop.set()
        # silence: the autosave needs quiet audio and idle input
        while time.time() - t0 < a.round_seconds:
            time.sleep(1)
        d = diag()
        ident = info()
        ok = d.get("park_fail", 1) == 0 and distinct >= 10 and "FM-1" in ident
        bad += not ok
        say(f"round {r + 1}/{a.rounds}: {ident} capture {frames}/{distinct} parks={d.get('vc1.parks')} park_fail={d.get('park_fail')} "
            f"drops={d.get('vc1.drops')} q={d.get('vc1.quality')} frames={d.get('vc1.frames')} uvc_starts={d.get('uvc.starts')} "
            f"resets={d.get('usb.resets')} {'ok' if ok else 'BAD'}")
    say(f"done: {bad} bad rounds of {a.rounds}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
