/* SPDX-License-Identifier: GPL-3.0-only */
/* SCREEN (vis_set.h VIS_SCREEN): the webcam shows the FM-1's display itself, for streaming a set or a tutorial.
 * While it is on (mir_px set, vcore.c vcore_poll, once the video core asks for it), every LCD write (lcd.c
 * lcd_fill, lcd_blit) also lands, at 3/4 size, in a 180 x 180 copy of one byte a pixel; the video core encodes
 * that copy centred in the 320 x 180 picture (vcore.c, vjpeg.c's placement).
 *
 * 3/4: of every 4 columns two keep a column of their own and two share one, and the same for rows. Where pixels
 * share, the stronger one wins: the one farther from the background's brightness (on the usual black background,
 * the brighter), so 1-pixel lines and text strokes stay. A write that splits such a pair (a rectangle starting or
 * ending between the two) sets the shared pixel from its own half, so the pairs are where the interface's
 * rectangles never split them: columns 4k and 4k+1 (no rectangle edge at x = 4k+1 in the whole UI test) and rows
 * 4k+2 and 4k+3 (9 of ~80000 edges at y = 4k+3; tests/ui_pages_test.c measures what is left).
 *
 * The byte: a 256-colour palette (tools/gen_mirror.py: a 6 x 6 x 6 cube and 40 greys), from RGB565 through a
 * 4096-entry table (RGB 4-4-4, black and white exact), in flash. */
#include "vmirror.h"                                    /* MIR_IDX, MIR_RGB, MIR_LUM (build/gen) */
#define MIR_W 180u
#define MIR_N (MIR_W * MIR_W)
#define MIR_OX 72u                                      /* its place in the 320 x 180 picture: 8-aligned, so the
                                                         * black either side is whole blocks (68 px on the right) */
/* (mir_px, the copy while SCREEN is on: declared before lcd.c in felucca.c) */

static uint8_t mir_st[256];                             /* each palette entry's distance from the background's brightness */
static uint32_t mir_st_bg = 0xFFFFFFFFu;

static inline uint32_t mir_m4(uint32_t t) { return (t >> 2) * 3u + (((t & 3u) + 1u) >> 1); }   /* 0 1 1 2 a group */
static inline uint32_t mir_mx(uint32_t x) { return mir_m4(x + 1u) - 1u; }   /* columns 4k, 4k+1 share */
static inline uint32_t mir_my(uint32_t y) { return mir_m4(y + 3u) - 2u; }   /* rows 4k+2, 4k+3 share */
static inline uint32_t mir_idx(uint32_t c) { return MIR_IDX[((c >> 12) << 8) | (((c >> 7) & 15u) << 4) | ((c >> 1) & 15u)]; }

static void mir_strength(void)                          /* for the theme's background (gfx.c pal_bg; 0 black) */
{
    uint32_t i, l;
    if (pal_bg == mir_st_bg)
        return;
    mir_st_bg = pal_bg;
    l = MIR_LUM[mir_idx(pal_bg)];
    for (i = 0; i < 256u; i++)
        mir_st[i] = (uint8_t)(MIR_LUM[i] > l ? MIR_LUM[i] - l : l - MIR_LUM[i]);
}

static void mir_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c)   /* c: RGB565 */
{
    uint8_t *p = mir_px;
    uint32_t i, j, x0, x1, y1, v;
    if (!p || !w || !h || x >= 240u || y >= 240u)
        return;
    if (x + w > 240u)
        w = 240u - x;
    if (y + h > 240u)
        h = 240u - y;
    x0 = mir_mx(x);
    x1 = mir_mx(x + w - 1u);
    y1 = mir_my(y + h - 1u);
    v = mir_idx(c);
    for (j = mir_my(y); j <= y1; j++)
        for (i = x0; i <= x1; i++)
            p[j * MIR_W + i] = (uint8_t)v;
}

static void mir_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)   /* px: as the LCD
                                                         * takes it (RGB565, bytes swapped), w a row */
{
    static uint8_t st[MIR_W];                           /* the strength of what this write put in the row */
    uint8_t *p = mir_px;
    uint32_t sx, sy, cw = w, ch = h, dyc = 0xFFFFFFFFu;
    if (!p || !w || !h || x >= 240u || y >= 240u)
        return;
    if (x + cw > 240u)
        cw = 240u - x;
    if (y + ch > 240u)
        ch = 240u - y;
    mir_strength();
    for (sy = 0; sy < ch; sy++) {
        uint32_t dy = mir_my(y + sy), dxc = 0xFFFFFFFFu, fresh = dy != dyc;   /* fresh: this write's first row there */
        const uint16_t *s = px + sy * w;
        uint8_t *row = p + dy * MIR_W;
        dyc = dy;
        for (sx = 0; sx < cw; sx++) {
            uint32_t v = s[sx], dx = mir_mx(x + sx), k = mir_idx(((v >> 8) | (v << 8)) & 0xFFFFu), t = mir_st[k];
            if ((fresh && dx != dxc) || t > st[dx]) {
                row[dx] = (uint8_t)k;
                st[dx] = (uint8_t)t;
            }
            dxc = dx;
        }
    }
}
