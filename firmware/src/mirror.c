/* SPDX-License-Identifier: GPL-3.0-only */
/* SCREEN (vis_set.h VIS_SCREEN): the webcam shows the FM-1's display itself, for streaming a set or a tutorial.
 * While it is on (mir_px set, vcore.c vcore_poll, once the video core asks for it), every LCD write (lcd.c
 * lcd_fill, lcd_blit) also lands in a 240 x 240 copy of one byte a pixel, pixel for pixel; the video core encodes
 * that copy centred in the 320 x 240 picture (vcore.c, vjpeg.c's placement). (0.1: a 320 x 180 picture and a 3/4
 * copy, two of every four columns and rows sharing a pixel: thin strokes merged, an M read as an N.)
 *
 * The byte: a 256-colour palette (tools/gen_mirror.py: a 6 x 6 x 6 cube and 40 greys), from RGB565 through a
 * 4096-entry table (RGB 4-4-4, black and white exact), in flash. */
#include "vmirror.h"                                    /* MIR_IDX, MIR_RGB (build/gen) */
#define MIR_W 240u
#define MIR_N (MIR_W * MIR_W)
#define MIR_OX 40u                                      /* its place in the 320 x 240 picture: 8-aligned, so the
                                                         * black either side is whole blocks */
/* (mir_px, the copy while SCREEN is on: declared before lcd.c in felucca.c) */

static inline uint32_t mir_idx(uint32_t c) { return MIR_IDX[((c >> 12) << 8) | (((c >> 7) & 15u) << 4) | ((c >> 1) & 15u)]; }

static void mir_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c)   /* c: RGB565 */
{
    uint8_t *p = mir_px;
    uint32_t j, v;
    if (!p || !w || !h || x >= MIR_W || y >= MIR_W)
        return;
    if (x + w > MIR_W)
        w = MIR_W - x;
    if (y + h > MIR_W)
        h = MIR_W - y;
    v = mir_idx(c);
    for (j = 0; j < h; j++)
        memset(p + (y + j) * MIR_W + x, (int)v, w);
}

static void mir_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)   /* px: as the LCD
                                                         * takes it (RGB565, bytes swapped), w a row */
{
    uint8_t *p = mir_px;
    uint32_t sx, sy, cw = w, ch = h;
    if (!p || !w || !h || x >= MIR_W || y >= MIR_W)
        return;
    if (x + cw > MIR_W)
        cw = MIR_W - x;
    if (y + ch > MIR_W)
        ch = MIR_W - y;
    for (sy = 0; sy < ch; sy++) {
        const uint16_t *s = px + sy * w;
        uint8_t *row = p + (y + sy) * MIR_W + x;
        for (sx = 0; sx < cw; sx++) {
            uint32_t v = s[sx];
            row[sx] = (uint8_t)mir_idx(((v >> 8) | (v << 8)) & 0xFFFFu);
        }
    }
}
