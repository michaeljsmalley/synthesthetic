/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthesthetic's visualizer (firmware/src/vrender.c, vproc.c) on the host: a synthetic mix (kick, bass, lead, hats)
 * through the scope rings with the sequencer's triggers, every one of the 30 modes rendered for a run of frames
 * with its own palette, the last ones written as PPM for tests/vrender_sheet.py (a contact sheet to look at).
 * Checks: the analyser's calibration (pure tones land in their band at their decibel height), every mode draws
 * from the sound, the classic modes draw little from silence, every palette fills its indices, the overlay goes
 * away again byte for byte, a mode change fades out and in, the job yields inside every frame, and the
 * time per frame here, for scale only.
 *
 *   cc -O2 -Wall -Ibuild/gen -o build/host/vrender_test tests/vrender_test.c -lm && build/host/vrender_test build/host
 * Exit status: the number of failed checks. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "felucca_tables.h"                                  /* SINE[1024], Q15 */
#include "../firmware/src/vjpeg.c"                           /* (vjpeg_ycc_t) */
typedef struct {                                             /* gfx.c's font record (the title card uses FONT_S) */
    uint8_t h, pad, first, last, sh;
    const uint8_t *adv, *bw;
    const uint16_t *off;
    const uint8_t *data;
    uint8_t bits;
} felucca_font_t;
#include "felucca_font.h"

/* what vrender.c takes from the firmware around it */
#define VF_W 160u
#define VF_H 90u
#define VF_N (VF_W * VF_H)
#define SCOPE_N 1024u
#include "../firmware/src/vis_set.h"
static int16_t scope_buf[SCOPE_N], scope_bufr[SCOPE_N];
static uint32_t scope_w;
static uint8_t vis_style;
static uint32_t yields;
static void vcore_yield(void) { yields++; }
static void fm1_c1_sync(void) {}
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static struct {                                              /* vcore.c vc0, the fields the renderer reads */
    uint32_t level, notes, buttons, knob, hitc[4], kickc, note, tpk[4], beat_q8, playing, drumc[9];
    uint32_t var, pal, bpm, hud, pulsec;
} vc0;
#include "vmirror.h"                                         /* SCREEN's palette (tools/gen_mirror.py) */
static uint32_t mirror_want, mirror_on, mirror_again;        /* SCREEN: what the job asks of core 0, what core 0 does */
static int vcore_mirror(uint32_t on)                         /* (vcore.c) */
{
    if (mirror_want == 0u && on == 1u && mirror_on == 1u)
        mirror_again++;                                      /* asked for again while core 0 is letting go */
    mirror_want = on;
    return mirror_on == on;
}
#include "../firmware/src/vrender.c"

static int bad;
static void check(int ok, const char *what) { printf("vrender: %-72s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

static uint8_t rgb[256][3];
static uint8_t fb[2][VF_N];
static vjpeg_ycc_t lut[2][256];
static uint16_t p565[2][256];
static const char *const NAME[VIS_N] = {"wave", "bars", "heat", "xy", "meters", "ring", "spiral", "grid", "kit",
    "tunnel", "mirror", "stars", "squares", "shards", "orbits", "moire", "warp", "vortex", "ripple", "kaleido", "melt",
    "smoke", "wormhole", "plasma", "fire", "ridges", "lava", "cells", "wireframe", "fresnel", "screen"};

/* the mix at 22 kHz: a kick every half second, a bass note, a lead with its fifth, hats; t in samples */
static uint32_t rnd = 12345;
static int32_t mix_sample(uint32_t t, int side)
{
    uint32_t beat = t % 11025u;                              /* 0.5 s */
    int32_t kick = 0, bass, lead, hat = 0;
    if (beat < 4400u) {
        uint32_t ph = (beat * (120u - beat / 60u)) / 8u;     /* a falling pitch */
        kick = (isin(ph) * (int32_t)(4400u - beat)) / 4400;
    }
    bass = isin(t * 1024u * 55u / 22050u) / 3 + isin(t * 1024u * 110u / 22050u) / 6;
    lead = isin(t * 1024u * 440u / 22050u) / 5 + isin(t * 1024u * 659u / 22050u) / 7;
    if ((t % 5512u) < 600u) {
        rnd = rnd * 1103515245u + 12345u;
        hat = (int32_t)((rnd >> 16) & 0x3FFFu) - 8192;
        hat = hat * (int32_t)(600u - t % 5512u) / 1200;
    }
    return clamp((kick * 7 / 10 + bass + (side ? lead : lead * 2 / 3) + hat) * 4 / 5, -32767, 32767);
}

static void feed(uint32_t frame, int silent)                 /* the rings get the samples before "now" */
{
    uint32_t i, now = 2000u + frame * 1102u;                 /* 20 frames a second */
    for (i = 0; i < SCOPE_N; i++) {
        uint32_t t = now - SCOPE_N + i;
        scope_bufr[scope_w & (SCOPE_N - 1u)] = (int16_t)(silent ? 0 : mix_sample(t, 1));
        scope_buf[scope_w++ & (SCOPE_N - 1u)] = (int16_t)(silent ? 0 : mix_sample(t, 0));
    }
}

static void tone(double f1, double db1)                      /* a pure sine into the rings */
{
    uint32_t i;
    double a1 = 32767.0 * pow(10, db1 / 20);
    for (i = 0; i < SCOPE_N; i++)
        scope_buf[i] = (int16_t)clamp((int32_t)(a1 * sin(2 * M_PI * f1 * i / 22050.0)), -32767, 32767);
    scope_w = 0;
}

static uint32_t lit(const uint8_t *f, uint32_t mode)       /* pixels well above black, in the mode's layout */
{
    uint32_t i, n = 0;
    for (i = 0; i < VF_N; i++)
        n += mode >= VIS_FIRST_PROC ? f[i] >= VR_I0 + 60u : (f[i] >> 6) > 1u;
    return n;
}

static void write_ppm(const char *dir, const char *name, const uint8_t *f)
{
    char path[512];
    uint32_t i;
    FILE *fp;
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    fp = fopen(path, "wb");
    if (!fp)
        return;
    fprintf(fp, "P6\n%u %u\n255\n", VF_W, VF_H);
    for (i = 0; i < VF_N; i++)
        fwrite(rgb[f[i]], 1, 3, fp);
    fclose(fp);
}

static uint32_t cur;
static int screen_out;                                       /* the last frame was SCREEN's (encode the display's copy) */
static void frame_one(void)                                  /* a frame into fb[cur], its palette into rgb */
{
    vr_rgb_out = rgb;
    screen_out = vrender_frame(fb[cur], fb[cur ^ 1u], lut[cur], p565[cur]);
    cur ^= 1u;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/host";
    uint32_t mode, f, i;
    char what[160];
    /* the analyser: pure tones land in their band, at their decibel height (100 per 6 dB, 1000 = 0 dBFS) */
    {
        static const double F[] = {55, 110, 440, 1760, 7040};
        static const double D[] = {0, -6, -12, -24, -48, -54};
        for (i = 0; i < 5u; i++) {
            uint32_t k, best = 0;
            int32_t m = -1;
            double lo, hi;
            tone(F[i], -6);
            vr_snapshot(); vr_fft(); vr_bands();
            for (k = 0; k < 32u; k++)
                if (vr_band[k] > m) { m = vr_band[k]; best = k; }
            lo = VR_EDGE[best] * 22050.0 / 1024; hi = VR_EDGE[best + 1] * 22050.0 / 1024;
            snprintf(what, sizeof what, "analyser: %5.0f Hz at -6 dBFS -> band %2u (%5.0f-%5.0f Hz) height %4d", F[i], best, lo, hi, m);
            /* a tone sits in its band, or the next one when the bands are a bin wide (the window's lobe) */
            check((lo <= F[i] && F[i] < hi) || (best && VR_EDGE[best - 1] * 22050.0 / 1024 <= F[i] && F[i] < lo && hi - lo < 25),
                  what);
            check(m >= 850 && m <= 950, "analyser: a -6 dBFS tone is 900 +-50 high (the wide bands add leakage)");
        }
        for (i = 0; i < 5u; i++) {                          /* (-54 dB: the int16 FFT's floor, not checked) */
            uint32_t k;
            int32_t m = -1, want = (int32_t)(1000 + D[i] * 1000 / 60);
            tone(440, D[i]);
            vr_snapshot(); vr_fft(); vr_bands();
            for (k = 0; k < 32u; k++)
                if (vr_band[k] > m) m = vr_band[k];
            snprintf(what, sizeof what, "analyser: 440 Hz at %3.0f dBFS -> height %4d (want %4d +-40)", D[i], m, want);
            check(m >= want - 40 && m <= want + 40, what);
        }
        check(vr_db(32767) >= 980 && vr_db(16384) == 900 && vr_db(32) == 0, "meters: 0 dBFS = 1000 (987: the log is truncated), -6 dB = 900, -60 dB = 0");
    }
    vc0.knob = 128u | 128u << 8 | 160u << 16;                /* AMOUNT, RATE, TRAILS, COLOR at their defaults */
    vc0.bpm = 120;
    for (mode = 0; mode < VIS_SCREEN; mode++) {             /* (the pictures; SCREEN below) */
        uint32_t n0, n1, us, bad_idx = 0;
        clock_t c0;
        vis_style = (uint8_t)mode;
        vc0.var = mode * 7u;                                 /* (a variation each, as the autopilot would pick) */
        vc0.pal = mode % VIS_NPAL;
        memset(fb, 0, sizeof fb);
        vr_t = 0;
        scope_w = 0;
        for (i = 0; i < 4u; i++)
            vc0.tpk[i] = 0;
        for (f = 0; f < 10u; f++) {                          /* silence first (the channel change plays out) */
            feed(f, 1);
            frame_one();
        }
        n0 = lit(fb[cur ^ 1u], mode);
        c0 = clock();
        for (f = 10; f < 40u; f++) {                         /* then the music, 1.5 s, with the sequencer's notes */
            feed(f, 0);
            if (f % 3u == 0u)
                vc0.hitc[f % 4u]++;
            if (f % 8u == 0u) {
                vc0.kickc++;
                vc0.drumc[0]++;
            }
            vc0.drumc[1 + f % 8u]++;
            for (i = 0; i < 4u; i++)
                vc0.tpk[i] = 6000u + i * 5000u;
            vc0.note = 36u | 60u << 8 | 67u << 16 | 36u << 24;
            vc0.beat_q8 = f * 128u;
            frame_one();
            if (f >= 36u) {
                snprintf(what, sizeof what, "%02u-%s-%u", mode, NAME[mode], f);
                write_ppm(dir, what, fb[cur ^ 1u]);
            }
            if (mode >= VIS_FIRST_PROC)                      /* an intensity mode draws intensities only */
                for (i = 0; i < VF_N; i++)
                    bad_idx += fb[cur ^ 1u][i] != 0u && fb[cur ^ 1u][i] < VR_I0;
        }
        us = (uint32_t)((clock() - c0) * 1000000u / CLOCKS_PER_SEC / 30u);
        n1 = lit(fb[cur ^ 1u], mode);
        snprintf(what, sizeof what, "%-9s silent %5u px lit, music %5u px lit, %4u us/frame (host)", NAME[mode], n0, n1, us);
        check(n1 > 150u && (mode >= VIS_FIRST_PROC || mode == 9u || mode == 15u || n0 < VF_N / 4u), what);   /* (TUNNEL,
                                                                   * MOIRE: always moving) */
        if (mode >= VIS_FIRST_PROC) {
            snprintf(what, sizeof what, "%-9s draws in its intensity layout only (%u stray indices)", NAME[mode], bad_idx);
            check(bad_idx == 0u, what);
        }
    }
    {   /* every palette: black at 0, the overlay colours fixed, the intensity ramp rising from black */
        uint32_t pal, ok = 1;
        for (pal = 0; pal < VIS_NPAL; pal++) {
            vr_rgb_out = rgb;
            vr_palette_build(lut[0], p565[0], VP_INT, pal, 0, 0, 256);
            ok &= rgb[0][0] == 0 && rgb[VR_I0][0] == 0 && rgb[VR_I0][1] == 0 && rgb[VR_I0][2] == 0;
            ok &= rgb[VR_OV_WHITE][0] == 255 && rgb[VR_OV_RED][0] == 255 && rgb[VR_OV_BAR][0] == 191;
            ok &= (uint32_t)rgb[255][0] + rgb[255][1] + rgb[255][2] > 200u;
            vr_palette_build(lut[0], p565[0], VP_HUE, pal, 0, 0, 256);
            ok &= rgb[0][0] == 0 && rgb[33][0] + rgb[33][1] + rgb[33][2] > 0u && rgb[63 - 8][0] == 0;
        }
        check(ok, "palettes: black at 0, overlays fixed, the ramp rises from black, hue levels lit");
    }
    {   /* the overlay: drawn for the encode, taken off byte for byte after it */
        static uint8_t keep[VF_N];
        uint32_t diff = 0, ov = 0;
        vis_style = 16;
        vc0.hud = 3;
        vc0.playing = 1;
        for (f = 0; f < 12u; f++) {
            feed(f, 0);
            frame_one();
        }
        memcpy(keep, fb[cur ^ 1u], VF_N);
        vr_overlay(fb[cur ^ 1u]);
        for (i = 0; i < VF_N; i++)
            ov += fb[cur ^ 1u][i] != keep[i];
        write_ppm(dir, "overlay", fb[cur ^ 1u]);
        vr_overlay_restore(fb[cur ^ 1u]);
        for (i = 0; i < VF_N; i++)
            diff += fb[cur ^ 1u][i] != keep[i];
        snprintf(what, sizeof what, "overlay: HUD and title drawn (%u px), then taken off (%u px left)", ov, diff);
        check(ov > 400u && diff == 0u, what);
        vc0.hud = 0;
        vc0.playing = 0;
    }
    {   /* a mode change fades: the old mode's colours down in 4 frames, then the new one's up in 5; the overlay's
         * colours never fade */
        uint32_t lv[12], k, down = 1, up = 1, ov = 1;
        vis_style = 16;
        vc0.pal = 0;
        for (f = 0; f < 8u; f++) {                           /* settled on WARP, full brightness */
            feed(f, 0);
            frame_one();
        }
        vis_style = 23;                                      /* to PLASMA */
        for (k = 0; k < 12u; k++) {
            feed(8u + k, 0);
            frame_one();
            lv[k] = (uint32_t)rgb[255][0] + rgb[255][1] + rgb[255][2];   /* the brightest intensity's colour */
            ov &= rgb[VR_OV_WHITE][0] == 255u && rgb[VR_OV_RED][0] == 255u;
        }
        for (k = 1; k < 4u; k++)
            down &= lv[k] < lv[k - 1u];
        for (k = 5; k < 9u; k++)
            up &= lv[k] > lv[k - 1u];
        snprintf(what, sizeof what, "a mode change fades: down %u %u %u %u, black %u, up %u %u %u %u %u",
                 lv[0], lv[1], lv[2], lv[3], lv[4], lv[5], lv[6], lv[7], lv[8], lv[9]);
        check(down && lv[3] == 0u && up && vr_mode_last == 23u, what);
        check(ov, "a mode change: the overlay's colours stay as they are");
        write_ppm(dir, "fade-mid", fb[cur ^ 1u]);
    }
    {   /* SCREEN: nothing drawn, the display's copy encoded (1); in from a picture: it fades out, the copy is asked
         * for, black until core 0 copies, then up; out: down, the copy given back (never asked for again on the
         * way), and the frames cleared before the next picture */
        uint32_t k, in_ok = 1, out_ok = 1, y[16], left = 0;
        const uint32_t WHITE = 215u;                         /* (MIR_RGB: the cube's 255 255 255) */
        vis_style = 16;
        mirror_want = mirror_on = mirror_again = 0;
        for (f = 0; f < 8u; f++) {
            feed(f, 0);
            frame_one();
        }
        vis_style = VIS_SCREEN;
        for (k = 0; k < 6u; k++) {                           /* WARP down; SCREEN asked for, core 0 not there yet */
            feed(8u + k, 0);
            frame_one();
            in_ok &= k < 3u ? !screen_out && mirror_want == 0u : screen_out && mirror_want == 1u && lut[cur ^ 1u][WHITE].y == 0u;
        }
        mirror_on = 1;                                       /* core 0 copies */
        for (k = 0; k < 7u; k++) {
            feed(14u + k, 0);
            frame_one();
            y[k] = lut[cur ^ 1u][WHITE].y;
            in_ok &= screen_out != 0;
        }
        for (k = 1; k < 5u; k++)
            in_ok &= y[k] > y[k - 1u];
        in_ok &= y[0] > 0u && y[5] == 255u && lut[cur ^ 1u][0].y == 0u && lut[cur ^ 1u][0].cb == 128u;
        snprintf(what, sizeof what, "SCREEN in: the picture fades out, copy asked for, black until there, up %u %u %u %u %u %u",
                 y[0], y[1], y[2], y[3], y[4], y[5]);
        check(in_ok, what);
        vis_style = 16;                                      /* back to WARP */
        for (k = 0; k < 6u; k++) {                           /* down; the copy given back; core 0 still copying */
            feed(21u + k, 0);
            frame_one();
            out_ok &= screen_out && (k < 3u ? mirror_want == 1u : mirror_want == 0u && lut[cur ^ 1u][WHITE].y == 0u);
        }
        memset(fb, 5, sizeof fb);                            /* (an overlay index: WARP never draws it) */
        mirror_on = 0;                                       /* core 0 stopped copying */
        feed(27u, 0);
        frame_one();
        for (i = 0; i < VF_N; i++)
            left += (fb[0][i] == 5u) + (fb[1][i] == 5u);
        out_ok &= !screen_out && vr_mode_last == 16u && left == 0u && mirror_again == 0u;
        snprintf(what, sizeof what, "SCREEN out: down, the copy given back first, the frames cleared (%u px left)", left);
        check(out_ok, what);
    }
    check(yields > VIS_SCREEN * 40u * 2u, "the job yields inside every frame (the flash can park it)");
    printf("vrender: %d failed\n", bad);
    return bad;
}
