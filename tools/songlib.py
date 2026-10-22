#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Songs for the Play page (firmware/host/web.c): the first visit's song is a Python file in tools/songs/ written with
this module, which checks it against the FM-1's sequencer and writes the C tables web.c loads (tools/build_web.py
--song), a MIDI file of it (to try the parts in another program) and a report (ranges, density, the lead against
the chords).

    from songlib import Song
    song = Song("Example", bpm=106)
    a = song.section("A", "verse")
    a.bass.sound("DIGITAL", "FM BASS").set(level=96)
    a.bass.note(0, "F1", 3, vel=110)                       # at and length in sixteenths from the section's start
    a.keys.chord(0, "Fmaj9", 4)                            # voiced near the keys' last chord
    a.drums.pattern("kick", "x... ..x. x... ....")        # a character a sixteenth: . g s x X (ghost soft norm hard)
    song.order(("A", 8))
    song.main()

Every section is a project slot of the FM-1 (A..D, as SAVE + a key stores them); each track holds up to 64 steps.
On 1/16 steps that is 4 bars; a part on 1/8 steps (part.steps("1/8")) holds 8 bars, and a note on an odd sixteenth
goes on the next step nudged a sixteenth early (or the step before, nudged late). A section starts from the
power-on track settings, then its sound (the preset's), then what set() says."""
import argparse
import copy
import itertools
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE_H = ROOT / "firmware" / "src" / "core.h"


def _enum(comment, last):
    """the members of a core.h enum, in order (P_* or G_*)"""
    text = CORE_H.read_text(encoding="utf-8")
    m = re.search(r"enum\s*\{\s*/\*\s*" + re.escape(comment) + r"\s*\*/(.*?)\b" + last + r"\b", text, re.S)
    if not m:
        raise SystemExit(f"songlib: the enum of {comment} is not where songlib.py looks in firmware/src/core.h")
    body = re.sub(r"/\*.*?\*/", " ", m.group(1), flags=re.S)
    return {n: i for i, n in enumerate(re.findall(r"\b([PG]_[A-Z0-9_]+)\b", body))}


P = _enum("per-track parameters", "P_COUNT")
G = _enum("global parameters", "G_COUNT")


def lockable(p):
    """seq.c p_lockable: the parameters a step can lock"""
    i = P[p]
    return (i <= P["P_LD_AMP"] or p in ("P_SGATE", "P_GLIDE", "P_PAN", "P_DETUNE", "P_TFLT") or
            P["P_DIST"] <= i <= P["P_REV"] or P["P_SLCR"] <= i <= P["P_SLDEPTH"] or P["P_E0"] <= i <= P["P_E7"] or
            P["P_IA"] <= i <= P["P_IMIX"])


# the engines and their presets, in the FM-1's order (engines.c ENGINES, each engine's PRESETS)
ENGINES = [
    ("ANALOG", ["808 BOOM", "808 DIRTY", "SUB BASS", "808 SLIDE", "ACID 303", "PLUGG BASS", "REESE", "WOBBLE",
                "FUNK BASS", "G-FUNK LD", "TRAP PLUCK", "SYN BRASS", "SUPERSAW", "WARM PAD", "DARK STR", "ATMOS PAD",
                "PAN FLUTE", "FAT LEAD", "MONO BASS", "PWM LEAD", "SAW BASS", "SINE PAD"]),
    ("DIGITAL", ["RHODES", "DX RHODES", "WURLI", "CLAV", "M1 PIANO", "AFRO KEYS", "TRAP BELL", "MUSIC BOX", "MARIMBA",
                 "KALIMBA", "PLUGG BELL", "GLASS PAD", "FM BASS", "STEEL DRUM", "GLOCKEN", "CELESTA", "XYLOPHONE",
                 "HARP", "BASS GTR", "TUBE BELL", "FM PLUCK", "FM GRAND"]),
    ("PHASE", ["CZ BASS", "SOFT KEYS", "CZ STRING", "RESO PLUCK", "CZ BRASS", "CZ LEAD", "CZ PAD", "CZ RESO"]),
    ("LOFI", ["GAME LEAD", "GB BASS", "8BIT ARP", "NES BASS", "CHIP CHORD", "1BIT BEEP", "WAVE LEAD", "CHIP NOISE"]),
    ("SAMPLE", ["UP BASS", "VIBES", "HORN STAB", "STRING STB", "SCRATCH", "GM KIT", "DEEP BASS"]),
    ("VOICE", ["CHOIR AAH", "SOUL OOH", "TALKBOX", "WOW BASS", "HARMONICA", "ROBOT", "VOX PAD"]),
    ("TRIO", ["FAT BASS", "MIN STAB", "MIN7 STAB", "RAVE STAB", "DUB CHORD", "SAW PAD", "SYNC LEAD", "HOOVER",
              "ACCORDION", "ENSEMBLE", "80S POLY", "SQR LEAD", "POWER STAB", "PULSE PAD"]),
    ("WHEEL", ["SOUL ORGAN", "GOSPEL", "JAZZ ORGAN", "DIRTY B3", "HOUSE ORGN", "ROCK ORGAN", "CHAPEL", "REGGAE ORG"]),
    ("GRAIN", ["LOFI CLOUD", "VIBE HAZE", "STR DUST", "STR CLOUD", "SHIMMER", "DRONE", "STR FREEZE", "HORN CLOUD"]),
    ("FM6", ["TINE EP", "GLASS BELL", "ROUND BASS", "BRASS SECT", "SOFT PAD", "WOOD BARS", "DRAWBARS", "NYLON PICK"]),
    ("PHYS", ["BELL TREE", "MODAL BAR", "STR PLUCK", "BOWED MTL", "THUMB PNO", "HAND DRUM", "MEMB TOMS", "SITAR",
              "PHYS HARP", "NYLON GTR", "STEEL GTR", "MUTED GTR", "PLUCK BASS", "KOTO", "BANJO", "CELLO BOW",
              "TANPURA", "CHIMES", "BIG BELL", "VIBRA BAR", "GLASS BOWL", "STEEL PAN", "WOOD BLOCK", "TABLA", "CONGA",
              "TIMPANI"]),
    ("NOISE", ["WIND", "RAIN", "NZ ARCADE", "NZ METAL", "OCEAN", "VINYL", "HISS", "RISER", "NZ SNARE", "BITCRUSH",
               "RADIO"]),
]
# the drum kits (drums.c DRUM_KIT_NAMES: the sampled five, then the synthesized ones)
KITS = ["ACOUSTIC", "DEEP", "TIGHT", "BRIGHT", "DUST", "808", "909", "606", "80S", "VINTAGE", "TRAP", "DRILL", "BOOMBAP",
        "LO-FI", "PHONK", "HOUSE", "D.HOUSE", "TECHNO", "MINIMAL", "ELECTRO", "DISCO", "GARAGE", "JUNGLE", "DUBSTEP",
        "DEMBOW", "AMAPIANO", "AFRO", "LATIN", "TRIBAL", "SYNTHWV", "CHIP", "ARCADE", "GLITCH", "INDUSTR", "HYPER",
        "AMBIENT", "JAZZ"]
# the drum track's 16 sounds by their General MIDI notes (drums.c LANE_NOTE)
LANES = {"kick": 36, "kick2": 35, "snare": 38, "clap": 39, "hat": 42, "open": 46, "pedal": 44, "rim": 37,
         "snare2": 40, "ltom": 43, "htom": 48, "crash": 49, "ride": 51, "shaker": 70, "conga": 63, "cowbell": 56}
LANE_ORDER = ["kick", "kick2", "snare", "clap", "hat", "open", "pedal", "rim", "snare2", "ltom", "htom", "crash",
              "ride", "shaker", "conga", "cowbell"]
LEVELS = {"g": "LV_GHOST", "s": "LV_SOFT", "x": "LV_NORM", "X": "LV_HARD"}
LEVEL_VEL = {"LV_GHOST": 42, "LV_SOFT": 72, "LV_NORM": 100, "LV_HARD": 127}       # drums.c lvl_vel
DIVS = {"1/16": ("2", 1), "1/8": ("1", 2)}                                        # P_SDIV (params.c N_SDIV), 16ths a step
VOICE_MODES = {"poly": "V_POLY", "mono": "V_MONO", "legato": "V_LEGATO", "unison": "V_UNISON"}
SHORT = {"level": "P_LEVEL", "pan": "P_PAN", "rev": "P_REV", "dly": "P_DLY", "chor": "P_CHOR", "dist": "P_DIST",
         "tflt": "P_TFLT", "gate": "P_SGATE", "swing": "P_SSWING", "glide": "P_GLIDE", "voice": "P_VOICE",
         "detune": "P_DETUNE", "atk": "P_ATK", "dec": "P_DEC", "sus": "P_SUS", "rel": "P_REL", "strum": "P_STRUM",
         "ed_flt": "P_ED_FLT", "ed_pit": "P_ED_PIT", "lrate": "P_LRATE", "ld_pit": "P_LD_PIT", "ld_flt": "P_LD_FLT",
         "lwave": "P_LWAVE", "lfade": "P_LFADE", "lphase": "P_LPHASE", "ld_amp": "P_LD_AMP",
         "glmode": "P_GLMODE", **{f"e{i}": f"P_E{i}" for i in range(8)}}
GSHORT = {"bpm": "G_BPM", "swing": "G_SWING", "duck": "G_DUCK", "dtime": "G_DTIME", "dfdbk": "G_DFDBK",
          "dcolor": "G_DCOLOR", "dmix": "G_DMIX", "rsize": "G_RSIZE", "rdamp": "G_RDAMP", "crate": "G_CRATE",
          "cdepth": "G_CDEPTH", "dust": "G_DUST", "filt": "G_FILT", "drlvl": "G_DRLVL", "drrev": "G_DRREV",
          "drdly": "G_DRDLY"}

# ---- pitch
_PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]


def pc(s):
    """a pitch class from a name (C, F#, Bb)"""
    return (_PC[s[0].upper()] + s[1:].count("#") - s[1:].count("b")) % 12


def note(x):
    """a MIDI note from a name (C4 = 60, Bb2, F#3) or a number"""
    if isinstance(x, int):
        return x
    m = re.fullmatch(r"([A-Ga-g])([#b]*)(-?\d)", x.strip())
    if not m:
        raise ValueError(f"not a note: {x!r}")
    return 12 * (int(m.group(3)) + 1) + _PC[m.group(1).upper()] + m.group(2).count("#") - m.group(2).count("b")


def name(n):
    return f"{NAMES[n % 12]}{n // 12 - 1}"


# ---- chords: the intervals of each quality above the root (tensions written above 12)
QUALITIES = {
    "": (0, 4, 7), "m": (0, 3, 7), "dim": (0, 3, 6), "aug": (0, 4, 8), "sus4": (0, 5, 7), "sus2": (0, 2, 7),
    "6": (0, 4, 7, 9), "m6": (0, 3, 7, 9), "6/9": (0, 4, 7, 9, 14), "m6/9": (0, 3, 7, 9, 14),
    "maj7": (0, 4, 7, 11), "7": (0, 4, 7, 10), "m7": (0, 3, 7, 10), "m7b5": (0, 3, 6, 10), "dim7": (0, 3, 6, 9),
    "mmaj7": (0, 3, 7, 11), "add9": (0, 4, 7, 14), "madd9": (0, 3, 7, 14),
    "maj9": (0, 4, 7, 11, 14), "9": (0, 4, 7, 10, 14), "m9": (0, 3, 7, 10, 14), "mmaj9": (0, 3, 7, 11, 14),
    "11": (0, 7, 10, 14, 17), "m11": (0, 3, 7, 10, 14, 17), "maj7#11": (0, 4, 7, 11, 18), "maj9#11": (0, 4, 7, 11, 14, 18),
    "13": (0, 4, 7, 10, 14, 21), "m13": (0, 3, 7, 10, 14, 21), "maj13": (0, 4, 7, 11, 14, 21),
    "7sus4": (0, 5, 7, 10), "9sus4": (0, 5, 7, 10, 14), "13sus4": (0, 5, 7, 10, 14, 21),
    "7b9": (0, 4, 7, 10, 13), "7#9": (0, 4, 7, 10, 15), "7b13": (0, 4, 7, 10, 20), "7#5": (0, 4, 8, 10),
    "7#11": (0, 4, 7, 10, 18), "9#11": (0, 4, 7, 10, 14, 18), "13b9": (0, 4, 7, 10, 13, 21), "7alt": (0, 4, 10, 13, 15, 20),
    "7b9b13": (0, 4, 7, 10, 13, 20), "m7b9": (0, 3, 7, 10, 13), "maj7#5": (0, 4, 8, 11),
}


def parse_chord(sym):
    """(root pc, intervals, slash bass pc or None) of a symbol: Fmaj9, Bb13, C9sus4, Dm9/G, G7b9"""
    m = re.fullmatch(r"([A-G][#b]?)(.*?)(?:/([A-G][#b]?))?", sym.strip())
    if not m or m.group(2) not in QUALITIES:
        raise ValueError(f"not a chord songlib knows: {sym!r}")
    return pc(m.group(1)), QUALITIES[m.group(2)], pc(m.group(3)) if m.group(3) else None


def chord_tones(sym):
    root, iv, bass = parse_chord(sym)
    return {(root + i) % 12 for i in iv} | ({bass} if bass is not None else set())


def _role(i):
    """how much a chord member matters to the chord's color: the 3rd (or the sus note) and the 7th (or the 6th), then
    the tensions, then the 5th, then the root (the bass plays it)"""
    if i in (3, 4):
        return 0
    if i in (2, 5):
        return 0.5
    if i in (9, 10, 11):
        return 1
    if i >= 12:
        return 2
    if i in (6, 7, 8):
        return 3
    return 4


def voicing(sym, lo="F3", hi="F5", prev=None, n=4, top=None, span=14, centre="Eb4"):
    """n notes of the chord between lo and hi, the most colorful members first (rootless when there are enough), the
    placement that moves least from the previous voicing (each voice to its nearest), the top note top if asked.
    The rootless forms (3 5 7 9, 7 9 3 5) keep their inner half steps (the m9's 9 under its 3rd, the 13 under the
    b7: their color); a minor ninth between any two voices (but a b9 the symbol asks for) costs, and a half step
    between the top two (it sits exposed)"""
    root, iv, _ = parse_chord(sym)
    b9 = 13 in iv
    lo, hi = note(lo), note(hi)
    members = sorted(iv, key=_role)
    pcs = []
    for i in members:
        p = (root + i) % 12
        if p not in pcs:
            pcs.append(p)
    pcs = pcs[:n]
    cands = [[x for x in range(lo, hi + 1) if x % 12 == p] for p in pcs]
    if any(not c for c in cands):
        raise ValueError(f"{sym}: a member has no place between {name(lo)} and {name(hi)}")
    best, best_score = None, None
    want_top = note(top) if top is not None else None
    centre = note(centre)
    for combo in itertools.product(*cands):
        v = sorted(combo)
        if v[-1] - v[0] > span:
            continue
        if v[0] < 52 and len(v) > 1 and v[1] - v[0] < 3:   # (no second at the bottom of a low voicing: mud)
            continue
        score = abs(sum(v) / len(v) - centre) * 0.25
        score += 4 if len(v) > 1 and v[-1] - v[-2] == 1 else 0
        score += 0 if b9 else 6 * sum(1 for x, y in itertools.combinations(v, 2) if y - x == 13)
        if prev:
            pv = sorted(prev)
            if len(pv) == len(v):
                score += sum(abs(a - b) for a, b in zip(v, pv))
            else:
                score += sum(min(abs(a - b) for b in pv) for a in v)
        if want_top is not None:
            score += 6 * abs(v[-1] - want_top)
        if best_score is None or score < best_score:
            best, best_score = v, score
    if best is None:
        raise ValueError(f"{sym}: no voicing of {n} notes within {span} semitones between {name(lo)} and {name(hi)}")
    return best


# ---- the parts
def _params(kw, names=SHORT):
    out = {}
    for k, v in kw.items():
        p = names.get(k, k)
        if p not in P and p not in G:
            raise ValueError(f"no parameter {k!r}")
        if p == "P_VOICE" and isinstance(v, str):
            v = VOICE_MODES[v]
        if p == "P_SDIV":
            raise ValueError("set the steps with steps(), not P_SDIV")
        out[p] = v
    return out


class Ev:
    """a step: its notes, held steps, flags, velocity, per-note ratchets, nudge; where it sounds (sixteenths)"""
    def __init__(self, step, notes, steps, flags, vel, rat, micro, at, dur):
        self.step, self.notes, self.steps, self.flags, self.vel = step, notes, steps, flags, vel
        self.rat, self.micro, self.at, self.dur = rat, micro, at, dur

    @property
    def loud(self):
        """the velocity it plays at (seq.c step_vel: an accent plays 127)"""
        return 127 if "SF_ACCENT" in self.flags else self.vel


class Part:
    """a synth track of a section"""
    def __init__(self, section, k, role):
        self.sec, self.k, self.role = section, k, role
        self.engine = self.preset = None
        self.params = {}
        self.div, self.nsteps = "1/16", 64
        self.shuffle = 0                              # (a 1/8 part: the sixteenths' swing it places its odd ones at)
        self.ev = {}
        self.locks = {}
        self.last = None

    @property
    def s16(self):
        return DIVS[self.div][1]

    @property
    def span16(self):
        """the length of the part's loop, in sixteenths"""
        return self.nsteps * self.s16

    def sound(self, engine, preset):
        e = engine if isinstance(engine, int) else [x for x, _ in ENGINES].index(engine.upper())
        pr = preset if isinstance(preset, int) else ENGINES[e][1].index(preset.upper())
        self.engine, self.preset = e, pr
        return self

    def steps(self, div, n=64, shuffle=0):
        """the part's steps: 1/16 (64 of them: 4 bars) or 1/8 (8 bars). shuffle: on 1/8 steps, the swing (0..100, as
        SWG) of the sixteenths around it: an odd sixteenth goes where the swung one would be (66: on the triplet)"""
        self.shuffle = shuffle
        if div not in DIVS:
            raise ValueError(f"steps of {div}: songlib places notes on 1/16 or 1/8 steps")
        if not 1 <= n <= 64:
            raise ValueError("a track holds 1 to 64 steps")
        if self.ev:
            raise ValueError("set the steps before the notes")
        self.div, self.nsteps = div, n
        return self

    def set(self, **kw):
        self.params.update(_params(kw))
        return self

    def _place(self, at, push):
        """the step and nudge (1/64 of a step) of a note at `at` sixteenths, pushed `push` 64ths of a sixteenth"""
        s = self.s16
        if at % s == 0:
            step, micro = at // s, 0
        else:                                       # (1/8 steps, an odd sixteenth: the next step a sixteenth early,
            early = round(32 * (1 - self.shuffle / 200))   # less its swing; or, taken, the step before a sixteenth late)
            step, micro = (at + 1) // s, -early
            if step in self.ev or step >= self.nsteps:
                step, micro = (at - 1) // s, 31
                if self.shuffle:
                    raise ValueError(f"{self.sec.letter} {self.role} at {at}: a swung sixteenth needs the next step free")
        micro += round(push * (1 if s == 1 else 0.5))
        if not -32 <= micro <= 31:
            raise ValueError(f"{self.sec.letter} {self.role} at {at}: a nudge of {micro}/64 is past half a step")
        return step, micro

    def note(self, at, pitches, dur, vel=100, accent=False, slide=False, rat=1, push=0):
        """notes (one or up to four) at `at` sixteenths for `dur` sixteenths. push: a nudge in 64ths of a sixteenth
        (+ late). rat: hits in the step (1..4, every note)"""
        ps = [note(p) for p in (pitches if isinstance(pitches, (list, tuple)) else [pitches])]
        if not 1 <= len(ps) <= 4:
            raise ValueError("a step plays 1 to 4 notes")
        if any(not 0 <= p <= 127 for p in ps):
            raise ValueError(f"a note out of range: {ps}")
        step, micro = self._place(at, push)
        if step >= self.nsteps:
            raise ValueError(f"{self.sec.letter} {self.role} at {at}: past the part's {self.nsteps} steps of {self.div}")
        if step in self.ev:
            raise ValueError(f"{self.sec.letter} {self.role} at {at}: step {step} has a note already "
                             f"(at {self.ev[step].at})")
        end = at + dur
        steps = max(1, -(-(end - step * self.s16) // self.s16))     # (the steps it spans, from its own step)
        steps = min(steps, self.nsteps - step)
        flags = (["SF_ACCENT"] if accent else []) + (["SF_SLIDE"] if slide else [])
        r = 0
        for i in range(len(ps)):
            r |= (rat - 1) << (2 * i)
        self.ev[step] = Ev(step, ps, steps, flags, vel, r, micro, at, dur)
        for k in range(step + 1, step + steps):
            if k in self.ev:
                raise ValueError(f"{self.sec.letter} {self.role} at {at}: held over the note at {self.ev[k].at}")
        return self

    def chord(self, at, sym, dur, vel=92, lo="F3", hi="F5", n=4, top=None, accent=False, push=0, centre="Eb4"):
        v = voicing(sym, lo, hi, self.last, n, top, centre=centre)
        self.last = v
        return self.note(at, v, dur, vel, accent, push=push)

    def lock(self, at, param, value):
        p = SHORT.get(param, param)
        if not lockable(p):
            raise ValueError(f"{p} cannot be locked on a step")
        if at % self.s16:
            raise ValueError(f"a lock goes on a step: {at} is not on the {self.div} grid")
        self.locks[(at // self.s16, p)] = value
        if len(self.locks) > 24:
            raise ValueError(f"{self.sec.letter} {self.role}: more than 24 locks")
        return self

    def transposed(self, semis):
        c = copy.copy(self)
        c.ev = {k: copy.copy(e) for k, e in self.ev.items()}
        for e in c.ev.values():
            e.notes = [p + semis for p in e.notes]
        return c


class Drums:
    """the drum track of a section"""
    role = "drums"
    k = 3

    def __init__(self, section):
        self.sec = section
        self.kit = 0
        self.params = {}
        self.div, self.nsteps = "1/16", 64
        self.hits = {}                                # (step, lane) -> (level, ratchet)
        self.nudges = {}                              # step -> nudge (64ths of a step)
        self.locks = {}
        self.globals = {}                             # G_DRLVL, G_DRREV, G_DRDLY of this section
        self.tune = [0] * 16
        self.decay = [0] * 16

    @property
    def s16(self):
        return DIVS[self.div][1]

    @property
    def span16(self):
        return self.nsteps * self.s16

    def use(self, kit):
        self.kit = kit if isinstance(kit, int) else KITS.index(kit.upper())
        return self

    def steps(self, n=64, div="1/16"):
        """the drum track's steps: 1/16 (64: 4 bars) or 1/8 (64: 8 bars; ratchets make the sixteenths and
        thirty-seconds; a hit on an odd sixteenth takes the next step and nudges it a sixteenth early)"""
        if self.hits:
            raise ValueError("set the drums' steps before the hits")
        self.nsteps, self.div = n, div
        return self

    def set(self, level=None, rev=None, dly=None, **kw):
        """level / rev / dly: the drums' level and sends (GLO > DRUMS, a section keeps them); the rest the track's"""
        for g, v in (("G_DRLVL", level), ("G_DRREV", rev), ("G_DRDLY", dly)):
            if v is not None:
                self.globals[g] = v
        self.params.update(_params(kw))
        return self

    def hit(self, at, lane, lvl="x", rat=1):
        """a hit `at` sixteenths in (on 1/8 steps: an odd sixteenth nudges the next step a sixteenth early)"""
        if lane not in LANES:
            raise ValueError(f"no drum sound {lane!r} (one of {', '.join(LANES)})")
        step = -(-at // self.s16)
        if at % self.s16:
            if any(k[0] == step for k in self.hits) and self.nudges.get(step) != -32:
                raise ValueError(f"{self.sec.letter} drums: {lane} at {at} needs step {step} early, which has hits")
            self.nudges[step] = -32
        elif self.nudges.get(step) == -32:
            raise ValueError(f"{self.sec.letter} drums: {lane} at {at}: its step is nudged early for a push")
        if not 0 <= step < self.nsteps:
            raise ValueError(f"{self.sec.letter} drums: {lane} at {at} is past the {self.nsteps} steps")
        self.hits[(step, lane)] = (LEVELS[lvl] if lvl in LEVELS else lvl, rat)
        return self

    def pattern(self, lane, s, at=0, repeat=1):
        """a character a step from `at` sixteenths (a sixteenth on 1/16 steps, an eighth on 1/8; spaces and |
        ignored): . nothing, g ghost, s soft, x normal, X hard; repeat: the pattern again after itself"""
        cells = [c for c in s if c not in " |"]
        for r in range(repeat):
            for i, c in enumerate(cells):
                if c != ".":
                    self.hit(at + (r * len(cells) + i) * self.s16, lane, c)
        return self

    def clear(self, at, lane=None):
        step = at // self.s16
        for key in [k for k in self.hits if k[0] == step and (lane is None or k[1] == lane)]:
            del self.hits[key]
        return self

    def nudge(self, at, micro):
        """the step `at` sixteenths in (every sound on it) nudged micro 64ths of a step (+ late)"""
        if not -32 <= micro <= 31:
            raise ValueError("a nudge is -32..31")
        self.nudges[at // self.s16] = micro
        return self

    def tuned(self, lane, semis=0, decay=0):
        """a sound's TUNE (-8..7 semitones) and DECAY (-4..3) for this section (DRUMS > SOUND)"""
        i = LANE_ORDER.index(lane)
        self.tune[i], self.decay[i] = semis, decay
        return self

    def lock(self, at, param, value):
        p = SHORT.get(param, param)
        if not lockable(p):
            raise ValueError(f"{p} cannot be locked on a step")
        self.locks[(at // self.s16, p)] = value
        return self


class Section:
    def __init__(self, song, letter, title):
        self.song, self.letter, self.title = song, letter, title
        self.index = "ABCD".index(letter)
        self.bass, self.keys, self.lead = Part(self, 0, "bass"), Part(self, 1, "keys"), Part(self, 2, "lead")
        self.drums = Drums(self)
        self.harmony = []                             # (at, chord symbol): what the report checks the lead against

    @property
    def parts(self):
        return [self.bass, self.keys, self.lead]

    def chords(self, *spec):
        """the section's chords for the report: (at sixteenths, symbol) pairs"""
        self.harmony = sorted((at, sym) for at, sym in spec)
        for _, sym in self.harmony:
            parse_chord(sym)
        return self

    def chord_at(self, at):
        cur = None
        for a, sym in self.harmony:
            if a <= at:
                cur = sym
        return cur


class Song:
    def __init__(self, title, bpm=106, **globals_):
        self.title = title
        self.globals = _params({"bpm": bpm, **globals_}, GSHORT)
        self.sections = {}
        self.entries = []
        self.loop = True
        self.stop_at = None
        self.strong = 4                               # (the report's strong beats: every 4 sixteenths; half time: 8)
        self.check_lead = True                        # (False: track 3 is not a melody, the report leaves it alone)

    def section(self, letter, title, like=None, transpose=0):
        """a section; like: another one copied (its synth notes transposed `transpose` semitones)"""
        if like:
            src = self.sections[like]
            s = copy.deepcopy(src)
            s.letter, s.title, s.index = letter, title, "ABCD".index(letter)
            for p in s.parts:
                p.sec = s
                for e in p.ev.values():
                    e.notes = [n + transpose for n in e.notes]
            s.drums.sec = s
            s.harmony = [(a, _transpose_sym(sym, transpose)) for a, sym in s.harmony]
        else:
            s = Section(self, letter, title)
        self.sections[letter] = s
        return s

    def order(self, *entries, loop=True, stop_at=None):
        """the song: (section, bars) in order (up to 16), looping; stop_at: the section STOP brings back"""
        if not 1 <= len(entries) <= 16:
            raise ValueError("a song has 1 to 16 steps")
        for letter, bars in entries:
            if letter not in self.sections:
                raise ValueError(f"no section {letter}")
            if not 1 <= bars <= 64:
                raise ValueError("a song step is 1 to 64 bars")
        self.entries, self.loop = list(entries), loop
        self.stop_at = stop_at or entries[0][0]
        return self

    # ---- the timeline: every note as it sounds, the song once through
    def timeline(self):
        """(start sixteenth (float, nudges included), length, notes, velocity, track, section letter) of every note,
        and (start, lane, velocity, section letter) of every drum hit, the song once through"""
        notes, hits, t0 = [], [], 0
        gsw = self.globals.get("G_SWING", 0)

        def swung(part, step):
            if part.div != "1/16" or not step % 2:
                return 0
            return min(100, part.params.get("P_SSWING", 0) + gsw) / 200
        for letter, bars in self.entries:
            s = self.sections[letter]
            span = bars * 16
            for p in s.parts:
                if not p.ev:
                    continue
                for rep in range(0, span, p.span16):
                    for e in p.ev.values():
                        start = rep + e.step * p.s16 + e.micro / 64 * p.s16 + swung(p, e.step)
                        if start < span:
                            notes.append((t0 + start, min(e.dur, span - start), e.notes, e.loud, p.role, letter))
            d = s.drums
            for rep in range(0, span, d.span16):
                for (st, lane), (lvl, rat) in d.hits.items():
                    start = rep + st * d.s16 + d.nudges.get(st, 0) / 64 * d.s16 + swung(d, st)
                    if start < span:
                        for h in range(rat):
                            hits.append((t0 + start + h * d.s16 / rat, lane, LEVEL_VEL[lvl], letter))
            t0 += span
        return notes, hits, t0

    # ---- the C tables (firmware/host/web.c ws_*)
    def header(self, source):
        out = [f"/* generated by tools/songlib.py from {source}: edit that file, not this one */",
               f"/* {self.title} */"]
        out.append("static const ws_gparam_t WS_GLOBAL[] = {" +
                   ", ".join(f"{{{g}, {v}}}" for g, v in self.globals.items()) + "};")
        for letter, s in sorted(self.sections.items()):
            for p in s.parts:
                out += _c_part(letter, p)
            out += _c_drums(letter, s.drums)
        secs = []
        for letter in "ABCD":
            s = self.sections.get(letter)
            if not s:
                secs.append("    {0}")
                continue
            d = s.drums
            dg = d.globals
            tr = [_c_track_ref(letter, p) for p in s.parts] + [_c_track_ref(letter, d)]
            secs.append(f"    {{1, {dg.get('G_DRLVL', -1)}, {dg.get('G_DRREV', -1)}, {dg.get('G_DRDLY', -1)}, "
                        f"{{{', '.join(map(str, d.tune))}}}, {{{', '.join(map(str, d.decay))}}},\n     {{" +
                        ",\n      ".join(tr) + "}}")
        out.append("static const ws_section_t WS_SECTION[4] = {\n" + ",\n".join(secs) + "};")
        out.append("static const arr_entry_t WS_ORDER[] = {" +
                   ", ".join(f"{{{'ABCD'.index(l)}, {b}}}" for l, b in self.entries) + "};")
        out.append(f"#define WS_NORDER {len(self.entries)}u")
        out.append(f"#define WS_LOOP {int(self.loop)}u")
        out.append(f"#define WS_STOP_AT {'ABCD'.index(self.stop_at)}u")
        return "\n".join(out) + "\n"

    # ---- a MIDI file: the four tracks, the song once through
    def midi(self):
        tpq = 480
        notes, hits, _ = self.timeline()
        tempo = round(60_000_000 / self.globals["G_BPM"])
        tracks = [[(0, b"\xff\x51\x03" + tempo.to_bytes(3, "big")), (0, b"\xff\x03" + _vlq(len(self.title)) +
                                                                    self.title.encode())]]
        for ch, role in enumerate(["bass", "keys", "lead"]):
            ev = [(0, b"\xff\x03" + _vlq(len(role)) + role.encode())]
            for start, dur, ps, vel, r, _ in notes:
                if r != role:
                    continue
                a, b = round(start * tpq / 4), round((start + dur) * tpq / 4) - 2
                for p in ps:
                    ev.append((a, bytes([0x90 | ch, p, vel])))
                    ev.append((max(a + 1, b), bytes([0x80 | ch, p, 0])))
            tracks.append(ev)
        ev = [(0, b"\xff\x03\x05drums")]
        for start, lane, vel, _ in hits:
            a = round(start * tpq / 4)
            ev.append((a, bytes([0x99, LANES[lane], vel])))
            ev.append((a + tpq // 8, bytes([0x89, LANES[lane], 0])))
        tracks.append(ev)
        data = b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), tpq)
        for ev in tracks:
            ev.sort(key=lambda e: (e[0], e[1][0] & 0xF0 == 0x90))
            body, last = b"", 0
            for t, msg in ev:
                body += _vlq(t - last) + msg
                last = t
            body += b"\x00\xff\x2f\x00"
            data += b"MTrk" + struct.pack(">I", len(body)) + body
        return data

    # ---- the report: what to look at before listening
    def report(self):
        lines = [f"{self.title}: {self.globals['G_BPM']} BPM"]
        notes, hits, total = self.timeline()
        secs = total / 4 * 60 / self.globals["G_BPM"]
        lines.append(f"  {total // 16} bars, {secs:.0f} s: " + ", ".join(f"{l} {b}" for l, b in self.entries))
        for letter, s in sorted(self.sections.items()):
            lines.append(f"  {letter} {s.title}")
            for p in s.parts:
                if not p.ev:
                    continue
                ps = [n for e in p.ev.values() for n in e.notes]
                bars = p.span16 / 16
                lines.append(f"    {p.role:5s} {ENGINES[p.engine][0]} {ENGINES[p.engine][1][p.preset]:11s} "
                             f"{name(min(ps))}..{name(max(ps))}  {len(p.ev) / bars:4.1f} a bar  "
                             f"({p.nsteps} steps of {p.div}, {len(p.locks)} locks)")
            d = s.drums
            lanes = sorted({l for _, l in d.hits}, key=LANE_ORDER.index)
            lines.append(f"    drums {KITS[d.kit]:8s} {len(d.hits) / (d.span16 / 16):4.1f} hits a bar: " + " ".join(lanes))
            if s.harmony and s.lead.ev and self.check_lead:
                for msg in _check_lead(s):
                    lines.append("    lead: " + msg)
        return "\n".join(lines)

    def main(self):
        ap = argparse.ArgumentParser(description=f"{self.title}: the song's C tables, MIDI file and report")
        ap.add_argument("--header", help="the C tables for web.c (tools/build_web.py --song passes it)")
        ap.add_argument("--midi", help="a MIDI file of the song once through")
        ap.add_argument("--json", help="the song's tempo, sections and length (tools/song_render.mjs reads it)")
        ap.add_argument("--quiet", action="store_true")
        a = ap.parse_args()
        src = Path(sys.argv[0]).resolve()
        try:
            src = src.relative_to(ROOT)
        except ValueError:
            pass
        if a.header:
            Path(a.header).write_text(self.header(str(src)), encoding="utf-8")
        if a.midi:
            Path(a.midi).write_bytes(self.midi())
        if a.json:
            import json
            bars = sum(b for _, b in self.entries)
            Path(a.json).write_text(json.dumps({
                "title": self.title, "bpm": self.globals["G_BPM"], "bars": bars, "loop": self.loop,
                "seconds": bars * 240 / self.globals["G_BPM"],
                "entries": [{"section": l, "title": self.sections[l].title, "bars": b} for l, b in self.entries]}, indent=1))
        if not a.quiet:
            print(self.report())


def _transpose_sym(sym, semis):
    m = re.fullmatch(r"([A-G][#b]?)(.*?)(?:/([A-G][#b]?))?", sym)
    root = NAMES[(pc(m.group(1)) + semis) % 12]
    bass = "/" + NAMES[(pc(m.group(3)) + semis) % 12] if m.group(3) else ""
    return root + m.group(2) + bass


def _check_lead(s):
    """the lead's notes on the beat, or held a beat or more, against the chord there (an anticipation, starting
    within an eighth of a chord change and held over it: the chord it is held into): outside it (and not a tension
    the symbol names) is worth a listen; a 4th over a major 3rd, a b9 over a major or minor chord: avoid notes"""
    out = []
    for e in sorted(s.lead.ev.values(), key=lambda e: e.at):
        for rep in range(1):
            at = e.at
            strong = at % s.song.strong == 0 or e.dur >= 4
            nxt = min((a for a, _ in s.harmony if at < a <= at + 2), default=None)
            sym = s.chord_at(nxt if nxt is not None and at + e.dur > nxt else at)
            if not sym or not strong:
                continue
            root, iv, _ = parse_chord(sym)
            tones = chord_tones(sym)
            for n in e.notes:
                rel = (n - root) % 12
                bar, beat = at // 16 + 1, at % 16 / 4 + 1
                if n % 12 in tones:
                    continue
                avoid = (rel == 5 and 4 in [i % 12 for i in iv] and 5 not in [i % 12 for i in iv]) or \
                        (rel == 1 and 13 not in iv)
                out.append(f"bar {bar} beat {beat:g}: {name(n)} over {sym} " + ("is an avoid note" if avoid else
                                                                                   "is outside the chord"))
    return out


def _c_list(name_, items):
    if not items:
        return [], "NULL, 0"
    return [f"static const {name_[0]} {name_[1]}[] = {{" + ", ".join(items) + "};"], f"{name_[1]}, {len(items)}"


def _c_part(letter, p):
    if p.engine is None:
        if p.ev:
            raise ValueError(f"{letter} {p.role}: notes, but no sound()")
        p.engine, p.preset = 0, 0                       # (an empty part: any sound, it plays nothing)
    base = f"WS_{letter}{p.k}"
    params = dict(p.params)
    params["P_SDIV"] = DIVS[p.div][0]
    params["P_SLEN"] = p.nsteps
    lines, p._c = [], {}
    for kind, items in (
            ("P", [f"{{{k}, {v}}}" for k, v in params.items()]),
            ("N", [f"{{{e.step}, {len(e.notes)}, {{{', '.join(map(str, e.notes + [0] * (4 - len(e.notes))))}}}, "
                   f"{e.steps}, {' | '.join(e.flags) or 0}, {e.vel}, 0, {e.rat}, {e.micro}}}"
                   for e in sorted(p.ev.values(), key=lambda e: e.step)]),
            ("L", [f"{{{st}, {pp}, {v}}}" for (st, pp), v in sorted(p.locks.items())])):
        t = {"P": "ws_param_t", "N": "ws_note_t", "L": "ws_lock_t"}[kind]
        decl, ref = _c_list((t, base + "_" + kind), items)
        lines += decl
        p._c[kind] = ref
    return lines


def _c_drums(letter, d):
    base = f"WS_{letter}3"
    params = dict(d.params)
    params["P_E0"] = d.kit
    params["P_SDIV"] = DIVS[d.div][0]
    params["P_SLEN"] = d.nsteps
    lines, d._c = [], {}
    for kind, t, items in (
            ("P", "ws_param_t", [f"{{{k}, {v}}}" for k, v in params.items()]),
            ("H", "ws_hit_t", [f"{{{at}, {LANES[lane]}, {lvl}, {rat - 1}}}"
                               for (at, lane), (lvl, rat) in sorted(d.hits.items(), key=lambda x: (x[0][0], LANE_ORDER.index(x[0][1])))]),
            ("U", "ws_nudge_t", [f"{{{at}, {m}}}" for at, m in sorted(d.nudges.items())]),
            ("L", "ws_lock_t", [f"{{{st}, {pp}, {v}}}" for (st, pp), v in sorted(d.locks.items())])):
        decl, ref = _c_list((t, base + "_" + kind), items)
        lines += decl
        d._c[kind] = ref
    return lines


def _c_track_ref(letter, p):
    c = p._c
    if isinstance(p, Drums):
        return f"{{0, 0, {c['P']}, NULL, 0, {c['H']}, {c['U']}, {c['L']}}}"
    return f"{{{p.engine}, {p.preset}, {c['P']}, {c['N']}, NULL, 0, NULL, 0, {c['L']}}}"


def _vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))
