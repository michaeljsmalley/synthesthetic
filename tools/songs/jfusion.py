#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Play page's song from 2026-10-21 to the new one, as it was written by hand in firmware/host/web.c: Japanese
fusion at 90 BPM in F major, in the spirit of Himiko Kikuchi's "Look Your Back!" and Masayoshi Takanaka's
"Seychelles". INTRO 4 bars, VERSE 8, CHORUS 8, VERSE 8, BRIDGE 4, CHORUS 8. Kept to compare against."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from songlib import Song  # noqa: E402

# the voicings, as the hand-written song had them (rootless, E3..E4, each voice a step from the last)
CH = {"FM9": [52, 55, 57, 60], "BBM9": [53, 57, 60, 62], "GM9": [53, 57, 58, 62], "C13": [52, 57, 58, 62],
      "C9S": [53, 55, 58, 62], "C9": [52, 55, 58, 62], "AM11": [52, 55, 60, 62], "DM9": [53, 57, 60, 64],
      "DBM9": [53, 56, 60, 63], "EBM9": [55, 58, 62, 65], "CM9": [55, 58, 62, 63], "F13": [55, 57, 62, 63]}
CH["C11"] = CH["GM9"]
# the tables: a note is (step, pitch, steps, velocity, accent), a chord (step, steps, velocity, voicing)
V_EP = [[(0, 4, 96, "FM9"), (6, 2, 82, "FM9"), (10, 3, 88, "FM9"), (14, 2, 92, "BBM9")],
        [(3, 2, 84, "BBM9"), (6, 2, 82, "BBM9"), (10, 3, 88, "BBM9"), (14, 2, 92, "FM9")],
        [(3, 2, 84, "FM9"), (6, 2, 82, "FM9"), (10, 3, 88, "FM9"), (14, 2, 92, "GM9")],
        [(3, 2, 84, "GM9"), (6, 2, 94, "C13"), (10, 3, 86, "C13"), (14, 2, 90, "C13")]]
V_BASS = [[(0, 29, 3, 112, 1), (3, 41, 1, 100, 1), (6, 36, 2, 92, 0), (8, 33, 2, 96, 0), (10, 36, 1, 90, 0),
           (11, 41, 1, 98, 0), (12, 40, 2, 92, 0), (14, 34, 2, 108, 1)],
          [(2, 34, 1, 64, 0), (3, 46, 1, 100, 1), (6, 29, 2, 92, 0), (8, 34, 2, 96, 0), (10, 38, 1, 90, 0),
           (11, 41, 1, 96, 0), (12, 33, 2, 90, 0), (14, 29, 2, 108, 1)],
          [(2, 29, 1, 64, 0), (3, 41, 1, 100, 1), (6, 36, 2, 92, 0), (8, 33, 2, 96, 0), (10, 36, 1, 90, 0),
           (11, 38, 1, 88, 0), (12, 40, 1, 90, 0), (13, 41, 1, 92, 0), (14, 31, 2, 108, 1)],
          [(2, 31, 1, 64, 0), (3, 43, 1, 100, 1), (4, 38, 2, 90, 0), (6, 36, 2, 104, 1), (10, 40, 1, 90, 0),
           (11, 43, 1, 92, 0), (12, 34, 2, 92, 0), (14, 36, 2, 100, 0)]]
V_THEME = [[(0, 69, 3, 96, 0), (3, 72, 1, 84, 0), (4, 76, 4, 100, 0), (8, 74, 2, 86, 0), (10, 72, 2, 88, 0),
            (12, 69, 4, 92, 0)],
           [(2, 67, 1, 82, 0), (3, 69, 1, 86, 0), (4, 72, 4, 98, 0), (8, 74, 3, 92, 0), (11, 72, 1, 82, 0),
            (12, 69, 2, 88, 0), (14, 65, 2, 90, 0)],
           [(0, 67, 6, 94, 0), (6, 69, 2, 86, 0), (8, 72, 4, 92, 0), (12, 76, 2, 96, 0), (14, 74, 2, 90, 0)],
           [(0, 74, 3, 94, 0), (3, 72, 1, 82, 0), (4, 70, 4, 92, 0), (8, 69, 3, 96, 0), (11, 67, 1, 84, 0),
            (12, 64, 4, 90, 0)]]
C_EP = [[(0, 4, 100, "BBM9"), (6, 2, 84, "BBM9"), (10, 3, 88, "BBM9"), (14, 2, 92, "C9S")],
        [(3, 2, 84, "C9S"), (6, 2, 94, "C9"), (10, 3, 86, "C9"), (14, 2, 92, "AM11")],
        [(3, 2, 82, "AM11"), (6, 2, 86, "AM11"), (10, 3, 88, "AM11"), (14, 2, 94, "DM9")],
        [(3, 2, 84, "DM9"), (6, 2, 96, "C11"), (10, 3, 86, "C11"), (14, 2, 92, "C11")]]
C_BASS = [[(0, 34, 3, 112, 1), (3, 46, 1, 100, 1), (6, 29, 2, 92, 0), (8, 34, 2, 96, 0), (10, 38, 1, 90, 0),
           (11, 41, 1, 96, 0), (12, 33, 2, 90, 0), (14, 36, 2, 108, 1)],
          [(2, 36, 1, 64, 0), (3, 48, 1, 100, 1), (6, 31, 2, 92, 0), (8, 36, 2, 100, 0), (10, 40, 1, 90, 0),
           (11, 43, 1, 94, 0), (12, 31, 1, 90, 0), (13, 32, 1, 88, 0), (14, 33, 2, 108, 1)],
          [(2, 33, 1, 64, 0), (3, 45, 1, 100, 1), (6, 40, 2, 92, 0), (8, 33, 2, 96, 0), (10, 36, 1, 90, 0),
           (11, 40, 1, 94, 0), (12, 43, 1, 90, 0), (13, 40, 1, 88, 0), (14, 38, 2, 108, 1)],
          [(2, 38, 1, 64, 0), (3, 50, 1, 100, 1), (4, 33, 2, 90, 0), (6, 36, 2, 104, 1), (9, 48, 1, 96, 0),
           (10, 34, 1, 88, 0), (11, 31, 1, 88, 0), (12, 29, 1, 90, 0), (13, 31, 1, 88, 0), (14, 36, 2, 100, 0)]]
C_HOOK = [[(0, 74, 3, 100, 0), (3, 72, 1, 86, 0), (4, 74, 2, 92, 0), (6, 77, 2, 96, 0), (8, 72, 4, 92, 0),
           (12, 74, 2, 90, 0), (14, 77, 2, 100, 0)],
          [(0, 77, 4, 98, 0), (6, 76, 2, 94, 0), (8, 74, 2, 90, 0), (10, 76, 2, 92, 0), (12, 79, 4, 104, 0)],
          [(0, 76, 4, 98, 0), (4, 74, 2, 90, 0), (6, 72, 2, 88, 0), (8, 74, 4, 92, 0), (12, 76, 2, 94, 0),
           (14, 77, 2, 98, 0)],
          [(2, 76, 2, 92, 0), (4, 74, 4, 96, 0), (8, 72, 4, 92, 0), (12, 70, 2, 90, 0), (14, 69, 2, 94, 0)]]
B_EP = [[(0, 8, 96, "DBM9"), (8, 6, 84, "DBM9"), (14, 2, 90, "EBM9")],
        [(0, 8, 92, "EBM9"), (8, 6, 84, "EBM9"), (14, 2, 90, "CM9")],
        [(0, 8, 92, "CM9"), (8, 6, 84, "CM9"), (14, 2, 92, "F13")],
        [(0, 8, 96, "F13"), (8, 6, 88, "F13"), (14, 2, 92, "F13")]]
B_BASS = [[(0, 37, 6, 108, 1), (6, 32, 2, 90, 0), (8, 37, 4, 96, 0), (12, 36, 2, 88, 0), (14, 39, 2, 104, 1)],
          [(6, 34, 2, 90, 0), (8, 39, 4, 96, 0), (12, 38, 2, 88, 0), (14, 36, 2, 104, 1)],
          [(6, 31, 2, 90, 0), (8, 36, 4, 96, 0), (12, 34, 2, 88, 0), (14, 29, 2, 104, 1)],
          [(4, 36, 2, 90, 0), (6, 39, 2, 92, 0), (8, 41, 2, 96, 0), (10, 38, 2, 90, 0), (12, 36, 2, 92, 0),
           (14, 33, 2, 100, 0)]]
B_SOLO = [[(0, 77, 2, 96, 0), (2, 75, 2, 88, 0), (4, 72, 2, 90, 0), (6, 68, 2, 86, 0), (8, 70, 4, 92, 0),
           (12, 72, 2, 88, 0), (14, 74, 2, 94, 0)],
          [(0, 77, 2, 94, 0), (2, 79, 2, 96, 0), (4, 77, 2, 88, 0), (6, 74, 2, 90, 0), (8, 70, 4, 92, 0),
           (12, 72, 2, 88, 0), (14, 75, 2, 94, 0)],
          [(0, 74, 2, 92, 0), (2, 75, 2, 88, 0), (4, 79, 4, 98, 0), (8, 77, 2, 90, 0), (10, 75, 2, 88, 0),
           (12, 74, 2, 90, 0), (14, 72, 2, 92, 0)],
          [(0, 69, 1, 90, 0), (1, 72, 1, 86, 0), (2, 74, 1, 88, 0), (3, 75, 1, 90, 0), (4, 77, 2, 96, 0),
           (6, 79, 2, 98, 0), (8, 81, 4, 106, 0), (12, 79, 2, 94, 0), (14, 75, 2, 96, 0)]]
I_EP = [[(0, 6, 88, "BBM9"), (6, 2, 74, "BBM9"), (10, 6, 80, "BBM9")],
        [(0, 6, 88, "AM11"), (6, 2, 74, "AM11"), (10, 6, 80, "AM11")],
        [(0, 4, 92, "GM9"), (6, 2, 80, "GM9"), (10, 3, 84, "GM9"), (14, 2, 88, "C13")],
        [(3, 2, 84, "C13"), (6, 2, 82, "C13"), (10, 3, 86, "C13"), (14, 2, 90, "C13")]]
I_BASS = [[(0, 31, 3, 108, 1), (3, 43, 1, 96, 1), (6, 38, 2, 90, 0), (8, 31, 2, 94, 0), (10, 34, 1, 88, 0),
           (11, 38, 1, 90, 0), (14, 36, 2, 104, 1)],
          [(2, 36, 1, 64, 0), (3, 48, 1, 98, 1), (6, 31, 2, 90, 0), (8, 36, 2, 94, 0), (10, 40, 1, 88, 0),
           (11, 43, 1, 90, 0), (12, 34, 2, 92, 0), (14, 36, 2, 98, 0)]]
I_PICKUP = [(8, 67, 2, 86, 0), (12, 72, 2, 92, 0), (14, 70, 2, 90, 0)]

song = Song("Japanese fusion, 2026-10-21 (hand-written)", bpm=90, swing=12, duck=0, drlvl=116)


def sounds(s, kit):
    """FM BASS, TINE EP, G-FUNK LD and the mix, as every section of it had them"""
    s.bass.sound("DIGITAL", "FM BASS").set(gate=80, level=96)
    s.keys.sound("FM6", "TINE EP").set(gate=90, level=96, tflt=28, rev=32, chor=34, pan=-12)
    s.lead.sound("ANALOG", "G-FUNK LD").set(gate=94, level=100, tflt=10, dly=30, rev=40, pan=10)
    s.drums.use(kit)


def tones(part, bar, table, octave=0):
    for at, n, ln, vel, acc in table:
        part.note(bar * 16 + at, [n, n + octave] if octave else n, ln, vel, accent=bool(acc))


def comp(part, bar, table):
    for at, ln, vel, ch in table:
        part.note(bar * 16 + at, CH[ch], ln, vel, accent=vel >= 100)


def hats(d, bar, a, b):
    for i in range(a, b):
        d.hit(bar * 16 + i, "hat", "x" if i % 4 == 0 else "g" if i % 2 else "s")


def fill(d, bar):
    d.hit(bar * 16 + 12, "snare", "x").hit(bar * 16 + 13, "htom", "s").hit(bar * 16 + 14, "htom", "x")
    d.hit(bar * 16 + 15, "ltom", "x")


# ---- VERSE (A)
a = song.section("A", "verse")
sounds(a, "ACOUSTIC")
a.keys.set(level=92)
a.lead.set(level=94)
for b in range(4):
    tones(a.bass, b, V_BASS[b])
    comp(a.keys, b, V_EP[b])
    tones(a.lead, b, V_THEME[b])
for b in range(4):
    hats(a.drums, b, 0, 12 if b == 3 else 16)
    a.drums.hit(b * 16, "kick", "X" if b == 0 else "x").hit(b * 16 + 10, "kick", "x")
    a.drums.hit(b * 16 + (6 if b == 3 else 14), "kick", "s")
    if b & 1:
        a.drums.hit(b * 16 + 4, "rim", "x").hit(b * 16 + 8, "rim", "x")
    else:
        a.drums.hit(b * 16, "rim", "x").hit(b * 16 + 6, "rim", "x").hit(b * 16 + 12, "rim", "x")
    if b == 1:
        a.drums.hit(b * 16 + 6, "open", "s")
a.drums.hit(0, "crash", "s")
fill(a.drums, 3)
a.chords((0, "Fmaj9"), (16, "Bbmaj9"), (32, "Fmaj9"), (48, "Gm9"), (54, "C13"))

# ---- CHORUS (B)
c = song.section("B", "chorus")
sounds(c, "ACOUSTIC")
c.bass.set(level=100)
c.keys.set(level=100)
c.lead.set(level=104)
for b in range(4):
    tones(c.bass, b, C_BASS[b])
    comp(c.keys, b, C_EP[b])
    tones(c.lead, b, C_HOOK[b], octave=12)
for b in range(4):
    hats(c.drums, b, 0, 12 if b == 3 else 16)
    c.drums.hit(b * 16, "kick", "X" if b == 0 else "x").hit(b * 16 + 10, "kick", "x")
    c.drums.hit(b * 16 + (14 if b == 0 else 6), "kick", "s")
    for i in (4, 12):
        c.drums.hit(b * 16 + i, "snare", "x").nudge(b * 16 + i, 3)
    c.drums.hit(b * 16 + 15, "snare", "x" if b == 3 else "g")
    if b & 1:
        c.drums.hit(b * 16 + 14, "open", "s")
c.drums.hit(0, "crash", "x")
fill(c.drums, 3)
c.chords((0, "Bbmaj9"), (14, "C9sus4"), (22, "C9"), (30, "Am11"), (46, "Dm9"), (54, "Gm9/C"))

# ---- BRIDGE (C)
br = song.section("C", "bridge")
sounds(br, "ACOUSTIC")
br.bass.set(level=88)
br.keys.set(level=90, rev=44)
br.lead.set(level=88, rev=48)
for b in range(4):
    tones(br.bass, b, B_BASS[b])
    comp(br.keys, b, B_EP[b])
    tones(br.lead, b, B_SOLO[b])
for b in range(4):
    for i in range(0, 8 if b == 3 else 16, 2):
        br.drums.hit(b * 16 + i, "ride", "s" if i % 4 == 0 else "g")
    br.drums.hit(b * 16 + 4, "pedal", "s").hit(b * 16 + 12, "pedal", "s")
    br.drums.hit(b * 16 + 8, "rim", "x").hit(b * 16 + 0, "kick", "s")
for i in range(8, 12):
    br.drums.hit(48 + i, "snare", "s" if i == 8 else "g")
fill(br.drums, 3)
br.drums.hit(60, "kick", "x")

# ---- INTRO (D)
i_ = song.section("D", "intro")
sounds(i_, "DUST")
i_.keys.set(level=90)
for b in range(2, 4):
    tones(i_.bass, b, I_BASS[b - 2])
for b in range(4):
    comp(i_.keys, b, I_EP[b])
tones(i_.lead, 3, I_PICKUP)
d = i_.drums
for b in range(2):
    for i in range(0, 16, 2):
        d.hit(b * 16 + i, "hat", "s" if i % 4 == 0 else "g")
hats(d, 2, 0, 16)
hats(d, 3, 0, 12)
d.hit(16 + 12, "rim", "s")
for b in range(2, 4):
    d.hit(b * 16, "kick", "x").hit(b * 16 + 10, "kick", "x")
d.hit(32, "rim", "s").hit(32 + 6, "rim", "s").hit(32 + 12, "rim", "s").hit(48 + 4, "rim", "s").hit(48 + 8, "rim", "s")
fill(d, 3)

song.order(("D", 4), ("A", 8), ("B", 8), ("A", 8), ("C", 4), ("B", 8), stop_at="A")

if __name__ == "__main__":
    song.main()
