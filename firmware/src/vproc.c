/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthesthetic's procedural pictures, palettes, overlays and transitions (included by vrender.c, on the video
 * core; everything here is Synthesthetic's own).
 *
 * The palette is rebuilt for every frame, into that frame's own JPEG and LCD tables (vcore.c), from the layout
 * its mode draws in, the palette (PRESETS on the visualiser, vis_set.h VIS_PAL_NAME) and a phase (COLOR, and
 * the palette cycling of the procedural modes):
 *   0                 black, always
 *   1..7              colour bars: white yellow cyan green magenta red blue, at 75 % (kept for the overlays)
 *   8..23             sixteen greys, 8 black, 23 white (static, text)
 *   24, 25            red, amber (the LIVE tag)
 *   32..47            the title card's letters, a hue each, turning
 *   HUE layout        64..255: 64 hues x levels 1..3 (vrender.c's modes 0..15)
 *   INT layout        48..255: an intensity 0..207 (48 black) along the palette's first five keys, brighter as it
 *                     rises (16..29); the cycling moves that window round the palette
 * The overlays use 1..47 in both layouts. They are drawn only for the encode and taken off again after it
 * (vr_overlay, vr_overlay_restore), so nothing of them reaches the feedback or the FM-1's screen. A change of
 * mode fades: the picture's colours (not the overlay's) go dark in 4 frames, the new mode comes up in 5
 * (vrender.c vrender_frame, the palette's bright).
 *
 * The warp engine is Milkdrop's per-vertex model: a mesh of 11 x 13 vertices, every 16 x 8 pixels, each telling
 * where its pixel comes from in the last frame (zoom, rotation, swirl, ripples, drift, wobble, kaleidoscope
 * folds); between vertices the source moves linearly, and each pixel is the last frame there, blended between
 * its four neighbours (4-bit fractions), through a decay table (TRAILS). On top, every frame, the sound. */
#include "vfont5.h"

#define VR_OV_BAR 1u
#define VR_OV_GRAY 8u
#define VR_OV_WHITE 23u
#define VR_OV_RED 24u
#define VR_OV_AMBER 25u
#define VR_OV_TITLE 32u
#define VR_I0 48u
#define VR_IMAX 207
enum { VP_HUE, VP_INT };

/* the palettes: eight colours round a circle, blended */
static const uint8_t VR_PAL_KEY[VIS_NPAL][8][3] = {
    {{255, 0, 0}, {255, 140, 0}, {255, 240, 0}, {40, 255, 40}, {0, 230, 255}, {20, 60, 255}, {150, 0, 255}, {255, 0, 170}},
    {{255, 0, 255}, {255, 0, 128}, {150, 0, 255}, {0, 90, 255}, {0, 255, 255}, {0, 90, 255}, {150, 0, 255}, {255, 0, 128}},
    {{120, 0, 0}, {220, 20, 0}, {255, 90, 0}, {255, 170, 0}, {255, 240, 60}, {255, 255, 200}, {255, 170, 0}, {220, 20, 0}},
    {{0, 20, 90}, {0, 70, 200}, {0, 140, 255}, {0, 220, 230}, {170, 255, 255}, {0, 220, 230}, {0, 140, 255}, {0, 70, 200}},
    {{0, 70, 10}, {0, 140, 25}, {0, 210, 45}, {40, 255, 80}, {170, 255, 170}, {40, 255, 80}, {0, 210, 45}, {0, 140, 25}},
    {{255, 113, 206}, {185, 103, 255}, {1, 205, 254}, {5, 255, 161}, {255, 251, 150}, {5, 255, 161}, {1, 205, 254},
     {185, 103, 255}},
    {{0, 255, 0}, {255, 255, 0}, {255, 0, 255}, {0, 255, 255}, {255, 60, 0}, {120, 0, 255}, {200, 255, 0}, {0, 120, 255}},
    {{20, 0, 70}, {120, 0, 160}, {220, 0, 90}, {255, 70, 0}, {255, 190, 0}, {255, 255, 210}, {255, 190, 0}, {220, 0, 90}},
    {{40, 40, 48}, {90, 90, 100}, {150, 150, 160}, {210, 210, 220}, {255, 255, 255}, {210, 210, 220}, {150, 150, 160},
     {90, 90, 100}},
};
/* the procedural modes' palette cycling, a step a frame (times 1 + RATE / 8) */
static const uint8_t VR_CYCLE[VIS_SCREEN - VIS_FIRST_PROC] = {1, 1, 2, 2, 1, 1, 2, 6, 0, 0, 2, 1, 1, 4};

static uint8_t (*vr_rgb_out)[3];                        /* the host test: the palette in RGB too (0 on the FM-1) */

static void vr_grad(uint32_t pal, uint32_t pos, uint32_t *rgb)   /* pos 0..255 round the palette */
{
    const uint8_t (*k)[3] = VR_PAL_KEY[pal % VIS_NPAL];
    uint32_t a = (pos >> 5) & 7u, b = (a + 1u) & 7u, f = pos & 31u, c;
    for (c = 0; c < 3u; c++)
        rgb[c] = ((uint32_t)k[a][c] * (32u - f) + (uint32_t)k[b][c] * f) >> 5;
}

static void vr_hsv(uint32_t h, uint32_t *rgb)            /* a pure hue, 0..255 round */
{
    uint32_t seg = (h & 255u) / 43u, f = ((h & 255u) % 43u) * 255u / 42u;
    switch (seg) {
    case 0: rgb[0] = 255; rgb[1] = f; rgb[2] = 0; break;
    case 1: rgb[0] = 255 - f; rgb[1] = 255; rgb[2] = 0; break;
    case 2: rgb[0] = 0; rgb[1] = 255; rgb[2] = f; break;
    case 3: rgb[0] = 0; rgb[1] = 255 - f; rgb[2] = 255; break;
    case 4: rgb[0] = f; rgb[1] = 0; rgb[2] = 255; break;
    default: rgb[0] = 255; rgb[1] = 0; rgb[2] = 255 - f; break;
    }
}

/* the frame's palette: the JPEG's Y Cb Cr and the LCD's RGB565 (byte-swapped) */
static void vr_palette_build(vjpeg_ycc_t *lut, uint16_t *p565, uint32_t layout, uint32_t pal, uint32_t phase,
                             uint32_t tphase, uint32_t bright)   /* bright: the picture's, 0..256 (the fade) */
{
    static const uint8_t BAR[7][3] = {{191, 191, 191}, {191, 191, 0}, {0, 191, 191}, {0, 191, 0}, {191, 0, 191},
                                      {191, 0, 0}, {0, 0, 191}};
    uint32_t i;
    for (i = 0; i < 256u; i++) {
        uint32_t rgb[3] = {0, 0, 0}, c;
        int32_t r, g, b;
        if (i == 0u) {
        } else if (i < VR_OV_GRAY) {
            rgb[0] = BAR[i - 1u][0]; rgb[1] = BAR[i - 1u][1]; rgb[2] = BAR[i - 1u][2];
        } else if (i <= VR_OV_WHITE) {
            rgb[0] = rgb[1] = rgb[2] = (i - VR_OV_GRAY) * 17u;
        } else if (i == VR_OV_RED) {
            rgb[0] = 255; rgb[1] = 40; rgb[2] = 40;
        } else if (i == VR_OV_AMBER) {
            rgb[0] = 255; rgb[1] = 176; rgb[2] = 0;
        } else if (i >= VR_OV_TITLE && i < VR_OV_TITLE + 16u) {
            vr_hsv(tphase + (i - VR_OV_TITLE) * 14u, rgb);
        } else if (layout == VP_HUE) {
            if (i >= 64u) {
                uint32_t lvl = (i >> 6) * bright;       /* (levels 1..3, times the fade) */
                vr_grad(pal, (i & 63u) * 4u + phase, rgb);
                rgb[0] = rgb[0] * lvl / 768u; rgb[1] = rgb[1] * lvl / 768u; rgb[2] = rgb[2] * lvl / 768u;
            }
        } else if (i >= VR_I0) {
            uint32_t I = i - VR_I0, br = (I < 51u ? I * 5u : 255u) * bright;
            vr_grad(pal, phase + I * 10u / 13u, rgb);     /* (the first five eighths of the round: dark to bright) */
            rgb[0] = rgb[0] * br / 65280u; rgb[1] = rgb[1] * br / 65280u; rgb[2] = rgb[2] * br / 65280u;
        }
        r = (int32_t)rgb[0]; g = (int32_t)rgb[1]; b = (int32_t)rgb[2];
        lut[i].y = (uint8_t)((77 * r + 150 * g + 29 * b + 128) >> 8);
        lut[i].cb = (uint8_t)(128 + ((-43 * r - 85 * g + 128 * b + 128) >> 8));
        lut[i].cr = (uint8_t)(128 + ((128 * r - 107 * g - 21 * b + 128) >> 8));
        lut[i].pad = 0;
        c = ((uint32_t)r >> 3) << 11 | ((uint32_t)g >> 2) << 5 | (uint32_t)b >> 3;
        p565[i] = (uint16_t)((c >> 8) | (c << 8));
        if (vr_rgb_out) {
            vr_rgb_out[i][0] = (uint8_t)r; vr_rgb_out[i][1] = (uint8_t)g; vr_rgb_out[i][2] = (uint8_t)b;
        }
    }
}

/* ---- small integer geometry ---- */
static uint32_t vr_isqrt(uint32_t x)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x)
        b >>= 2;
    while (b) {
        if (x >= r + b) {
            x -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
        b >>= 2;
    }
    return r;
}

static uint32_t vr_angle(int32_t dx, int32_t dy)       /* 1024 a turn, 0 along +x, 256 down the screen (+y) */
{
    uint32_t ax = (uint32_t)(dx < 0 ? -dx : dx), ay = (uint32_t)(dy < 0 ? -dy : dy), t, a;
    if (!ax && !ay)
        return 0;
    if (ax >= ay) {
        t = (ay << 8) / ax;                             /* tan, Q8: atan(t) ~ 128 t + 45 t (1 - t), in 1/1024 turns */
        a = (t * 128u >> 8) + ((t * (256u - t) * 45u) >> 16);
    } else {
        t = (ax << 8) / ay;
        a = 256u - ((t * 128u >> 8) + ((t * (256u - t) * 45u) >> 16));
    }
    if (dx < 0)
        a = 512u - a;
    if (dy < 0)
        a = (1024u - a) & 1023u;
    return a & 1023u;
}

static uint8_t vr_iv(int32_t i) { return (uint8_t)(VR_I0 + (uint32_t)(i < 0 ? 0 : i > VR_IMAX ? VR_IMAX : i)); }

/* ---- the warp engine ---- */
#define WG_X 11
#define WG_Y 13
static int32_t wu[WG_Y][WG_X], wv[WG_Y][WG_X];          /* each vertex's source, pixels Q8 */
static uint8_t wdec[256];                               /* a blended raw index -> the next frame's (fading) */
typedef struct {
    int32_t cx, cy;                                     /* the centre, pixels Q8 */
    int32_t zoom;                                       /* 1 / zoom, Q12: under 4096 the picture grows (zooms in) */
    int32_t rot;                                        /* rotation a frame, 1/16 of a 1/1024 turn */
    int32_t swirl, rsw;                                 /* more rotation at the centre (same unit), none past rsw px */
    int32_t rip;                                        /* a wave in the zoom along the radius (Q12), its step, phase */
    uint32_t ripk, ripph;
    int32_t tx, ty;                                     /* a drift, pixels Q8 */
    int32_t wobx, wobk, wobph;                          /* x wobbling along y: pixels Q8, step, phase */
    int32_t woby, wobyk;                                /* y wobbling along x */
    uint32_t fold;                                      /* kaleidoscope: mirrored wedges (0: none) */
} warp_t;

static void warp_mesh(const warp_t *w)
{
    uint32_t gx, gy;
    for (gy = 0; gy < (uint32_t)WG_Y; gy++)
        for (gx = 0; gx < (uint32_t)WG_X; gx++) {
            int32_t x8 = (int32_t)(gx * 16u) << 8, y8 = (int32_t)(gy * 8u) << 8;
            int32_t dx8 = x8 - w->cx, dy8 = y8 - w->cy, dx = dx8 >> 8, dy = dy8 >> 8;
            int32_t r = (int32_t)vr_isqrt((uint32_t)(dx * dx + dy * dy)), a = w->rot, z = w->zoom, c, s, rx, ry, u, v;
            if (w->swirl && r < w->rsw)
                a += w->swirl * (w->rsw - r) / w->rsw;
            if (w->rip)
                z += (w->rip * isin((uint32_t)r * w->ripk + w->ripph)) >> 15;
            if (w->fold) {
                uint32_t ang = vr_angle(dx, dy), wedge = 1024u / w->fold, k = ang / wedge, f = ang - k * wedge;
                if (k & 1u)
                    f = wedge - f;
                dx8 = (r * icos(f)) >> 7;
                dy8 = (r * isin(f)) >> 7;
            }
            c = icos((uint32_t)(a >> 4));
            s = isin((uint32_t)(a >> 4));
            rx = (dx8 * c - dy8 * s) >> 15;
            ry = (dx8 * s + dy8 * c) >> 15;
            rx = (rx * z) >> 12;
            ry = (ry * z) >> 12;
            u = w->cx + rx + w->tx;
            v = w->cy + ry + w->ty;
            if (w->wobx)
                u += (w->wobx * isin((uint32_t)(y8 >> 8) * (uint32_t)w->wobk + (uint32_t)w->wobph)) >> 15;
            if (w->woby)
                v += (w->woby * isin((uint32_t)(x8 >> 8) * (uint32_t)w->wobyk + (uint32_t)w->wobph * 3u / 2u)) >> 15;
            wu[gy][gx] = u;
            wv[gy][gx] = v;
        }
}

static void warp_decay(uint32_t trails)                 /* TRAILS: 0 a short glow .. 255 nearly forever */
{
    uint32_t keep = 200u + trails * 55u / 255u, sub = trails >= 250u ? 0u : 1u + (255u - trails) / 64u, m;
    for (m = 0; m < 256u; m++) {
        uint32_t i = m < VR_I0 ? 0u : m - VR_I0;
        i = (i * keep) >> 8;
        i = i > sub ? i - sub : 0u;
        wdec[m] = (uint8_t)(VR_I0 + i);
    }
}

/* the last frame through the mesh: border 0 clamps the source to the frame, 1 is black outside it */
static void warp_render(uint8_t *dst, const uint8_t *prev, uint32_t border)
{
    uint32_t gx, gy, r, c;
    for (gy = 0; gy + 1u < (uint32_t)WG_Y; gy++) {
        for (gx = 0; gx + 1u < (uint32_t)WG_X; gx++) {
            int32_t u00 = wu[gy][gx], u10 = wu[gy][gx + 1u], u01 = wu[gy + 1u][gx], u11 = wu[gy + 1u][gx + 1u];
            int32_t v00 = wv[gy][gx], v10 = wv[gy][gx + 1u], v01 = wv[gy + 1u][gx], v11 = wv[gy + 1u][gx + 1u];
            for (r = 0; r < 8u; r++) {
                uint32_t y = gy * 8u + r;
                int32_t uL, uR, vL, vR, du, dv;
                uint8_t *d;
                if (y >= VR_H)
                    break;
                uL = u00 + (((u01 - u00) * (int32_t)r) >> 3);
                uR = u10 + (((u11 - u10) * (int32_t)r) >> 3);
                vL = v00 + (((v01 - v00) * (int32_t)r) >> 3);
                vR = v10 + (((v11 - v10) * (int32_t)r) >> 3);
                du = (uR - uL) >> 4;
                dv = (vR - vL) >> 4;
                d = dst + y * VR_W + gx * 16u;
                for (c = 0; c < 16u; c++, uL += du, vL += dv) {
                    int32_t sx = uL >> 8, sy = vL >> 8;
                    uint32_t fx = ((uint32_t)uL >> 4) & 15u, fy = ((uint32_t)vL >> 4) & 15u, top, bot;
                    const uint8_t *p;
                    if ((uint32_t)sx > VR_W - 2u || (uint32_t)sy > VR_H - 2u) {
                        if (border) {
                            d[c] = (uint8_t)VR_I0;
                            continue;
                        }
                        sx = sx < 0 ? 0 : sx > (int32_t)VR_W - 2 ? (int32_t)VR_W - 2 : sx;
                        sy = sy < 0 ? 0 : sy > (int32_t)VR_H - 2 ? (int32_t)VR_H - 2 : sy;
                    }
                    p = prev + (uint32_t)sy * VR_W + (uint32_t)sx;
                    top = p[0] * (16u - fx) + p[1] * fx;
                    bot = p[VR_W] * (16u - fx) + p[VR_W + 1u] * fx;
                    d[c] = wdec[(top * (16u - fy) + bot * fy) >> 8];
                }
            }
        }
        vcore_yield();
    }
}

/* a warp step: the mesh, the decay, the last frame through them (the first frame of a mode: black) */
static void warp_step(uint8_t *dst, const uint8_t *prev, const warp_t *w, uint32_t border)
{
    if (vr_fresh) {
        memset(dst, (int)VR_I0, VF_N);
        return;
    }
    warp_mesh(w);
    warp_decay(vr_k[2]);
    warp_render(dst, prev, border);
}

/* ---- the sound on top ---- */
static void vp_ring_wave(uint8_t *f, int32_t cx, int32_t cy, int32_t r0, int32_t amp, int32_t I)
{
    uint32_t i;
    int32_t px = 0, py = 0;
    for (i = 0; i <= 128u; i++) {
        uint32_t a = (i & 127u) * 8u + vr_t * 3u;
        int32_t r = r0 + vr_l[(i * 4u) & (VR_N - 1u)] * amp / vr_gain;
        int32_t x = cx + ((r * icos(a)) >> 15), y = cy + ((r * isin(a)) >> 15);
        if (i)
            vr_line(f, px, py, x, y, vr_iv(I - (int32_t)(i & 31u)));
        px = x;
        py = y;
    }
}

static void vp_line_wave(uint8_t *f, int32_t y0, int32_t amp, int32_t I)
{
    uint32_t i, start = 0;
    int32_t x, py = y0, steep = 0;
    for (i = 1; i < VR_N / 2u; i++)
        if (vr_l[i - 1u] < 0 && vr_l[i] >= 0 && vr_l[i] - vr_l[i - 1u] > steep) {
            steep = vr_l[i] - vr_l[i - 1u];
            start = i;
        }
    for (x = 0; x < (int32_t)VR_W; x++) {
        int32_t y = y0 - vr_l[(start + (uint32_t)x * 3u) & (VR_N - 1u)] * amp / vr_gain;
        if (x)
            vr_line(f, x - 1, py, x, y, vr_iv(I - (y > y0 ? y - y0 : y0 - y)));
        py = y;
    }
}

/* ---- 16 WARP: the classic: the picture zooms in and turns, the wave round a ring, the kick a push ---- */
static void vp_warp(uint8_t *dst, const uint8_t *prev)
{
    static int32_t push;
    warp_t w = {0};
    int32_t bass = vr_group(0, 6), dir = (vr_var & 1u) ? -1 : 1, cx, cy;
    push = vr_bang ? 150 : push * 7 / 8;
    w.cx = (int32_t)(VR_CX << 8) + ((isin(vr_t * 2u) * 8) >> 7);
    w.cy = (int32_t)(VR_CY << 8) + ((icos(vr_t * 3u) * 5) >> 7);
    w.zoom = (vr_var & 2u) ? 4096 + 30 + push / 2 : 4096 - 24 - bass * 60 / 1000 - push;   /* in, or out */
    w.rot = dir * (8 + (int32_t)vr_rate() * 3);
    warp_step(dst, prev, &w, (vr_var & 2u) ? 1u : 0u);
    cx = w.cx >> 8;
    cy = w.cy >> 8;
    vp_ring_wave(dst, cx, cy, 10 + bass * 10 / 1000 + vr_amp(6), vr_amp(14), 200);
}

/* ---- 17 VORTEX: a whirlpool, strongest at the centre; the 32 bands as sparks on a circle ---- */
static void vp_vortex(uint8_t *dst, const uint8_t *prev)
{
    warp_t w = {0};
    int32_t dir = (vr_var & 1u) ? -1 : 1;
    uint32_t k;
    w.cx = (int32_t)(VR_CX << 8);
    w.cy = (int32_t)(VR_CY << 8);
    w.zoom = 4096 - 16;
    w.rot = dir * 4;
    w.swirl = dir * (60 + (int32_t)vr_rate() * 8);
    w.rsw = 70;
    warp_step(dst, prev, &w, 0);
    vp_ring_wave(dst, VR_CX, VR_CY, 26 + vr_group(0, 6) * 8 / 1000, vr_amp(10), 170);
    for (k = 0; k < 32u; k++) {
        uint32_t a = k * 32u + vr_t * vr_rate();
        int32_t r = 12 + vr_band[k] * vr_amp(30) / 1000;
        vr_disc(dst, VR_CX + ((r * icos(a)) >> 15), VR_CY + ((r * isin(a)) >> 15) * 3 / 4, vr_band[k] > 600 ? 1 : 0,
                vr_iv(110 + vr_band[k] / 10));
    }
}

/* ---- 18 RIPPLE: rings of zoom running out from the centre; the wave across ---- */
static void vp_ripple(uint8_t *dst, const uint8_t *prev)
{
    warp_t w = {0};
    w.cx = (int32_t)(VR_CX << 8);
    w.cy = (int32_t)(VR_CY << 8);
    w.zoom = 4096 - 10;
    w.rip = 140 + vr_amp(200) + (vr_bang ? 200 : 0);
    w.ripk = 18u + (vr_var & 3u) * 6u;
    w.ripph = vr_t * (6u + vr_rate());
    warp_step(dst, prev, &w, 0);
    vp_line_wave(dst, VR_CY, vr_amp(26), 205);
}

/* ---- 19 KALEIDO: the last frame folded into 4, 6 or 8 mirrored wedges, turning; the stereo image off-centre ---- */
static void vp_kaleido(uint8_t *dst, const uint8_t *prev)
{
    static const uint8_t FOLD[3] = {6, 4, 8};
    warp_t w = {0};
    uint32_t i;
    int32_t px = 0, py = 0, dir = (vr_var & 1u) ? -1 : 1, amp = vr_amp(14);
    w.cx = (int32_t)(VR_CX << 8);
    w.cy = (int32_t)(VR_CY << 8);
    w.zoom = 4096 - 30;
    w.rot = dir * (6 + vr_group(0, 6) / 100);
    w.fold = FOLD[(vr_var >> 1) % 3u];
    warp_step(dst, prev, &w, 0);
    for (i = 0; i < 128u; i++) {                        /* the seed: in the first wedge (0 .. 45 degrees, right and
                                                         * down from the centre), which the folds mirror round */
        int32_t x = VR_CX + 26 + vr_l[i * 4u] * amp / vr_gain, y = VR_CY + 9 - vr_r[i * 4u] * amp / vr_gain;
        if (i)
            vr_line(dst, px, py, x, y, vr_iv(150 + (int32_t)(i & 63u)));
        px = x;
        py = y;
    }
    for (i = 0; i < 8u; i++) {                          /* the bands as sparks along the wedge's middle */
        uint32_t a = 50u, rr = 8u + i * 6u;
        int32_t v = vr_group(i * 4u, i * 4u + 4u);
        if (v > 300)
            vr_disc(dst, VR_CX + (int32_t)((rr * (uint32_t)icos(a)) >> 15), VR_CY + (int32_t)((rr * (uint32_t)isin(a)) >> 15),
                    v > 700 ? 2 : 1, vr_iv(100 + v / 10));
    }
    if (vr_bang)
        vr_tri(dst, VR_CX + 30, VR_CY + 12, 8 + vr_amp(6), vr_t * 16u, vr_iv(VR_IMAX));
}

/* ---- 20 MELT: everything drips down, wobbling; the spectrum along the top feeds it ---- */
static void vp_melt(uint8_t *dst, const uint8_t *prev)
{
    warp_t w = {0};
    uint32_t k;
    w.cx = (int32_t)(VR_CX << 8);
    w.cy = (int32_t)(VR_CY << 8);
    w.zoom = 4096;
    w.ty = -(256 + (int32_t)vr_rate() * 24);
    w.wobx = 256 + vr_amp(300);
    w.wobk = 20;
    w.wobph = (int32_t)(vr_t * 8u);
    warp_step(dst, prev, &w, 0);
    for (k = 0; k < 32u; k++) {
        int32_t h = vr_band[k] * vr_amp(14) / 1000, y;
        for (y = 0; y <= h; y++)
            vr_rect(dst, (int32_t)k * 5, y, 4, 1, vr_iv(VR_IMAX - y * 4));
    }
}

/* ---- 21 SMOKE: everything rises and curls; the wave along the bottom is the fire under it ---- */
static void vp_smoke(uint8_t *dst, const uint8_t *prev)
{
    warp_t w = {0};
    w.cx = (int32_t)(VR_CX << 8);
    w.cy = (int32_t)(VR_CY << 8);
    w.zoom = 4096 + 20;
    w.ty = 200 + (int32_t)vr_rate() * 16;
    w.wobx = 200 + vr_amp(200);
    w.wobk = 14;
    w.wobph = (int32_t)(vr_t * 5u);
    w.woby = 128;
    w.wobyk = 11;
    warp_step(dst, prev, &w, 0);
    vp_line_wave(dst, 80, vr_amp(10), 200);
}

/* ---- 22 WORMHOLE: a hole that wanders, pulling the picture round and in ---- */
static void vp_wormhole(uint8_t *dst, const uint8_t *prev)
{
    warp_t w = {0};
    int32_t bass = vr_group(0, 6), dir = (vr_var & 1u) ? -1 : 1;
    w.cx = (int32_t)(VR_CX << 8) + ((isin(vr_t * 2u) * 30) >> 7);
    w.cy = (int32_t)(VR_CY << 8) + ((icos(vr_t * 3u) * 14) >> 7);
    w.zoom = 4096 - 60 - bass * 80 / 1000;
    w.rot = dir * 10;
    w.swirl = dir * (120 + (int32_t)vr_rate() * 10);
    w.rsw = 60;
    warp_step(dst, prev, &w, 0);
    vp_ring_wave(dst, w.cx >> 8, w.cy >> 8, 9 + bass * 8 / 1000, vr_amp(7), 205);
    {
        uint32_t k;
        for (k = 0; k < 16u; k++) {                     /* the bands as sparks round the mouth, turning */
            uint32_t a = k * 64u + vr_t * 9u;
            int32_t v = vr_band[k * 2u], rr = 14 + v * 12 / 1000;
            vr_px(dst, (w.cx >> 8) + ((rr * icos(a)) >> 15), (w.cy >> 8) + ((rr * isin(a)) >> 15), vr_iv(90 + v / 9));
        }
    }
    if (vr_bang)
        vr_disc(dst, w.cx >> 8, w.cy >> 8, 4, vr_iv(VR_IMAX));
}

/* ---- 23 PLASMA: four waves added, the colours cycling through them (the demo-scene classic) ---- */
static void vp_plasma(uint8_t *dst)
{
    static int32_t pc[VR_W], pr[VR_H];
    uint32_t t = vr_t * (1u + vr_rate() / 2u), x, y, fa = 5u + (uint32_t)vr_group(0, 6) / 125u;
    uint32_t fb = 7u + (uint32_t)vr_group(14, 23) / 125u, fd = 4u + vr_k[0] / 32u, fe = 2u + (vr_var & 3u);
    int32_t mx = VR_CX + ((isin(t * 3u) * 40) >> 15), my = VR_CY + ((icos(t * 2u) * 22) >> 15);
    for (x = 0; x < VR_W; x++)
        pc[x] = isin(x * fa + t * 3u) + (isin(x * 3u + t * 2u) >> 1);
    for (y = 0; y < VR_H; y++)
        pr[y] = isin(y * fb + t * 4u);
    for (y = 0; y < VR_H; y++) {
        uint8_t *row = dst + y * VR_W;
        int32_t dy = (int32_t)y - my, d2 = (0 - mx) * (0 - mx) + dy * dy, ddx = 2 * (0 - mx) + 1;
        for (x = 0; x < VR_W; x++) {
            int32_t v = pc[x] + pr[y] + isin((x + y) * fd + t * 5u) + isin(((uint32_t)d2 >> 3) * fe + t * 7u);
            row[x] = vr_iv(56 + ((v + 147456) * 134 >> 18));   /* (v: -147451 .. 147451) */
            d2 += ddx;
            ddx += 2;
        }
        if ((y & 15u) == 15u)
            vcore_yield();
    }
}

/* ---- 24 FIRE: heat from the bottom, rising and cooling; the bands feed it, the kick flares ---- */
static void vp_fire(uint8_t *dst, const uint8_t *prev)
{
    uint32_t x, y, cool = (vr_t & 1u) ? 1u + (255u - vr_k[2]) / 64u : (255u - vr_k[2]) / 96u;
    int32_t wind = (vr_var & 1u) ? 1 : (vr_var & 2u) ? -1 : 0;
    if (vr_fresh) {
        memset(dst, (int)VR_I0, VF_N);
        return;
    }
    for (y = 0; y + 2u < VR_H; y++) {
        const uint8_t *a = prev + (y + 1u) * VR_W, *b = prev + (y + 2u) * VR_W;
        uint8_t *d = dst + y * VR_W;
        for (x = 0; x < VR_W; x++) {
            int32_t xl = (int32_t)x - 1 + wind, xc = (int32_t)x + wind, xr = (int32_t)x + 1 + wind, s, i;
            xl = xl < 0 ? 0 : xl >= (int32_t)VR_W ? (int32_t)VR_W - 1 : xl;
            xc = xc < 0 ? 0 : xc >= (int32_t)VR_W ? (int32_t)VR_W - 1 : xc;
            xr = xr < 0 ? 0 : xr >= (int32_t)VR_W ? (int32_t)VR_W - 1 : xr;
            s = a[xl] + a[xc] + a[xr] + b[xc];
            i = (s >> 2) - (int32_t)VR_I0 - (int32_t)cool;
            d[x] = vr_iv(i);
        }
        if ((y & 15u) == 15u)
            vcore_yield();
    }
    for (y = VR_H - 2u; y < VR_H; y++)                  /* the coals: the band under each place, sparks */
        for (x = 0; x < VR_W; x++) {
            uint32_t band = (uint32_t)vr_band[x * 32u / VR_W], thr = vr_bang ? 240u : 90u + band * 150u / 1000u;
            dst[y * VR_W + x] = (vr_rnd() & 255u) < thr ? vr_iv(VR_IMAX - (int32_t)(vr_rnd() & 31u)) : (uint8_t)VR_I0;
        }
}

/* ---- 25 RIDGES: the spectrum's last seconds as a mountain range, front to back (Unknown Pleasures) ---- */
#define RG_N 22u
#define RG_S 40u
static uint8_t rg_line[RG_N][RG_S];
static void vp_ridges(uint8_t *dst)
{
    static uint32_t tick;
    uint32_t k, x, s;
    if (++tick >= 17u - vr_rate()) {                    /* a new line every 1..16 frames (RATE) */
        tick = 0;
        for (k = 0; k + 1u < RG_N; k++)                /* (no memmove here: row by row, forwards) */
            for (s = 0; s < RG_S; s++)
                rg_line[k][s] = rg_line[k + 1u][s];
        for (s = 0; s < RG_S; s++) {
            uint32_t b = s * 31u / (RG_S - 1u);
            rg_line[RG_N - 1u][s] = (uint8_t)((uint32_t)vr_band[b] * 255u / 1000u);
        }
    }
    memset(dst, (int)VR_I0, VF_N);
    for (k = 0; k < RG_N; k++) {
        int32_t base = 16 + (int32_t)k * 3, amp = vr_amp(34) * (int32_t)(k + 6u) / (int32_t)(RG_N + 6u), pyt = base;
        int32_t I = 90 + (int32_t)k * 5;
        for (x = 0; x < VR_W; x++) {
            uint32_t sx = x * (RG_S - 1u) * 256u / (VR_W - 1u), i = sx >> 8, f = sx & 255u, v;
            int32_t bell = isin(x * 512u / VR_W), h, yt, y;
            v = i + 1u < RG_S ? (rg_line[k][i] * (256u - f) + rg_line[k][i + 1u] * f) >> 8 : rg_line[k][i];
            h = (int32_t)(((int32_t)v * amp >> 8) * bell >> 15);
            yt = base - h;
            for (y = yt + 1; y <= base + 1; y++)        /* the ground under the line hides the lines behind */
                vr_px(dst, (int32_t)x, y, (uint8_t)VR_I0);
            if (x)
                vr_line(dst, (int32_t)x - 1, pyt, (int32_t)x, yt, vr_iv(I));
            pyt = yt;
        }
        if ((k & 7u) == 7u)
            vcore_yield();
    }
}

/* ---- 26 LAVA: four blobs that swell with their bands and melt into each other ---- */
static uint16_t lv_inv[1024];
static void vp_lava(uint8_t *dst)
{
    static uint8_t ready;
    int32_t bx[4], by[4], m[4], d2y[4], d2x[4], dd[4];
    uint32_t b, x, y, t = vr_t * vr_rate();
    if (!ready) {
        for (x = 0; x < 1024u; x++)
            lv_inv[x] = (uint16_t)(64u * 256u / (x + 4u));
        ready = 1;
    }
    for (b = 0; b < 4u; b++) {
        int32_t R = 9 + vr_group(b * 8u, b * 8u + 8u) * vr_amp(14) / 1000 + (vr_bang ? 3 : 0);
        bx[b] = VR_CX + ((isin(t * (3u + b) + b * 300u) * 60) >> 15);
        by[b] = VR_CY + ((icos(t * (2u + b * 2u) / 2u + b * 500u) * 32) >> 15);
        m[b] = 4096 * 64 / (R * R);
    }
    for (y = 0; y < VR_H; y++) {
        uint8_t *row = dst + y * VR_W;
        for (b = 0; b < 4u; b++) {
            int32_t dy = (int32_t)y - by[b];
            d2y[b] = dy * dy;
            d2x[b] = bx[b] * bx[b];                      /* (0 - bx)^2 */
            dd[b] = 1 - 2 * bx[b];
        }
        for (x = 0; x < VR_W; x++) {
            uint32_t F = 0;
            for (b = 0; b < 4u; b++) {
                uint32_t q = (uint32_t)((d2x[b] + d2y[b]) * m[b]) >> 12;
                F += lv_inv[q > 1023u ? 1023u : q];
                d2x[b] += dd[b];
                dd[b] += 2;
            }
            row[x] = vr_iv(F < 128u ? 0 : F < 256u ? (int32_t)((F - 128u) * 110u >> 7)
                                                   : 150 + (int32_t)((F - 256u) >> 4 > 57u ? 57u : (F - 256u) >> 4));
        }
        if ((y & 15u) == 15u)
            vcore_yield();
    }
}

/* ---- 27 CELLS: eight seeds and the cells round them, bright edges; the kick shakes them ---- */
#define CL_N 8u
static void vp_cells(uint8_t *dst)
{
    static int32_t jx[CL_N], jy[CL_N];
    int32_t sx[CL_N], sy[CL_N], ci[CL_N];
    uint32_t k, gx, gy, t = vr_t * vr_rate();
    for (k = 0; k < CL_N; k++) {
        if (vr_bang) {
            jx[k] = (int32_t)(vr_rnd() % 13u) - 6;
            jy[k] = (int32_t)(vr_rnd() % 9u) - 4;
        } else {
            jx[k] = jx[k] * 7 / 8;
            jy[k] = jy[k] * 7 / 8;
        }
        sx[k] = VR_CX + ((isin(t * (2u + k % 3u) + k * 128u) * 70) >> 15) + jx[k];
        sy[k] = VR_CY + ((icos(t * (1u + k % 4u) + k * 200u) * 38) >> 15) + jy[k];
        ci[k] = 30 + vr_group(k * 4u, k * 4u + 4u) * 120 / 1000;
    }
    for (gy = 0; gy * 8u < VR_H; gy++) {
        for (gx = 0; gx < VR_W / 16u; gx++) {
            int32_t cx = (int32_t)(gx * 16u + 8u), cy = (int32_t)(gy * 8u + 4u), ew;
            uint32_t c0 = 0, c1 = 1, c2 = 2, r, c;
            int32_t e0 = 0x7FFFFFFF, e1 = 0x7FFFFFFF, e2 = 0x7FFFFFFF;
            for (k = 0; k < CL_N; k++) {                /* the three seeds nearest this block's centre */
                int32_t e = (sx[k] - cx) * (sx[k] - cx) + (sy[k] - cy) * (sy[k] - cy);
                if (e < e0) { e2 = e1; c2 = c1; e1 = e0; c1 = c0; e0 = e; c0 = k; }
                else if (e < e1) { e2 = e1; c2 = c1; e1 = e; c1 = k; }
                else if (e < e2) { e2 = e; c2 = k; }
            }
            {   /* the edge's width: d2 - d1 grows as 2 x (the seeds' distance) x (the distance from the edge), so a
                 * constant width wants the threshold in proportion to the two nearest seeds' distance */
                int32_t ex = sx[c0] - sx[c1], ey = sy[c0] - sy[c1];
                ew = 3 * (int32_t)vr_isqrt((uint32_t)(ex * ex + ey * ey)) + 4;
            }
            for (r = 0; r < 8u && gy * 8u + r < VR_H; r++) {
                int32_t y = (int32_t)(gy * 8u + r);
                uint8_t *row = dst + (uint32_t)y * VR_W + gx * 16u;
                for (c = 0; c < 16u; c++) {
                    int32_t x = (int32_t)(gx * 16u + c), d0, d1, d2, m1, m2;
                    uint32_t id;
                    d0 = (sx[c0] - x) * (sx[c0] - x) + (sy[c0] - y) * (sy[c0] - y);
                    d1 = (sx[c1] - x) * (sx[c1] - x) + (sy[c1] - y) * (sy[c1] - y);
                    d2 = (sx[c2] - x) * (sx[c2] - x) + (sy[c2] - y) * (sy[c2] - y);
                    if (d0 <= d1 && d0 <= d2) { id = c0; m1 = d0; m2 = d1 < d2 ? d1 : d2; }
                    else if (d1 <= d2) { id = c1; m1 = d1; m2 = d0 < d2 ? d0 : d2; }
                    else { id = c2; m1 = d2; m2 = d0 < d1 ? d0 : d1; }
                    row[c] = m2 - m1 < ew ? vr_iv(VR_IMAX - (m2 - m1) * 60 / ew)
                                          : vr_iv(ci[id] * (1024 - (m1 > 2047 ? 1023 : m1 >> 1)) >> 10);
                }
            }
        }
        vcore_yield();
    }
}

/* ---- 28 WIREFRAME: a cube, an octahedron, an icosahedron or a tesseract turning, its trails behind it ---- */
#define WF_V 16u
#define WF_E 32u
static int16_t wf_v[WF_V][4];
static uint8_t wf_e[WF_E][2];
static uint32_t wf_nv, wf_ne, wf_obj = 0xFFu;
static void wf_build(uint32_t obj)
{
    uint32_t i, j, c;
    int32_t want, tol;
    wf_nv = 0;
    if (obj == 0u) {                                    /* the cube */
        for (i = 0; i < 8u; i++) {
            wf_v[i][0] = (i & 1u) ? 60 : -60; wf_v[i][1] = (i & 2u) ? 60 : -60; wf_v[i][2] = (i & 4u) ? 60 : -60;
            wf_v[i][3] = 0;
        }
        wf_nv = 8; want = 120 * 120; tol = 100;
    } else if (obj == 1u) {                             /* the octahedron */
        for (i = 0; i < 6u; i++) {
            wf_v[i][0] = wf_v[i][1] = wf_v[i][2] = wf_v[i][3] = 0;
            wf_v[i][i >> 1] = (i & 1u) ? 90 : -90;
        }
        wf_nv = 6; want = 2 * 90 * 90; tol = 100;
    } else if (obj == 2u) {                             /* the icosahedron: (0, +-1, +-phi) and its turns */
        static const int8_t P[3][2] = {{0, 1}, {1, 2}, {2, 0}};
        for (i = 0; i < 12u; i++) {
            uint32_t g = i >> 2;
            wf_v[i][0] = wf_v[i][1] = wf_v[i][2] = wf_v[i][3] = 0;
            wf_v[i][P[g][0]] = (i & 1u) ? 40 : -40;
            wf_v[i][P[g][1]] = (i & 2u) ? 65 : -65;
        }
        wf_nv = 12; want = 80 * 80; tol = 700;
    } else {                                            /* the tesseract */
        for (i = 0; i < 16u; i++) {
            wf_v[i][0] = (i & 1u) ? 50 : -50; wf_v[i][1] = (i & 2u) ? 50 : -50;
            wf_v[i][2] = (i & 4u) ? 50 : -50; wf_v[i][3] = (i & 8u) ? 50 : -50;
        }
        wf_nv = 16; want = 100 * 100; tol = 100;
    }
    wf_ne = 0;
    for (i = 0; i < wf_nv; i++)
        for (j = i + 1u; j < wf_nv && wf_ne < WF_E; j++) {
            int32_t d2 = 0, e;
            for (c = 0; c < 4u; c++) {
                e = wf_v[i][c] - wf_v[j][c];
                d2 += e * e;
            }
            if (d2 > want - tol && d2 < want + tol) {
                wf_e[wf_ne][0] = (uint8_t)i;
                wf_e[wf_ne][1] = (uint8_t)j;
                wf_ne++;
            }
        }
    wf_obj = obj;
}

static void vp_wireframe(uint8_t *dst, const uint8_t *prev)
{
    static int32_t sxs[WF_V], sys[WF_V], szs[WF_V];
    warp_t w = {0};
    uint32_t i, obj = vr_var % 4u, t = vr_t * vr_rate();
    int32_t scale = 256 + vr_group(0, 6) * 128 / 1000 + (vr_bang ? 60 : 0) + vr_amp(60) - 40;
    int32_t ca = icos(t * 2u), sa = isin(t * 2u), cb = icos(t * 3u / 2u), sb = isin(t * 3u / 2u);
    int32_t cw = icos(t), sw = isin(t);
    if (obj != wf_obj)
        wf_build(obj);
    w.cx = (int32_t)(VR_CX << 8);
    w.cy = (int32_t)(VR_CY << 8);
    w.zoom = 4096 - 20;
    w.rot = 6;
    warp_step(dst, prev, &w, 0);
    for (i = 0; i < wf_nv; i++) {
        int32_t x = wf_v[i][0], y = wf_v[i][1], z = wf_v[i][2], q = wf_v[i][3], x1, y1, z1;
        if (obj == 3u) {                                /* the fourth dimension: x-w and z-w turns, then w as depth */
            int32_t xw = (x * cw - q * sw) >> 15, qw = (x * sw + q * cw) >> 15, zw = (z * cw - qw * sw) >> 15;
            int32_t ww = (z * sw + qw * cw) >> 15;
            x = xw * 160 / (160 - ww);
            y = y * 160 / (160 - ww);
            z = zw * 160 / (160 - ww);
        }
        x1 = (x * ca - z * sa) >> 15;                   /* about y, then about x */
        z1 = (x * sa + z * ca) >> 15;
        y1 = (y * cb - z1 * sb) >> 15;
        z1 = (y * sb + z1 * cb) >> 15;
        szs[i] = z1 + 260;
        sxs[i] = VR_CX + x1 * scale * 100 / (szs[i] * 256);
        sys[i] = VR_CY + y1 * scale * 100 / (szs[i] * 256);
    }
    for (i = 0; i < wf_ne; i++) {
        uint32_t a = wf_e[i][0], b = wf_e[i][1];
        int32_t I = 230 - (szs[a] + szs[b]) / 6;
        vr_line(dst, sxs[a], sys[a], sxs[b], sys[b], vr_iv(I));
    }
}

/* ---- 29 FRESNEL: two zone plates wandering through each other, the colours running round the rings ---- */
static void vp_fresnel(uint8_t *dst)
{
    uint32_t x, y, t = vr_t * vr_rate();
    int32_t ax = VR_CX + ((isin(t * 2u) * 50) >> 15), ay = VR_CY + ((icos(t * 3u) * 25) >> 15);
    int32_t bx = VR_CX + ((icos(t * 2u + 300u) * 50) >> 15), by = VR_CY + ((isin(t * 5u / 2u) * 25) >> 15);
    int32_t k1 = 6 + vr_amp(10) + vr_group(0, 6) / 200, k2 = 5 + vr_amp(8) + vr_group(14, 23) / 200;
    for (y = 0; y < VR_H; y++) {
        uint8_t *row = dst + y * VR_W;
        int32_t da = ax * ax + ((int32_t)y - ay) * ((int32_t)y - ay), dda = 1 - 2 * ax;
        int32_t db = bx * bx + ((int32_t)y - by) * ((int32_t)y - by), ddb = 1 - 2 * bx;
        for (x = 0; x < VR_W; x++) {
            uint32_t v = ((uint32_t)(da * k1 + db * k2) >> 7) + t * 3u;
            row[x] = vr_iv(56 + (int32_t)((v & 255u) * 151u >> 8));
            da += dda; dda += 2;
            db += ddb; ddb += 2;
        }
        if ((y & 15u) == 15u)
            vcore_yield();
    }
}

/* ---- the overlays (the webcam only): saved under, drawn, encoded, put back (vcore.c) ---- */
#define OV_POOL 5120u
#define OV_RECTS 8u
#ifdef VF_SPARE                                         /* after the analysis in the frames' RAM (vcore.c VF_SPARE): used
                                                         * within one visual-mode frame, never while SCREEN is drawn */
#define ov_pool (VF_SPARE + VF_W * VIS_ROWS + 4u * VR_N * 2u)
_Static_assert(VF_W * VIS_ROWS + 4u * VR_N * 2u + OV_POOL <= VF_SPARE_N, "the overlay's pool fits after the analysis");
#else
static uint8_t ov_pool[OV_POOL];
#endif
static struct { int16_t x, y, w, h; uint16_t at; } ov_rect[OV_RECTS];
static uint32_t ov_n, ov_used;
static uint32_t vr_name_t, vr_palname_t;                /* frames left to show the mode's / palette's name */

static int ov_save(const uint8_t *f, int32_t x, int32_t y, int32_t w, int32_t h)   /* 1: saved, draw there */
{
    int32_t r;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int32_t)VR_W) w = (int32_t)VR_W - x;
    if (y + h > (int32_t)VR_H) h = (int32_t)VR_H - y;
    if (w <= 0 || h <= 0 || ov_n >= OV_RECTS || ov_used + (uint32_t)(w * h) > OV_POOL)
        return 0;
    ov_rect[ov_n].x = (int16_t)x; ov_rect[ov_n].y = (int16_t)y;
    ov_rect[ov_n].w = (int16_t)w; ov_rect[ov_n].h = (int16_t)h;
    ov_rect[ov_n].at = (uint16_t)ov_used;
    for (r = 0; r < h; r++)
        memcpy(ov_pool + ov_used + (uint32_t)(r * w), f + (uint32_t)(y + r) * VR_W + (uint32_t)x, (uint32_t)w);
    ov_used += (uint32_t)(w * h);
    ov_n++;
    return 1;
}

static void vr_overlay_restore(uint8_t *f)
{
    uint32_t i;
    int32_t r;
    for (i = ov_n; i-- > 0;)
        for (r = 0; r < ov_rect[i].h; r++)
            memcpy(f + (uint32_t)(ov_rect[i].y + r) * VR_W + (uint32_t)ov_rect[i].x,
                   ov_pool + ov_rect[i].at + (uint32_t)(r * ov_rect[i].w), (uint32_t)ov_rect[i].w);
    ov_n = 0;
    ov_used = 0;
}

static uint32_t vr_slen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }

/* 5 x 7 text on a black box (6 pixels a character), colour c */
static void vr_text5(uint8_t *f, int32_t x, int32_t y, const char *s, uint8_t c)
{
    uint32_t n = vr_slen(s), i, col, bit;
    int32_t w = (int32_t)n * 6 + 1;
    if (!ov_save(f, x - 1, y - 1, w, 9))
        return;
    vr_rect(f, x - 1, y - 1, w, 9, 0);
    for (i = 0; i < n; i++) {
        uint32_t ch = (uint8_t)s[i];
        if (ch >= 'a' && ch <= 'z')
            ch -= 32u;
        if (ch < VFONT5_FIRST || ch > VFONT5_LAST)
            ch = '?';
        for (col = 0; col < 5u; col++)
            for (bit = 0; bit < 7u; bit++)
                if ((VFONT5[ch - VFONT5_FIRST][col] >> bit) & 1u)
                    vr_px(f, x + (int32_t)(i * 6u + col), y + (int32_t)bit, c);
    }
}

/* the title: the large font (FONT_S, 8 x 16), each letter its own turning hue, outlined in black */
static void vr_title(uint8_t *f, const char *s, int32_t y)
{
    uint32_t n = 0, i, pass, gx, gy;
    int32_t w = 0, x0;
    char t[VIS_TITLE_N];
    for (i = 0; i + 1u < VIS_TITLE_N && s[i]; i++)
        t[i] = s[i];
    t[i] = 0;
    n = i;
    if (!n)
        return;
    for (i = 0; i < n; i++)
        w += 8;
    x0 = ((int32_t)VR_W - w) / 2;
    if (!ov_save(f, x0 - 3, y - 2, w + 6, (int32_t)FONT_S.h + 4))
        return;
    for (pass = 0; pass < 2u; pass++) {
        int32_t x = x0;
        for (i = 0; i < n; i++) {
            uint32_t ch = (uint8_t)t[i], gi, bw;
            const uint8_t *gd;
            if (ch >= 'a' && ch <= 'z')
                ch -= 32u;
            if (ch < FONT_S.first || ch > FONT_S.last)
                ch = '?';
            gi = ch - FONT_S.first;
            bw = FONT_S.bw[gi];
            gd = FONT_S.data + FONT_S.off[gi];
            for (gy = 0; gy < FONT_S.h; gy++)
                for (gx = 0; gx < bw; gx++) {
                    uint32_t b = gy * bw + gx;
                    if ((gd[b >> 3] >> (7u - (b & 7u))) & 1u) {
                        int32_t px = x - (int32_t)FONT_S.pad + (int32_t)gx, py = y + (int32_t)gy;
                        if (pass == 0u) {
                            vr_px(f, px - 1, py, 0); vr_px(f, px + 1, py, 0);
                            vr_px(f, px, py - 1, 0); vr_px(f, px, py + 1, 0);
                        } else
                            vr_px(f, px, py, (uint8_t)(VR_OV_TITLE + (i & 15u)));
                    }
                }
            x += 8;
        }
    }
}

/* the overlays for this frame, by vis_set.hud: 1 INFO (tempo, bar, the mode's and palette's names a moment,
 * LIVE), 2 the title card, 3 both. (SCREEN has none: vcore.c encodes the display's copy as it is.) */
static void vr_overlay(uint8_t *f)
{
    uint32_t hud = vc0.hud & 3u;
    ov_n = 0;
    ov_used = 0;
    if (!hud)
        return;
    if (hud & 1u) {
        char b[16];
        uint32_t bpm = vc0.bpm, k = 0, beat = vc0.beat_q8 >> 8;
        if (bpm >= 100u)
            b[k++] = (char)('0' + bpm / 100u);
        if (bpm >= 10u)
            b[k++] = (char)('0' + bpm / 10u % 10u);
        b[k++] = (char)('0' + bpm % 10u);
        b[k++] = ' '; b[k++] = 'B'; b[k++] = 'P'; b[k++] = 'M';
        if (vc0.playing) {
            uint32_t bar = beat / 4u + 1u;
            b[k++] = ' '; b[k++] = ' ';
            if (bar >= 100u)
                b[k++] = (char)('0' + bar / 100u % 10u);
            if (bar >= 10u)
                b[k++] = (char)('0' + bar / 10u % 10u);
            b[k++] = (char)('0' + bar % 10u);
            b[k++] = '.';
            b[k++] = (char)('1' + beat % 4u);
        }
        b[k] = 0;
        vr_text5(f, 2, 2, b, VR_OV_WHITE);
        if (vr_name_t) {
            const char *m = VIS_NAME[vis_style % VIS_N];
            vr_text5(f, (int32_t)VR_W - 1 - (int32_t)vr_slen(m) * 6, 2, m, VR_OV_WHITE);
        }
        if (vr_palname_t)
            vr_text5(f, 2, (int32_t)VR_H - 9, VIS_PAL_NAME[vr_pal_cur % VIS_NPAL], VR_OV_AMBER);
        if (ov_save(f, (int32_t)VR_W - 34, (int32_t)VR_H - 10, 34, 10)) {   /* LIVE, the dot blinking on the beat */
            vr_rect(f, (int32_t)VR_W - 34, (int32_t)VR_H - 10, 34, 10, 0);
            if ((vc0.beat_q8 & 255u) < 128u)
                vr_rect(f, (int32_t)VR_W - 31, (int32_t)VR_H - 7, 4, 4, (uint8_t)VR_OV_RED);
        }
        vr_text5(f, (int32_t)VR_W - 25, (int32_t)VR_H - 8, "LIVE", VR_OV_RED);
    }
    if (hud & 2u)
        vr_title(f, vis_set.title, 37);
}
