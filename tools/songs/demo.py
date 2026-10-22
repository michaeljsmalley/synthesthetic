#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Play page's song: 80s dance pop in D minor, 116 BPM, straight sixteenths, after the records Quincy Jones made
with Michael Jackson in the Thriller and Bad years: a synth bass riff holding the tonic while the chords move above
it, brass hits where they land hardest, claps on the backbeat, a drum machine's four on the floor in the chorus. The
band: ROUND BASS and TINE EP (FM6), SYN BRASS and DARK STR (ANALOG), the 80S kit (no snare, no congas, no cowbell).

    INTRO 4       the chorus's loop: a brass hit on the first beat
    VERSE 8       Dm9 Dm9 G9 G9 over the bass's riff on D (dorian: the IV a major chord), the strings holding them
    PRE 4         Gm7 Am7 Bbmaj7 A7sus4-A7: the bass climbs in eighths under the strings, a brass hit on every
                  chord, the claps roll
    CHORUS 8      Dm7 C Bb A7: down to the dominant (the Andalusian descent), the piano's riff with the bass
    VERSE 8, PRE 4, CHORUS 8
    BREAKDOWN 8   the bass's riff under the strings, the claps and a rim pattern, brass hits; four on the floor
                  into the last chorus
    CHORUS 8

Every part is a 4-bar loop of sixteenths, played twice in an 8-bar section, as a drum machine and a sequencer would.
The brass only hits: the top of a section, a chord change, the band's hits at the end of a phrase. Its voicings are
written out an octave above the piano (the bright register of a JV's brass stabs), each chord doubled at the octave on
top, as a horn section voices its hits. The piano's track plays the strings' pad (DARK STR) everywhere but the
chorus, where it plays its riff. The three parts share 8 voices (voice.c): the brass's hits take 4, so the strings
and the piano play 3."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from songlib import Song, note  # noqa: E402

song = Song("Synthesthetic demo", bpm=116, swing=0, duck=0, dtime=0, dfdbk=34)
song.check_lead = False                           # (no melody: track 3 is the brass)

# the brass's chords, voiced by hand
DM7 = ["A4", "D5", "F5", "A5"]
C = ["G4", "C5", "E5", "G5"]
BB = ["F4", "Bb4", "D5", "F5"]
A7 = ["E4", "G4", "C#5", "E5"]
G9 = ["B4", "D5", "F5", "A5"]


STRINGS = ("ANALOG", "DARK STR")


def band(s, keys=("FM6", "TINE EP"), keys_level=86, keys_pan=-12, bass_level=96, brass_level=94):
    """the band's sounds and mix (each section restates them; P_LEVEL steps are half a dB). Every part on 1/16 steps:
    4 bars. keys: the piano, or the strings in its place"""
    s.bass.steps("1/16").sound("FM6", "ROUND BASS").set(level=bass_level, gate=96, swing=0)
    s.keys.steps("1/16").sound(*keys).set(level=keys_level, gate=100, tflt=24, pan=keys_pan, swing=0)
    s.lead.steps("1/16").sound("ANALOG", "SYN BRASS").set(level=brass_level, gate=100, tflt=24, pan=0, chor=24,
                                                           swing=0)
    s.drums.use("80S").set(swing=0)


def bass(s, line):
    for at, n, ln, vel in line:
        s.bass.note(at, note(n), ln, vel, accent=vel >= 120)


def brass(s, hits):
    for at, chord, ln, vel in hits:
        s.lead.note(at, [note(n) for n in chord], ln, vel, accent=vel >= 120)


def strings(s, chords):
    """the strings' pad: (at, length, chord symbol), three notes (the brass's hits take four of the 8 voices)"""
    for at, ln, sym in chords:
        s.keys.chord(at, sym, ln, 90, lo="D3", hi="A4", centre="F4", n=3)


def comp(s, hits, **kw):
    """the piano: (at, length, chord symbol, velocity), voiced near the last chord"""
    for at, ln, sym, vel in hits:
        s.keys.chord(at, sym, ln, vel, **kw)


def drums(s, pats):
    for lane, bars in pats.items():
        for b in range(4):
            s.drums.pattern(lane, bars[b % len(bars)], at=b * 16)


# ======================================================================================================== VERSE (A)
# the bass's riff on D, a bar long: the root on 1 and its "a", the octave popping on the "and" of 2, the root on 3,
# then the minor pentatonic up (F on the "a" of 3, G, A) and the b7 below to come home; the 4th bar's second half runs
# up the scale and in by half steps (C, C#, D). The chords move over it: Dm9, then G9 (D dorian's major IV)
RIFF = [(0, "D2", 2, 120), (3, "D2", 1, 92), (6, "D3", 1, 108), (7, "D2", 1, 88), (8, "D2", 2, 112), (11, "F2", 1, 96),
        (13, "G2", 1, 100), (14, "A2", 1, 104), (15, "C2", 1, 96)]
RIFF_LAST = [(0, "D2", 2, 120), (3, "D2", 1, 92), (6, "D3", 1, 108), (7, "D2", 1, 88), (8, "D2", 1, 112),
             (10, "F2", 1, 96), (11, "G2", 1, 100), (12, "A2", 1, 108), (14, "C2", 1, 100), (15, "C#2", 1, 104)]
RIFF_4 = [(b * 16 + at, n, ln, vel) for b in range(3) for at, n, ln, vel in RIFF] + \
         [(48 + at, n, ln, vel) for at, n, ln, vel in RIFF_LAST]
v = song.section("A", "verse")
v.chords((0, "Dm9"), (32, "G9"))
band(v, keys=STRINGS, keys_level=85, keys_pan=0, bass_level=92, brass_level=87)
v.drums.set(level=76, rev=16)
bass(v, RIFF_4)
# the strings hold the chords, two bars each
strings(v, [(0, 32, "Dm9"), (32, 32, "G9")])
# the brass hits the end of each 4-bar phrase: the "and" of 3 and beat 4, over the riff's F and A (off before its C)
brass(v, [(58, G9, 1, 110), (60, G9, 2, 120)])
# the kit: kick on 1, the "and" of 2 and 3 (with the bass), claps on 2 and 4, eighth-note hats, a shaker in
# sixteenths, a rim click on the "a" of 3 every second bar, the open hat lifting bar 4's last "and"
drums(v, {"kick": ["x.....x.x.......", "x.....x.x.....x.", "x.....x.x.......", "x.....x.x...x..."],
          "clap": ["....x.......x..."],
          "hat": ["x.s.x.s.x.s.x.s.", "x.s.x.s.x.s.x.s.", "x.s.x.s.x.s.x.s.", "x.s.x.s.x.s.x..."],
          "open": ["................", "................", "................", "..............s."],
          "rim": ["................", "...........s....", "................", "...........s...."],
          "shaker": ["gsgsgsgsgsgsgsgs"]})

# ===================================================================================================== PRE-CHORUS (C)
c = song.section("C", "pre-chorus")
c.chords((0, "Gm7"), (16, "Am7"), (32, "Bbmaj7"), (48, "A7sus4"), (56, "A7"))
band(c, keys=STRINGS, keys_level=85, keys_pan=0, bass_level=92, brass_level=89)
c.drums.set(level=82, rev=18)
# the bass climbs in eighths, G A Bb, and falls back to the dominant, the leading tone under the last "a"
bass(c, [(b * 16 + k * 2, r, 1, 112 if k % 2 == 0 else 92) for b, r in enumerate(("G2", "A2", "Bb2")) for k in range(8)] +
        [(48 + k * 2, "A2", 1, 112 if k % 2 == 0 else 92) for k in range(7)] + [(63, "C#2", 1, 104)])
# the strings a chord a bar, the sus4 resolving on beat 3 of the last
strings(c, [(0, 16, "Gm7"), (16, 16, "Am7"), (32, 16, "Bbmaj7"), (48, 8, "A7sus4"), (56, 8, "A7")])
# the brass hits each chord as it comes, harder each bar, the top climbing G A Bb and settling on A; the sus4
# resolving on beat 3 of the last bar
brass(c, [(0, ["Bb4", "D5", "F5", "G5"], 2, 100), (16, ["C5", "E5", "G5", "A5"], 2, 108),
          (32, ["D5", "F5", "A5", "Bb5"], 2, 116), (48, ["D5", "E5", "G5", "A5"], 2, 120),
          (56, ["C#5", "E5", "G5", "A5"], 2, 124)])
# the kit builds: four on the floor, claps on 2 and 4, sixteenth hats, the claps rolling through the last two beats
drums(c, {"kick": ["x...x...x...x..."],
          "clap": ["....x.......x...", "....x.......x...", "....x.......x...", "....x...sxsxxXXX"],
          "hat": ["xsxsxsxsxsxsxsxs", "xsxsxsxsxsxsxsxs", "xsxsxsxsxsxsxs.s", "xsxsxsxs........"],
          "open": ["................", "................", "..............s.", "................"],
          "shaker": ["gsgsgsgsgsgsgsgs"]})

# ========================================================================================================= CHORUS (B)
CHORDS = [("D", DM7, "Dm9"), ("C", C, "Cadd9"), ("Bb", BB, "Bbmaj7"), ("A", A7, "A7b9")]
ROOTS = {"D": ("D2", "D3"), "C": ("C2", "C3"), "Bb": ("Bb1", "Bb2"), "A": ("A1", "A2")}
APPROACH = {"D": "C#2", "C": "B1", "Bb": "Bb1", "A": "C#2"}     # (the last "a": into the next root by a half step)
ch = song.section("B", "chorus")
ch.chords(*[(b * 16, sym) for b, (_, _, sym) in enumerate(CHORDS)])
band(ch, keys_level=90, bass_level=94, brass_level=95)
ch.drums.set(level=88, rev=22)
# the bass: the root on 1 and its "a" and the octave on the "and" of 2, in unison with the piano; then its own
# second half (the root on 3, the octave, the root, the root again) and the half step into the next chord
bass(ch, [(b * 16 + at, n, ln, vel) for b, (r, _, _) in enumerate(CHORDS)
          for at, n, ln, vel in ((0, ROOTS[r][0], 2, 120), (3, ROOTS[r][0], 1, 96), (6, ROOTS[r][1], 1, 108),
                                 (8, ROOTS[r][0], 2, 116), (10, ROOTS[r][1], 1, 100), (11, ROOTS[r][0], 1, 92),
                                 (14, ROOTS[r][0], 1, 104), (15, APPROACH[r], 1, 100))])
# the piano's riff: the chord on 1, its "a", the "and" of 2 and the "and" of 3 (three notes: the brass's hits take
# four of the 8 voices)
comp(ch, [(b * 16 + at, ln, sym, vel) for b, (_, _, sym) in enumerate(CHORDS)
          for at, ln, vel in ((0, 2, 96), (3, 1, 84), (6, 2, 92), (10, 2, 86))], lo="D3", hi="A4", centre="F4", n=3)
# the brass hits: the top, the Bb halfway down, and the band's hits on the A7 (1, its "a", the "and" of 2, with the
# bass and the piano) before the toms' fill
brass(ch, [(0, DM7, 2, 124), (32, BB, 2, 116), (48, A7, 1, 118), (51, A7, 1, 108), (54, A7, 2, 124)])
# the kit: four on the floor, claps on 2 and 4, sixteenth hats and shaker, the open hat on bar 2's last "and", the
# crash on the top, the fill down the toms
drums(ch, {"kick": ["x...x...x...x..."],
           "clap": ["....X.......X..."],
           "hat": ["xsxsxsxsxsxsxsxs", "xsxsxsxsxsxsxs.s", "xsxsxsxsxsxsxsxs", "xsxsxsxsxsxs...."],
           "open": ["................", "..............s.", "................", "................"],
           "shaker": ["sxsxsxsxsxsxsxsx"]})
ch.drums.hit(0, "crash", "x")
ch.drums.hit(60, "htom", "x").hit(61, "htom", "x").hit(62, "ltom", "x").hit(63, "ltom", "X")

# ====================================================================================================== BREAKDOWN (D)
bd = song.section("D", "breakdown")
bd.chords((0, "Dm9"), (56, "A7"))
band(bd, keys=STRINGS, keys_level=85, keys_pan=0, bass_level=94, brass_level=91)
bd.drums.set(level=84, rev=26)
# the strings hold Dm9 under the riff, A7 with the last hits
strings(bd, [(0, 56, "Dm9"), (56, 8, "A7")])
# the bass's riff, its last bar's second half on A with the brass's hits (the leading tone after them)
bass(bd, RIFF_4[:-len(RIFF_LAST)] +
     [(48 + at, n, ln, vel) for at, n, ln, vel in RIFF_LAST if at < 8] +
     [(56, "A1", 1, 112), (58, "A1", 1, 116), (60, "A1", 1, 120), (62, "A1", 1, 124), (63, "C#2", 1, 104)])
# the brass hits the top of bars 1 and 3, then four stabs on the dominant with the bass into the next bar
brass(bd, [(0, DM7, 2, 124), (32, DM7, 2, 124), (56, A7, 1, 104), (58, A7, 1, 110), (60, A7, 1, 116),
           (62, A7, 2, 124)])
# the kit stripped down: kick on 1 and 3, claps on 2 and 4, a rim pattern across the bar, soft eighth hats; the last
# bar four on the floor and the claps doubling into the next
drums(bd, {"kick": ["x.......x.......", "x.......x.......", "x.......x.......", "x...x...x...x..."],
           "clap": ["....x.......x...", "....x.......x...", "....x.......x...", "....x.......xsxX"],
           "rim": ["...x..x....x..x."],
           "hat": ["s.s.s.s.s.s.s.s."]})

# (STOP brings the verse back: the loop a visitor plays along with)
song.order(("B", 4), ("A", 8), ("C", 4), ("B", 8), ("A", 8), ("C", 4), ("B", 8), ("D", 8), ("B", 8), stop_at="A")
if __name__ == "__main__":
    song.main()
