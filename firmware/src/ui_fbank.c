/* SPDX-License-Identifier: GPL-3.0-only
 * SMK-37 Pro: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The FADER BANK screen: the five banks, the active one lit, what faders 1..4 set (track colours; the VISUAL bank,
 * Synthesthetic's: the visualizer's four knobs, as on its screen).
 * FADER BANK opens it and then steps the bank, as < > and SELECT (K2, or K6) do; PATCH closes it, or it closes by itself. */
#ifndef FB_N                                      /* (ui_input.c; the host tests include this file alone) */
#define FB_N 5u
#define FB_VIS 4u
#endif
static const char *const FB_NAME[FB_N] = {"LEVEL", "FILT", "REV", "DLY", "VISUAL"};   /* (SLOOP's short names: five boxes of 48 px) */
#define FB_SHOW_MS 4000u                           /* up this long after the last change */
static uint8_t fb_open;
static uint32_t fb_t, fb_frame;                    /* last action (ms); the frame drawn last */

static int fb_shown(void)
{
    return fb_open && !ui.menu && ui.layer == LY_PLAY && !ui.hold_kind;
}
static void fb_set(uint32_t b)                     /* bank b, the screen up */
{
    smk_fbank = (uint8_t)(b % FB_N);
    smk_fbank_new = 1;
    fb_open = 1;
    fb_t = fm1_ms;
    ui.force = 1;
}
static void fb_close(void) { fb_open = 0; }
static void fb_expire(void)                        /* up FB_SHOW_MS after the last change */
{
    if (fb_open && fm1_ms - fb_t > FB_SHOW_MS)
        fb_open = 0, ui.force = 1;
}
static int fb_left(void)                           /* drawn last frame, not now: the page needs a redraw */
{
    int l = fb_frame && fb_frame + 1u == ui.frame;
    if (l)
        fb_frame = 0;
    return l;
}

/* what fader i sets in bank b (main.c smk_faders) */
static const param_desc_t *fb_param(uint32_t b, uint32_t i, int16_t **vp)
{
    static const uint8_t TID[4] = {P_LEVEL, P_TFLT, P_REV, P_DLY}, GID[4] = {G_DRLVL, 0, G_DRREV, G_DRDLY};
    if (i == TRK_DRUM && b != 1u) {
        *vp = &song.g[GID[b & 3u]];
        return &GP[GID[b & 3u]];
    }
    *vp = &trk[i].p[TID[b & 3u]];
    return &TP[TID[b & 3u]];
}

static void fb_draw(void)
{
    static uint32_t shown, shown_bank = 99;
    uint32_t b = smk_fbank % FB_N, i, half, sig = b * 7919u;
    int16_t *vp;
    if (fb_frame + 1u != ui.frame) {               /* another screen was up: all of it */
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
    fb_frame = ui.frame;
    for (i = 0; i < 4u; i++) {
        if (b == FB_VIS) {
            sig = sig * 31u + vis_knob[i];
            continue;
        }
        fb_param(b, i, &vp);
        sig = sig * 31u + (uint32_t)(*vp + 200);
    }
    if (!ui.force && sig == shown)
        return;
    if (!ui.force && (ui.frame & 1u))              /* a fader moving: every other frame (a redraw a frame flickers) */
        return;
    for (half = 0; half < 2u; half++) {
        if (!half && !ui.force && shown_bank == b)    /* the same bank: its boxes stay, only the faders' half is drawn */
            continue;
        cv_begin(240, 120, C_BLACK);
        if (!half) {
            cv_text(4, 2, &FONT_L, "FADER BANK", C_WHITE);
            for (i = 0; i < FB_N; i++) {           /* the banks (five of 48 px) */
                int32_t x = (int32_t)i * 48, on = i == b;
                char l[2] = {(char)('A' + i), 0};
                cv_rect(x, 42, 47, 74, on ? C_WHITE : TE_G2);
                if (!on)
                    cv_rect(x + 1, 43, 45, 72, TE_G1);
                cv_text(x + 23 - text_w(&FONT_L, l) / 2, 48, &FONT_L, l, on ? C_BLACK : TE_G3);
                cv_text(x + 23 - text_w(&FONT_S, FB_NAME[i]) / 2, 90, &FONT_S, FB_NAME[i], on ? C_BLACK : TE_G3);
            }
        } else {
            for (i = 0; i < 4u; i++) {             /* the faders: F1 TRACK 1 ... value, a bar */
                int32_t y = 4 + (int32_t)i * 24, w;
                char lab[10] = "TRACK 1", val[12], v2[20];
                const char *unit = "";
                const param_desc_t *d = 0;
                char f[3] = {'F', (char)('1' + i), 0};
                if (b == FB_VIS) {                      /* the visualizer's knobs: 0..100, as its own bar shows them */
                    str_cpy(lab, VIS_KNOB[i], sizeof lab);
                    fmt_int(v2, (int32_t)vis_knob[i] * 100 / 255);
                    w = (int32_t)vis_knob[i] * 204 / 255;
                } else {
                    d = fb_param(b, i, &vp);
                    if (i == TRK_DRUM)
                        str_cpy(lab, "DRUMS", sizeof lab);
                    else
                        lab[6] = (char)('1' + i);
                    param_format(d, *vp, val, &unit);
                    str_cpy(v2, val, sizeof v2);
                    str_cpy(v2 + str_len(v2), unit, sizeof v2 - str_len(v2));
                    w = RATIO(d, *vp) * 204 / 1000;
                }
                cv_text(4, y, &FONT_S, f, TE_COL[i]);
                cv_text(32, y, &FONT_S, lab, TE_COL[i]);
                cv_text(236 - text_w(&FONT_S, v2), y, &FONT_S, v2, C_WHITE);
                cv_rect(32, y + 17, 204, 3, TE_G1);
                cv_rect(32, y + 17, w < 1 ? 1 : w, 3, TE_COL[i]);
            }
            {
                char h[24] = "< >  K2  FADER BANK";
                h[6] = knob_digit(EN_SELECT);          /* (SELECT: K2, or K6, HOME menu > SCREEN > KNOB LAYOUT) */
                cv_text((240 - text_w(&FONT_S, h)) / 2, 100, &FONT_S, h, TE_G3);
            }
        }
        cv_blit(0, half * 120u);
    }
    shown = sig;
    shown_bank = b;
}
