/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthesthetic's visualizer: the pictures, drawn on the video core (vcore.c) into a 160 x 90 frame of palette
 * indices: modes 0..15 here in 64 hues x 4 levels (level 0 black, so trails fade through the levels), modes
 * 16..29 in vproc.c in intensities (its palette notes). The same frame goes to the
 * webcam (JPEG, 2 x: 320 x 180) and to the FM-1's screen (ui_vis.c, 1.5 x). Everything here is fm1drome's own.
 *
 * The sound: the analyser snapshots the scope rings (audio.c: the master mix at 22 050 Hz, left and right, as
 * if MASTER were up; fx.c averages sample pairs first against aliasing) and runs a 1024-point FFT (21.5 Hz
 * bins) into 32 bands, about a quarter octave each from 40 Hz to 10 kHz (the lowest ones a bin wide), each
 * the energy in its bins on a decibel scale: 1000 = 0 dBFS, 0 = -60 dBFS, 100 per 6 dB. The meters are
 * decibels too. The waveform pictures scale the wave to the frame (autogain with a floor).
 *
 * The panel, as the EYESY does it: KNOB 1 AMOUNT (size, scale, count), KNOB 2 RATE (speed, rotation),
 * KNOB 3 TRAILS (0 = none, up = longer, full = persist), KNOB 4 COLOR (the hue; the modes add their own).
 * Triggers: the sequencer's and the keys' note-ons per track (voice.c vis_hit), the kick, and the beat.
 *
 * Runs on core 1: reads core 0's rings and mailbox (vc0), writes nothing of core 0's. Integers only; the
 * buffers are static (core 1's stack is small); between rows it yields (vcore_yield) so a flash write on core 0
 * can park it. */
#define VR_W VF_W
#define VR_H VF_H
#define VR_N 1024u                                      /* the analyser's window: the whole ring */
#define VR_MODES VIS_N                                  /* the mode names: ui_vis.c VIS_NAME */
#define VR_ENT 24u                                      /* trigger modes: live shapes */
#ifdef VF_SPARE                                         /* the sound analysis' four buffers, after ui_vis.c's slice in
                                                         * the frames' RAM (vcore.c VF_SPARE): SCREEN's copy takes it
                                                         * all, so vrender_frame leaves them alone while SCREEN is drawn */
#define VR_SCRATCH ((int16_t *)(void *)(VF_SPARE + VF_W * VIS_ROWS))
_Static_assert(VF_W * VIS_ROWS % 4u == 0u && VF_W * VIS_ROWS + 4u * VR_N * 2u <= VF_SPARE_N, "the analysis fits after the frames");
#else                                                   /* (tests/vrender_test.c: no video core around it) */
static int16_t vr_scratch[4u * VR_N];
#define VR_SCRATCH vr_scratch
#endif
static int16_t *const vr_l = VR_SCRATCH, *const vr_r = VR_SCRATCH + VR_N, *const vr_re = VR_SCRATCH + 2u * VR_N,
               *const vr_im = VR_SCRATCH + 3u * VR_N;
static int16_t vr_band[32], vr_cap[32], vr_capv[32];
static int32_t vr_lvl[5], vr_hold[5], vr_holdt[5];      /* tracks 1-4 and the master, dB 0..1000; peak holds */
static int32_t vr_peak, vr_gain = 4096;                 /* this frame's peak, the smoothed scale (Q15) */
static uint32_t vr_t;                                   /* frames drawn */
static uint32_t vr_k[4];                                /* the knobs, 0..255 */
static uint32_t vr_hue;                                 /* KNOB 4's hue (0..63) plus a slow drift */
static uint32_t vr_hitc[4], vr_kickc, vr_hit, vr_kick;  /* trigger counters seen; this frame's hits (bits), kick */
static uint32_t vr_note[4], vr_beat, vr_flash;          /* the last notes, the beat phase (1/256), a kick flash */
static uint32_t vr_seed = 0x9E3779B9u;
enum { DK_KICK, DK_SNARE, DK_CLAP, DK_CHH, DK_OHH, DK_TOM, DK_CRASH, DK_RIDE, DK_PERC, DK_N };   /* voice.c vis_drum_family */
enum { EK_SQUARE, EK_SHARD, EK_DRUM };                 /* a shape's kind: a track's square, a track's flying triangle, a drum's */
static struct { int32_t x, y, vx, vy, size, rot, life, life0; uint32_t hue, kind, drum, seed; } vr_ent[VR_ENT];
static uint32_t vr_drumc[DK_N], vr_drum;                /* drum hit counters seen; this frame's hits, a bit per family */
static uint32_t vr_var;                                 /* the mode's variation (the autopilot's pick; 0 by hand) */
static uint32_t vr_pulsec, vr_bang;                     /* MIDI pulses seen; this frame: a kick or a pulse */
static uint32_t vr_fresh;                               /* the first frame of a mode: the last frame is another's */
static uint32_t vr_mode_last = 0xFFu, vr_pal_cur, vr_pal_last = 0xFFu, vr_cyc;   /* the palette's state, cycling */
static uint32_t vr_bright = 256;                        /* the picture's brightness, 0..256: a mode change fades */
#define VR_FADE_OUT 64u                                 /* 4 frames down */
#define VR_FADE_IN 52u                                  /* 5 frames up */

/* the bands' edges in 21.5 Hz bins: 40 Hz .. 10 kHz in 32 steps of a quarter octave, never under a bin */
static const uint16_t VR_EDGE[33] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 18, 21, 25, 29, 35, 41, 49, 59,
                                     70, 83, 98, 117, 139, 165, 196, 233, 277, 329, 391, 464};

static int32_t isin(uint32_t a) { return SINE[a & 1023u]; }          /* a: 1024 a turn; Q15 */
static int32_t icos(uint32_t a) { return SINE[(a + 256u) & 1023u]; }
static uint32_t vr_rnd(void) { vr_seed = vr_seed * 1103515245u + 12345u; return vr_seed >> 8; }
static uint8_t vr_pal(uint32_t hue, uint32_t lvl) { return (uint8_t)((lvl & 3u) ? (hue & 63u) | ((lvl & 3u) << 6) : 0u); }
static uint32_t vr_lg8(uint32_t x)                      /* 8 log2 x: an octave in 8 steps (x >= 1) */
{
    uint32_t lg = 0;
    if (!x)
        return 0;
    while ((x >> lg) > 1u)
        lg++;
    return lg * 8u + (lg >= 3u ? (x >> (lg - 3u)) & 7u : (x << (3u - lg)) & 7u);
}
static int32_t vr_db(uint32_t peak)                     /* a Q15 peak as 0..1000: 1000 = 0 dBFS, 0 = -60 dB */
{
    return clamp(((int32_t)vr_lg8(peak) - 40) * 1000 / 80, 0, 1000);
}

/* ---- drawing ---- */
static void vr_px(uint8_t *f, int32_t x, int32_t y, uint8_t c)
{
    if ((uint32_t)x < VR_W && (uint32_t)y < VR_H)
        f[(uint32_t)y * VR_W + (uint32_t)x] = c;
}

static void vr_line(uint8_t *f, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t c)   /* Bresenham */
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int32_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy, n = 0;
    for (;;) {
        vr_px(f, x0, y0, c);
        if ((x0 == x1 && y0 == y1) || ++n > 400)
            break;
        {
            int32_t e2 = err * 2;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

static void vr_disc(uint8_t *f, int32_t cx, int32_t cy, int32_t r, uint8_t c)   /* filled circle */
{
    int32_t y;
    if (r > 70)
        r = 70;
    for (y = -r; y <= r; y++) {
        int32_t w = 0, x;
        while ((w + 1) * (w + 1) + y * y <= r * r)
            w++;
        for (x = -w; x <= w; x++)
            vr_px(f, cx + x, cy + y, c);
    }
}

static void vr_ring(uint8_t *f, int32_t cx, int32_t cy, int32_t r, uint8_t c)   /* an outline */
{
    uint32_t i, n = r < 8 ? 16u : r < 24 ? 32u : 64u;
    int32_t px = cx + r, py = cy;
    for (i = 1; i <= n; i++) {
        uint32_t a = i * 1024u / n;
        int32_t x = cx + ((r * icos(a)) >> 15), y = cy + ((r * isin(a)) >> 15);
        vr_line(f, px, py, x, y, c);
        px = x;
        py = y;
    }
}

static void vr_box(uint8_t *f, int32_t cx, int32_t cy, int32_t s, uint32_t rot, uint8_t c)   /* an outline square */
{
    int32_t x[4], y[4], i;
    for (i = 0; i < 4; i++) {
        uint32_t a = rot + (uint32_t)i * 256u + 128u;
        x[i] = cx + ((s * 181 / 128 * icos(a)) >> 15);         /* the corners at s * sqrt 2 */
        y[i] = cy + ((s * 181 / 128 * isin(a)) >> 15);
    }
    for (i = 0; i < 4; i++)
        vr_line(f, x[i], y[i], x[(i + 1) & 3], y[(i + 1) & 3], c);
}

static void vr_tri(uint8_t *f, int32_t cx, int32_t cy, int32_t s, uint32_t rot, uint8_t c)   /* an outline triangle */
{
    int32_t x[3], y[3], i;
    for (i = 0; i < 3; i++) {
        uint32_t a = rot + (uint32_t)i * 341u;
        x[i] = cx + ((s * icos(a)) >> 15);
        y[i] = cy + ((s * isin(a)) >> 15);
    }
    for (i = 0; i < 3; i++)
        vr_line(f, x[i], y[i], x[(i + 1) % 3], y[(i + 1) % 3], c);
}

static void vr_rect(uint8_t *f, int32_t x0, int32_t y0, int32_t w, int32_t h, uint8_t c)   /* filled */
{
    int32_t x, y;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++)
            vr_px(f, x, y, c);
}

static void vr_clear(uint8_t *dst) { memset(dst, 0, VF_N); }

/* the background: KNOB 3. 0 = clear; up to 254 = the trail fades a level every 1..8 frames; 255 = persist */
static void vr_bg(uint8_t *dst, const uint8_t *prev)
{
    uint32_t i, k = vr_k[2], keep = 1u + k / 36u, drop;
    if (k == 0u || vr_fresh) {
        vr_clear(dst);
        return;
    }
    drop = k < 255u && (vr_t % keep) == 0u;
    for (i = 0; i < VF_N; i++) {
        uint32_t v = prev[i], l = v >> 6;
        if (drop && l)
            l--;
        dst[i] = (uint8_t)(l ? (v & 63u) | (l << 6) : 0u);
        if ((i & 2047u) == 2047u)
            vcore_yield();
    }
}

/* ---- the sound ---- */
static void vr_snapshot(void)
{
    uint32_t w, i;
    int32_t pk = 0;
    fm1_c1_sync();
    w = scope_w;
    for (i = 0; i < VR_N; i++) {
        int32_t l = scope_buf[(w + i) & (SCOPE_N - 1u)], a = l < 0 ? -l : l;
        vr_l[i] = (int16_t)l;
        vr_r[i] = scope_bufr[(w + i) & (SCOPE_N - 1u)];
        if (a > pk)
            pk = a;
    }
    vr_peak = pk;
    vr_gain -= vr_gain >> 5;                            /* the scale follows the peaks down slowly, up at once */
    if (pk > vr_gain)
        vr_gain = pk;
    if (vr_gain < 2048)
        vr_gain = 2048;                                 /* floor: a quiet signal fills a quarter of the height */
}

static void vr_fft(void)                                /* 1024 points, Hann window, halved each stage (/N) */
{
    uint32_t i, j = 0, bit, len;
    for (i = 0; i < VR_N; i++) {
        int32_t w = (32768 - icos(i)) >> 1;
        vr_re[i] = (int16_t)((vr_l[i] * w) >> 15);
        vr_im[i] = 0;
    }
    for (i = 1; i < VR_N; i++) {
        for (bit = VR_N >> 1; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            int16_t t = vr_re[i]; vr_re[i] = vr_re[j]; vr_re[j] = t;
        }
    }
    for (len = 2; len <= VR_N; len <<= 1) {
        uint32_t half = len >> 1, k;
        for (i = 0; i < VR_N; i += len)
            for (k = 0; k < half; k++) {
                uint32_t a = i + k, b = a + half, ix = k * (1024u / len);
                int32_t wr = icos(ix), wi = -isin(ix);
                int32_t tr = (vr_re[b] * wr - vr_im[b] * wi) >> 15, ti = (vr_re[b] * wi + vr_im[b] * wr) >> 15;
                int32_t ar = vr_re[a], ai = vr_im[a];
                vr_re[b] = (int16_t)((ar - tr) >> 1);
                vr_im[b] = (int16_t)((ai - ti) >> 1);
                vr_re[a] = (int16_t)((ar + tr) >> 1);
                vr_im[a] = (int16_t)((ai + ti) >> 1);
            }
        vcore_yield();
    }
}

#define VR_E0 14                                        /* 8 log2 of the energy of a 0 dBFS sine, less 60 dB (the test calibrates it) */
#define VR_E60 159                                      /* 60 dB of energy in 8 log2 units */
static void vr_bands(void)                              /* the 32 bands: energy, in dB, and their falling caps */
{
    uint32_t k, i;
    for (k = 0; k < 32u; k++) {
        uint32_t e = 0;
        int32_t v;
        for (i = VR_EDGE[k]; i < VR_EDGE[k + 1] && i < VR_N / 2u; i++) {
            int32_t re = vr_re[i] >> 2, im = vr_im[i] >> 2;
            e += (uint32_t)(re * re + im * im);
        }
        v = e ? clamp(((int32_t)vr_lg8(e) - VR_E0) * 1000 / VR_E60, 0, 1000) : 0;
        vr_band[k] = (int16_t)v;
        if (v >= vr_cap[k]) {
            vr_cap[k] = (int16_t)v;
            vr_capv[k] = 0;
        } else {
            vr_capv[k] = (int16_t)(vr_capv[k] + 6);
            vr_cap[k] = (int16_t)(vr_cap[k] - vr_capv[k] < 0 ? 0 : vr_cap[k] - vr_capv[k]);
        }
    }
}

static int32_t vr_group(uint32_t a, uint32_t b)        /* the loudest band of a..b-1 */
{
    int32_t m = 0;
    for (; a < b; a++)
        if (vr_band[a] > m)
            m = vr_band[a];
    return m;
}

static void vr_inputs(void)                             /* the mailbox: knobs, levels, triggers, the beat */
{
    uint32_t i, kn, nt;
    fm1_c1_sync();
    kn = vc0.knob;
    nt = vc0.note;
    for (i = 0; i < 4u; i++) {
        vr_k[i] = (kn >> (8u * i)) & 255u;
        vr_note[i] = (nt >> (8u * i)) & 127u;
    }
    vr_hit = 0;
    for (i = 0; i < 4u; i++) {
        uint32_t c = vc0.hitc[i];
        if (c != vr_hitc[i])
            vr_hit |= 1u << i;
        vr_hitc[i] = c;
        vr_lvl[i] = vr_db(vc0.tpk[i]);
    }
    vr_kick = vc0.kickc != vr_kickc;
    vr_kickc = vc0.kickc;
    vr_drum = 0;
    for (i = 0; i < DK_N; i++) {
        uint32_t c = vc0.drumc[i];
        if (c != vr_drumc[i])
            vr_drum |= 1u << i;
        vr_drumc[i] = c;
    }
    vr_lvl[4] = vr_db((uint32_t)vr_peak);
    for (i = 0; i < 5u; i++) {                          /* peak holds: a second, then they fall */
        if (vr_lvl[i] >= vr_hold[i]) {
            vr_hold[i] = vr_lvl[i];
            vr_holdt[i] = 20;
        } else if (vr_holdt[i])
            vr_holdt[i]--;
        else
            vr_hold[i] = vr_hold[i] > 15 ? vr_hold[i] - 15 : 0;
    }
    vr_beat = vc0.beat_q8;
    vr_hue = vr_k[3] / 4u + (vr_t >> 4);                /* KNOB 4, drifting slowly */
    vr_var = vc0.var;
    vr_bang = vr_kick || vc0.pulsec != vr_pulsec;       /* the kick, or a note on the visuals' MIDI channel */
    vr_pulsec = vc0.pulsec;
    vr_flash = vr_bang ? 3u : vr_flash ? vr_flash - 1u : 0u;
}

/* ---- the modes (a 160 x 90 frame, centre 80, 45) ---- */
#define VR_CX 80
#define VR_CY 45
static int32_t vr_amp(int32_t base) { return base * (int32_t)(64u + vr_k[0]) / 192; }   /* KNOB 1: a third to 1.7 x */
static void vr_drums_spawn(void);                       /* the shapes (below): this frame's drum hits, the live shapes */
static void vr_ents(uint8_t *dst);
static uint32_t vr_rate(void) { return 1u + vr_k[1] / 16u; }                             /* KNOB 2: 1..16 */

static void vr_wave(uint8_t *dst, const uint8_t *prev)   /* 0 WAVE: left and right, triggered, thick */
{
    uint32_t i, start = 0;
    int32_t x, yl = 0, yr = 0, steep = 0, amp = vr_amp(36);
    vr_bg(dst, prev);
    for (i = 1; i < VR_N / 2u; i++)                     /* the steepest rising zero crossing: a stable picture */
        if (vr_l[i - 1u] < 0 && vr_l[i] >= 0 && vr_l[i] - vr_l[i - 1u] > steep) {
            steep = vr_l[i] - vr_l[i - 1u];
            start = i;
        }
    for (x = 0; x < (int32_t)VR_W; x++) {
        uint32_t k = start + (uint32_t)x * 3u;
        int32_t l = VR_CY - vr_l[k & (VR_N - 1u)] * amp / vr_gain, r = VR_CY - vr_r[k & (VR_N - 1u)] * amp / vr_gain;
        if (x) {
            vr_line(dst, x - 1, yr, x, r, vr_pal(vr_hue + 32u, 1));
            vr_line(dst, x - 1, yl, x, l, vr_pal(vr_hue + (uint32_t)x / 10u, 3));
            vr_line(dst, x - 1, yl + 1, x, l + 1, vr_pal(vr_hue + (uint32_t)x / 10u, 2));
        }
        yl = l;
        yr = r;
    }
}

static void vr_bars(uint8_t *dst, const uint8_t *prev)   /* 1 BARS: the 32 bands and their caps */
{
    uint32_t k, h0 = 30u + vr_k[0] * 50u / 255u;       /* KNOB 1: the bars' height, 30..80 px for 0 dBFS */
    vr_bg(dst, prev);
    for (k = 0; k < 32u; k++) {
        int32_t x0 = 16 + (int32_t)k * 4, h = vr_band[k] * (int32_t)h0 / 1000, c = vr_cap[k] * (int32_t)h0 / 1000, y;
        uint8_t col = vr_pal(k * 2u + vr_hue, 3), dim = vr_pal(k * 2u + vr_hue, 1);
        for (y = 0; y < h; y++) {
            vr_px(dst, x0, 85 - y, col);
            vr_px(dst, x0 + 1, 85 - y, col);
            vr_px(dst, x0 + 2, 85 - y, dim);
        }
        vr_px(dst, x0, 85 - c, vr_pal(vr_hue + 32u, 3));
        vr_px(dst, x0 + 1, 85 - c, vr_pal(vr_hue + 32u, 3));
    }
}

static void vr_heat(uint8_t *dst, const uint8_t *prev)   /* 2 HEAT: the spectrum scrolling left */
{
    uint32_t y, x, step = 1u + vr_k[1] / 86u;           /* KNOB 2: 1..3 columns a frame */
    for (y = 0; y < VR_H; y++) {
        const uint8_t *s = prev + y * VR_W;
        uint8_t *d = dst + y * VR_W;
        uint32_t k = (VR_H - 1u - y) * 32u / VR_H, v = (uint32_t)vr_band[k];
        uint32_t lvl = v < 120u ? 0u : v < 450u ? 1u : v < 750u ? 2u : 3u;
        uint8_t c = vr_pal(vr_hue + 40u - v * 40u / 1000u, lvl);   /* cold to hot, from KNOB 4 */
        for (x = 0; x + step < VR_W; x++)
            d[x] = s[x + step] >= 64u ? s[x + step] : 0u;   /* (the hue levels only: never an overlay index) */
        for (; x < VR_W; x++)
            d[x] = c;
        if ((y & 15u) == 15u)
            vcore_yield();
    }
}

static void vr_xy(uint8_t *dst, const uint8_t *prev)   /* 3 XY: left against right */
{
    uint32_t i;
    int32_t px = VR_CX, py = VR_CY, amp = vr_amp(40);
    vr_bg(dst, prev);
    for (i = 0; i < VR_N / 2u; i++) {
        int32_t x = VR_CX + vr_l[i * 2u] * amp / vr_gain, y = VR_CY - vr_r[i * 2u] * amp / vr_gain;
        if (i)
            vr_line(dst, px, py, x, y, vr_pal(vr_hue + i / 32u, 3));
        px = x;
        py = y;
    }
}

static void vr_meters(uint8_t *dst, const uint8_t *prev)   /* 4 METERS: tracks 1-4 and the master, in dB */
{
    uint32_t j;
    vr_bg(dst, prev);
    for (j = 0; j < 5u; j++) {
        int32_t x0 = 8 + (int32_t)j * 30, h = vr_lvl[j] * 80 / 1000, hh = vr_hold[j] * 80 / 1000, y;
        uint32_t hue = j == 4u ? vr_hue : vr_hue + 16u * j;
        for (y = 0; y < h; y++)
            vr_rect(dst, x0, 85 - y, 24, 1, vr_pal(hue + (uint32_t)y / 6u, (y & 3u) == 3u ? 1u : 3u));
        vr_rect(dst, x0, 85 - hh, 24, 1, vr_pal(hue + 32u, 3));       /* the peak hold */
    }
}

static void vr_ringscope(uint8_t *dst, const uint8_t *prev)   /* 5 RING: the wave round a ring, the kick inside */
{
    uint32_t i, rot = vr_t * vr_rate();
    int32_t px = 0, py = 0, low = vr_group(0, 6), r0 = 12 + (int32_t)vr_k[0] * 16 / 255;
    vr_bg(dst, prev);
    vr_disc(dst, VR_CX, VR_CY, 2 + low * (r0 - 6) / 1000, vr_pal(vr_hue + 32u, vr_flash ? 3u : 2u));
    for (i = 0; i <= 256u; i++) {
        uint32_t a = (i & 255u) * 4u + rot;
        int32_t r = r0 + vr_l[(i * 2u) & (VR_N - 1u)] * (44 - r0) / vr_gain;
        int32_t x = VR_CX + ((r * icos(a)) >> 15), y = VR_CY + ((r * isin(a)) >> 15);
        if (i)
            vr_line(dst, px, py, x, y, vr_pal(vr_hue + i / 8u, 3));
        px = x;
        py = y;
    }
}

static void vr_spiral(uint8_t *dst, const uint8_t *prev)   /* 6 SPIRAL: the wave along a spiral from the centre */
{
    uint32_t i, rot = vr_t * vr_rate() * 2u;
    int32_t px = VR_CX, py = VR_CY, amp = vr_amp(8);
    vr_bg(dst, prev);
    for (i = 0; i < 512u; i++) {
        uint32_t a = i * 6u + rot;
        int32_t r = (int32_t)i * 42 / 512 + vr_l[i * 2u] * amp / vr_gain;
        int32_t x = VR_CX + ((r * icos(a)) >> 15), y = VR_CY + ((r * isin(a)) >> 15);
        if (i)
            vr_line(dst, px, py, x, y, vr_pal(vr_hue + i / 16u, i < 128u ? 2u : 3u));
        px = x;
        py = y;
    }
}

static void vr_grid(uint8_t *dst, const uint8_t *prev)   /* 7 GRID: 40 circles, each a sample of the wave */
{
    uint32_t j, rot = vr_t * vr_rate();
    int32_t amp = vr_amp(8);
    vr_bg(dst, prev);
    for (j = 0; j < 40u; j++) {
        int32_t cx = 10 + (int32_t)(j % 8u) * 20, cy = 9 + (int32_t)(j / 8u) * 18;
        int32_t s = vr_l[(j * 25u + rot) & (VR_N - 1u)], a = s < 0 ? -s : s, r = 1 + a * amp / vr_gain;
        if (r > 8)
            r = 8;
        if (s > vr_gain / 2)
            vr_disc(dst, cx, cy, r, vr_pal(vr_hue + j, 3));
        else
            vr_ring(dst, cx, cy, r, vr_pal(vr_hue + j, 2));
    }
}

static void vr_kit(uint8_t *dst, const uint8_t *prev)   /* 8 KIT: the drums, each sound its own shape, over a faint wave */
{
    uint32_t start = 0, i;
    int32_t x, py = VR_CY;
    vr_bg(dst, prev);
    for (i = 1; i < VR_N / 2u; i++)
        if (vr_l[i - 1u] < 0 && vr_l[i] >= 0) {
            start = i;
            break;
        }
    for (x = 1; x < (int32_t)VR_W; x++) {                /* the mix, faint, under the shapes */
        int32_t y = VR_CY - vr_l[(start + (uint32_t)x * 3u) & (VR_N - 1u)] * 10 / vr_gain;
        vr_line(dst, x - 1, py, x, y, vr_pal(vr_hue + 32u, 1));
        py = y;
    }
    vr_drums_spawn();
    vr_ents(dst);
}

static void vr_tunnel(uint8_t *dst, const uint8_t *prev)   /* 9 TUNNEL: squares receding, pumped by the mix */
{
    uint32_t j, rot = vr_t * vr_rate() * 2u, n = 5u + vr_k[0] * 5u / 255u;
    int32_t pump = vr_lvl[4] * 8 / 1000 + (int32_t)vr_flash * 3;
    vr_bg(dst, prev);
    for (j = 0; j < n; j++) {
        int32_t s = 5 + (int32_t)(j * 40u / n) + pump * (int32_t)(j + 1u) / (int32_t)n;
        vr_box(dst, VR_CX, VR_CY, s, rot + j * 24u, vr_pal(vr_hue + j * 6u, j + 2u > n ? 3u : 2u));
    }
    vr_disc(dst, VR_CX, VR_CY, 2 + pump / 2, vr_pal(vr_hue + 32u, 3));
}

static void vr_mirror(uint8_t *dst, const uint8_t *prev)   /* 10 MIRROR: the waves mirrored four ways */
{
    uint32_t i, start = 0;
    int32_t x, pl = 22, pr = 67, amp = vr_amp(18);
    vr_bg(dst, prev);
    for (i = 1; i < VR_N / 2u; i++)
        if (vr_l[i - 1u] < 0 && vr_l[i] >= 0) {
            start = i;
            break;
        }
    for (x = 0; x < VR_CX; x++) {
        uint32_t k = (start + (uint32_t)x * 3u) & (VR_N - 1u);
        int32_t yl = 22 - vr_l[k] * amp / vr_gain, yr = 67 - vr_r[k] * amp / vr_gain;
        uint8_t c = vr_pal(vr_hue + (uint32_t)x / 5u, 3), d = vr_pal(vr_hue + 32u + (uint32_t)x / 5u, 2);
        if (x) {
            vr_line(dst, x - 1, pl, x, yl, c);          /* the left channel, mirrored across the middle */
            vr_line(dst, VR_W - x, pl, VR_W - 1 - x, yl, c);
            vr_line(dst, x - 1, pr, x, yr, d);          /* the right channel below, mirrored too */
            vr_line(dst, VR_W - x, pr, VR_W - 1 - x, yr, d);
            vr_line(dst, x - 1, VR_H - 1 - pl, x, VR_H - 1 - yl, d);   /* and upside down */
            vr_line(dst, VR_W - x, VR_H - 1 - pl, VR_W - 1 - x, VR_H - 1 - yl, d);
            vr_line(dst, x - 1, VR_H - 1 - pr, x, VR_H - 1 - yr, c);
            vr_line(dst, VR_W - x, VR_H - 1 - pr, VR_W - 1 - x, VR_H - 1 - yr, c);
        }
        pl = yl;
        pr = yr;
    }
}

static void vr_stars(uint8_t *dst, const uint8_t *prev)   /* 11 STARS: flying at you; the bass is the throttle */
{
    static int32_t sx[96], sy[96], sz[96];
    static uint32_t inited;
    uint32_t i;
    int32_t speed = (int32_t)vr_rate() + vr_group(0, 6) * 10 / 1000 + (int32_t)((uint32_t)vr_peak >> 12);
    vr_bg(dst, prev);
    for (i = 0; i < 96u; i++) {
        int32_t x, y, z;
        if (!inited || sz[i] <= speed) {
            sx[i] = (int32_t)(vr_rnd() & 255u) - 128;
            sy[i] = (int32_t)(vr_rnd() & 255u) - 128;
            sz[i] = inited ? 128 : 8 + (int32_t)(i * 5u) / 2;
        }
        sz[i] -= speed;
        z = sz[i] < 1 ? 1 : sz[i];
        x = VR_CX + sx[i] * 48 / z;
        y = VR_CY + sy[i] * 30 / z;
        vr_disc(dst, x, y, z < 16 ? 3 : z < 40 ? 2 : z < 90 ? 1 : 0, vr_pal(vr_hue + i, z < 40 ? 3u : 2u));
        if (z < 48)
            vr_line(dst, x, y, VR_CX + sx[i] * 48 / (z + speed), VR_CY + sy[i] * 30 / (z + speed), vr_pal(vr_hue + i, 2));
    }
    inited = 1;
}

static uint32_t vr_ent_slot(void)                       /* a free slot, else the one nearest its end */
{
    uint32_t i, best = 0, oldest = 0xFFFFFFFFu;
    for (i = 0; i < VR_ENT; i++)
        if (vr_ent[i].life <= 0)
            return i;
        else if ((uint32_t)vr_ent[i].life < oldest) {
            oldest = (uint32_t)vr_ent[i].life;
            best = i;
        }
    return best;
}

static void vr_spawn(uint32_t track, int32_t size, int32_t fly)   /* a synth track's shape: a square, or a flying triangle */
{
    uint32_t b = vr_ent_slot();
    vr_ent[b].kind = fly ? EK_SHARD : EK_SQUARE;
    vr_ent[b].x = fly ? VR_CX * 16 : (int32_t)(10u + vr_rnd() % 140u) * 16;
    vr_ent[b].y = fly ? VR_CY * 16 : (int32_t)(8u + vr_rnd() % 74u) * 16;
    vr_ent[b].vx = fly ? (int32_t)(vr_rnd() % 96u) - 48 : 0;
    vr_ent[b].vy = fly ? (int32_t)(vr_rnd() % 64u) - 32 : 0;
    vr_ent[b].size = size;
    vr_ent[b].rot = (int32_t)(vr_rnd() & 1023u);
    vr_ent[b].life = vr_ent[b].life0 = fly ? 24 : 14;
    vr_ent[b].hue = vr_hue + track * 16u + (vr_note[track] % 12u) * 2u;   /* the track, tinted by its note */
}

/* a drum's shape: the kick a disc that pops in the middle, the snare a star of lines, the clap a scatter of
 * blocks, the closed hat a tick at the top, the open hat a ring that opens, a tom a triangle (low left, high
 * right), the crash a wide ring with sparks, the ride a small ring at the top right, percussion a diamond */
static void vr_spawn_drum(uint32_t d)
{
    uint32_t b = vr_ent_slot(), note = vr_note[3];
    int32_t a = vr_amp(10);
    vr_ent[b].kind = EK_DRUM;
    vr_ent[b].drum = d;
    vr_ent[b].vx = vr_ent[b].vy = 0;
    vr_ent[b].rot = (int32_t)(vr_rnd() & 1023u);
    vr_ent[b].seed = vr_rnd();
    vr_ent[b].x = VR_CX * 16;
    vr_ent[b].y = VR_CY * 16;
    vr_ent[b].hue = vr_hue + d * 7u;
    switch (d) {
    case DK_KICK: vr_ent[b].size = 14 + a; vr_ent[b].life0 = 9; break;
    case DK_SNARE: vr_ent[b].size = 22 + a; vr_ent[b].life0 = 8; break;
    case DK_CLAP: vr_ent[b].size = 3 + a / 3; vr_ent[b].life0 = 7; break;
    case DK_CHH:
        vr_ent[b].x = (int32_t)(16u + vr_rnd() % 128u) * 16; vr_ent[b].y = (int32_t)(8u + vr_rnd() % 12u) * 16;
        vr_ent[b].size = 3 + a / 4; vr_ent[b].life0 = 4; break;
    case DK_OHH: vr_ent[b].size = 6; vr_ent[b].life0 = 12; break;
    case DK_TOM:
        vr_ent[b].x = (note <= 43u ? 28 : note <= 47u ? 80 : 132) * 16; vr_ent[b].y = 62 * 16;
        vr_ent[b].size = 10 + a; vr_ent[b].life0 = 10; break;
    case DK_CRASH: vr_ent[b].size = 8; vr_ent[b].life0 = 18; break;
    case DK_RIDE: vr_ent[b].x = 132 * 16; vr_ent[b].y = 18 * 16; vr_ent[b].size = 4; vr_ent[b].life0 = 10; break;
    default:
        vr_ent[b].x = (int32_t)(10u + vr_rnd() % 140u) * 16; vr_ent[b].y = (int32_t)(8u + vr_rnd() % 74u) * 16;
        vr_ent[b].size = 3 + a / 3; vr_ent[b].life0 = 6; break;
    }
    vr_ent[b].life = vr_ent[b].life0;
}

static void vr_drums_spawn(void)                        /* this frame's drum hits into shapes */
{
    uint32_t d;
    for (d = 0; d < DK_N; d++)
        if (vr_drum & (1u << d))
            vr_spawn_drum(d);
}

static void vr_ents(uint8_t *dst)                       /* the live shapes: a step of life each, then drawn */
{
    uint32_t i, k;
    for (i = 0; i < VR_ENT; i++) {
        int32_t x, y, s, age, lf;
        uint32_t lvl, h;
        if (vr_ent[i].life <= 0)
            continue;
        lf = vr_ent[i].life;
        age = vr_ent[i].life0 - lf;
        lvl = lf * 3 > vr_ent[i].life0 * 2 ? 3u : lf * 3 > vr_ent[i].life0 ? 2u : 1u;
        h = vr_ent[i].hue;
        vr_ent[i].x += vr_ent[i].vx * (int32_t)vr_rate() / 4;
        vr_ent[i].y += vr_ent[i].vy * (int32_t)vr_rate() / 4;
        x = vr_ent[i].x / 16;
        y = vr_ent[i].y / 16;
        s = vr_ent[i].size;
        switch (vr_ent[i].kind) {
        case EK_SQUARE:
            vr_ent[i].rot += (int32_t)vr_rate() * 4;
            vr_ent[i].size += 1;
            vr_box(dst, x, y, s, (uint32_t)vr_ent[i].rot, vr_pal(h, lvl));
            break;
        case EK_SHARD:
            vr_ent[i].rot += vr_ent[i].vx;
            vr_tri(dst, x, y, s, (uint32_t)vr_ent[i].rot, vr_pal(h, lvl));
            break;
        default:
            switch (vr_ent[i].drum) {
            case DK_KICK:                               /* a disc that pops, then shrinks */
                vr_disc(dst, x, y, s * lf / vr_ent[i].life0 + 2, vr_pal(h, lvl));
                break;
            case DK_SNARE:                              /* a star of ten lines, shrinking */
                for (k = 0; k < 10u; k++) {
                    uint32_t a = (uint32_t)vr_ent[i].rot + k * 102u;
                    int32_t r = s * lf / vr_ent[i].life0 + 3;
                    vr_line(dst, x, y, x + ((r * icos(a)) >> 15), y + ((r * isin(a)) >> 15), vr_pal(h + k, lvl));
                }
                break;
            case DK_CLAP: {                             /* six blocks scattered round the middle, drifting out */
                uint32_t sd = vr_ent[i].seed;
                for (k = 0; k < 6u; k++) {
                    int32_t dx = (int32_t)(sd & 63u) - 32, dy = (int32_t)((sd >> 6) & 31u) - 16;
                    sd = sd * 1103515245u + 12345u;
                    vr_rect(dst, x + dx * (age + 4) / 6 - s / 2, y + dy * (age + 4) / 6 - s / 2, s, s, vr_pal(h + k * 3u, lvl));
                }
                break; }
            case DK_CHH:                                /* a tick at the top */
                vr_line(dst, x, y, x, y + s * 2, vr_pal(h, lvl));
                vr_line(dst, x - s / 2, y, x + s / 2, y, vr_pal(h, lvl));
                break;
            case DK_OHH:                                /* a ring that opens */
                vr_ring(dst, x, y, s + age * 4, vr_pal(h, lvl));
                break;
            case DK_TOM:                                /* a triangle, pointing up, filled for a moment */
                vr_tri(dst, x, y, s * lf / vr_ent[i].life0 + 4, 768u, vr_pal(h, lvl));
                if (age < 2)
                    vr_tri(dst, x, y, s / 2, 768u, vr_pal(h, 3));
                break;
            case DK_CRASH: {                            /* a wide ring with sparks */
                uint32_t sd = vr_ent[i].seed + (uint32_t)age;
                int32_t r = s + age * 4;
                vr_ring(dst, x, y, r, vr_pal(h, lvl));
                for (k = 0; k < 8u; k++) {
                    uint32_t a = sd & 1023u;
                    sd = sd * 1103515245u + 12345u;
                    vr_px(dst, x + ((r * icos(a)) >> 15) + (int32_t)(sd & 3u) - 1, y + ((r * isin(a)) >> 15) + (int32_t)((sd >> 2) & 3u) - 1, vr_pal(h + 32u, 3));
                }
                break; }
            case DK_RIDE:                               /* a small ring at the top right */
                vr_ring(dst, x, y, s + age, vr_pal(h, lvl));
                break;
            default:                                    /* percussion: a diamond */
                vr_box(dst, x, y, s, 128u, vr_pal(h, lvl));
                break;
            }
            break;
        }
        vr_ent[i].life--;
    }
}

static void vr_squares(uint8_t *dst, const uint8_t *prev)   /* 12 SQUARES: a square for every note, the drums their shapes */
{
    uint32_t i;
    vr_bg(dst, prev);
    for (i = 0; i < 3u; i++)
        if (vr_hit & (1u << i))
            vr_spawn(i, vr_amp(7) + vr_lvl[i] * 16 / 1000, 0);
    vr_drums_spawn();
    vr_ents(dst);
}

static void vr_shards(uint8_t *dst, const uint8_t *prev)   /* 13 SHARDS: triangles flung out by the notes, the drums their shapes */
{
    uint32_t i;
    vr_bg(dst, prev);
    for (i = 0; i < 3u; i++)
        if (vr_hit & (1u << i))
            vr_spawn(i, vr_amp(5) + vr_lvl[i] * 10 / 1000, 1);
    vr_drums_spawn();
    vr_ents(dst);
}

static void vr_orbits(uint8_t *dst, const uint8_t *prev)   /* 14 ORBITS: 32 dots, each band its own orbit */
{
    uint32_t k, rate = vr_rate();
    vr_bg(dst, prev);
    for (k = 0; k < 32u; k++) {
        uint32_t a = k * 32u + vr_t * rate * (k + 2u) / 6u;
        int32_t r = 6 + vr_band[k] * vr_amp(34) / 1000;
        int32_t x = VR_CX + ((r * icos(a)) >> 15), y = VR_CY + ((r * 9 / 16 * isin(a)) >> 15);   /* seen at a tilt */
        vr_disc(dst, x, y, vr_band[k] > 700 ? 3 : 2, vr_pal(k * 2u + vr_hue, 3));
    }
}

static void vr_plasma(uint8_t *dst)                     /* 15 MOIRE: an XOR moire, the sound its speed */
{
    static uint32_t t;
    uint32_t x, y, shade = (uint32_t)vr_peak >> 13;
    t += vr_rate() + ((uint32_t)vr_peak >> 11);
    if (shade > 3u)
        shade = 3u;
    for (y = 0; y < VR_H; y++) {
        uint8_t *row = dst + y * VR_W;
        uint32_t yy = y + (t >> 1);
        for (x = 0; x < VR_W; x++) {
            uint32_t xx = x + t, v = (xx ^ yy) + ((x * y) >> (5u + vr_k[0] / 128u)) + (t << 1) + vr_hue;
            uint32_t l = ((v >> 6) + shade + vr_flash) & 3u;
            row[x] = (uint8_t)(l ? (v & 63u) | (l << 6) : 0u);
        }
        if ((y & 15u) == 15u)
            vcore_yield();
    }
}

#include "vproc.c"                                       /* the procedural modes, palettes, overlays (16..29) */

/* SCREEN's palette: the display copy's 256 colours (mirror.c, tools/gen_mirror.py), dimmed by the fade */
static void vr_screen_lut(vjpeg_ycc_t *lut, uint32_t b)
{
    uint32_t i;
    for (i = 0; i < 256u; i++) {
        int32_t r = (int32_t)(MIR_RGB[i][0] * b >> 8), g = (int32_t)(MIR_RGB[i][1] * b >> 8), bl = (int32_t)(MIR_RGB[i][2] * b >> 8);
        lut[i].y = (uint8_t)((77 * r + 150 * g + 29 * bl + 128) >> 8);
        lut[i].cb = (uint8_t)(128 + ((-43 * r - 85 * g + 128 * bl + 128) >> 8));
        lut[i].cr = (uint8_t)(128 + ((128 * r - 107 * g - 21 * bl + 128) >> 8));
        lut[i].pad = 0;
    }
}

/* one frame: dst gets the picture, prev is the last frame (trails), lut / p565 the frame's palette (vcore.c).
 * -> 1: SCREEN: nothing drawn (dst and prev are the display's copy then), lut its colours: encode the copy.
 * SCREEN fades like the pictures (through lut). Into it: the old picture fades out, then the job asks core 0
 * for the copy (vcore_mirror) and stays black until it is there. Out of it: once faded, core 0 must stop
 * copying before the RAM is the frames' again (cleared: the next picture starts from black). */
static void vr_analyse(void)                            /* the sound, for the visual modes */
{
    vr_snapshot();
    vcore_yield();
    vr_fft();
    vr_bands();
}

static int vrender_frame(uint8_t *dst, const uint8_t *prev, vjpeg_ycc_t *lut, uint16_t *p565)
{
    uint32_t mode, layout, phase, want, heard = vr_mode_last != VIS_SCREEN;
    if (heard)                                          /* (SCREEN drawn: its copy has the frames' RAM, the analysis'
                                                         * buffers in it, until core 0 has stopped copying) */
        vr_analyse();
    vr_inputs();
    fm1_c1_sync();
    want = mode = vis_style % VR_MODES;
    vr_pal_cur = vc0.pal % VIS_NPAL;
    if (vr_mode_last == 0xFFu) {                        /* the first frame: no fade */
        vr_mode_last = mode;
        vr_fresh = 1;
        vr_name_t = 90;
    } else if (mode != vr_mode_last) {                  /* a new mode: the old one fades out (4 frames), then the new */
        vr_bright = vr_bright > VR_FADE_OUT ? vr_bright - VR_FADE_OUT : 0u;   /* one comes up (another change on the
                                                         * way down: it fades to the latest) */
        if (!vr_bright && (vr_mode_last != VIS_SCREEN || vcore_mirror(0))) {
            if (vr_mode_last == VIS_SCREEN) {           /* (the copy given back: the frames from black) */
                memset(dst, 0, VF_N);
                memset((void *)prev, 0, VF_N);
            }
            vr_mode_last = mode;
            vr_fresh = 1;
            vr_name_t = 90;
        }
    } else if (vr_bright < 256u)
        vr_bright = vr_bright + VR_FADE_IN < 256u ? vr_bright + VR_FADE_IN : 256u;
    mode = vr_mode_last;                                /* (what is drawn: the fading one) */
    if (mode == VIS_SCREEN) {
        if (want == VIS_SCREEN && !vcore_mirror(1))
            vr_bright = 0;                              /* (black until core 0 copies the display; leaving it, the
                                                         * copy is not asked for again) */
        vr_screen_lut(lut, vr_bright);
        if (vr_name_t)
            vr_name_t--;
        vr_fresh = 0;
        vr_t++;
        return 1;
    }
    if (!heard)
        vr_analyse();                                   /* (back from SCREEN: the RAM given back this frame) */
    if (vr_pal_cur != vr_pal_last) {
        if (vr_pal_last != 0xFFu)
            vr_palname_t = 90;
        vr_pal_last = vr_pal_cur;
    }
    layout = mode >= VIS_FIRST_PROC ? VP_INT : VP_HUE;
    if (layout == VP_INT) {
        vr_cyc += VR_CYCLE[mode - VIS_FIRST_PROC] * (1u + vr_k[1] / 32u);
        phase = vr_k[3] + (vr_cyc >> 1);
    } else
        phase = 0;
    vr_palette_build(lut, p565, layout, vr_pal_cur, phase, vr_t * 4u, vr_bright);
    switch (mode) {
    case 0: vr_wave(dst, prev); break;
    case 1: vr_bars(dst, prev); break;
    case 2: vr_heat(dst, prev); break;
    case 3: vr_xy(dst, prev); break;
    case 4: vr_meters(dst, prev); break;
    case 5: vr_ringscope(dst, prev); break;
    case 6: vr_spiral(dst, prev); break;
    case 7: vr_grid(dst, prev); break;
    case 8: vr_kit(dst, prev); break;
    case 9: vr_tunnel(dst, prev); break;
    case 10: vr_mirror(dst, prev); break;
    case 11: vr_stars(dst, prev); break;
    case 12: vr_squares(dst, prev); break;
    case 13: vr_shards(dst, prev); break;
    case 14: vr_orbits(dst, prev); break;
    case 15: vr_plasma(dst); break;
    case 16: vp_warp(dst, prev); break;
    case 17: vp_vortex(dst, prev); break;
    case 18: vp_ripple(dst, prev); break;
    case 19: vp_kaleido(dst, prev); break;
    case 20: vp_melt(dst, prev); break;
    case 21: vp_smoke(dst, prev); break;
    case 22: vp_wormhole(dst, prev); break;
    case 23: vp_plasma(dst); break;
    case 24: vp_fire(dst, prev); break;
    case 25: vp_ridges(dst); break;
    case 26: vp_lava(dst); break;
    case 27: vp_cells(dst); break;
    case 28: vp_wireframe(dst, prev); break;
    default: vp_fresnel(dst); break;                    /* (29; 30 SCREEN returned above) */
    }
    if (vr_name_t)
        vr_name_t--;
    if (vr_palname_t)
        vr_palname_t--;
    vr_fresh = 0;
    vr_t++;
    return 0;
}

/* the palette before the first frame (vcore.c vcore_start): the hue layout, the palette set */
static void vrender_palette_init(vjpeg_ycc_t *lut, uint16_t *p565) { vr_palette_build(lut, p565, VP_HUE, vis_pal_live % VIS_NPAL, 0, 0, 256); }
