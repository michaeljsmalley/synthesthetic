/* SPDX-License-Identifier: GPL-3.0-only */
/* vjpeg: a baseline JPEG encoder for the FM-1's video core, integers only (no float, no 64-bit division,
 * as the chip's rules in docs/upstream-dinghy/RULES.md). Input: an 8-bit indexed frame (one byte a pixel) and
 * a 256-entry palette already converted to Y Cb Cr (vjpeg_lut_from_rgb), or a 16-bit RGB565 frame; output: a
 * JFIF file in RAM for the UVC stream, 4:2:0, the standard Huffman tables, quality 1..100 (IJG scaling).
 *
 *   int32_t vjpeg_encode(const vjpeg_src_t *src, uint32_t quality, uint8_t *out, uint32_t cap);
 *       -> bytes written, or -1 when the file would not fit in cap (out is then incomplete; the caller
 *          lowers the quality for the next frame or drops this one)
 *   src->zoom = 1 or 2: a 120 x 120 frame encodes as 240 x 240 with each source pixel 2 x 2 (no RAM copy).
 *   src->ow (zoom 1, a palette): the output is ow x oh with the frame at ox, oy and black around it (SCREEN:
 *   the display's 180 x 180 copy in the 320 x 180 picture); 0: the output is the frame.
 *
 * The DCT is the IJG accurate integer one (jfdctint: Loeffler, Ligtenberg and Moschytz, 13-bit constants); the
 * coefficients it gives are 8 x the DCT, so the quantiser divides by 8 q through a 16-bit reciprocal. A 16 x 16
 * MCU is gathered straight from the source (no row copies): four Y blocks, one Cb and one Cr block from the
 * 2 x 2 averages. Pixels past the right or bottom edge repeat the last column or row (T.81 A.2.4). A flat block
 * (black around a placed frame, the interface's background) skips the DCT: its DC alone, the same bits.
 *
 * RAM: the quantisation tables and their reciprocals (384 B) in a vjpeg_t the caller keeps, the block
 * workspace on the stack (~640 B). The Huffman tables are const (flash, XIP): firmware/src/vjpeg_tables.h. */
#include <stdint.h>
#include "vjpeg_tables.h"

typedef struct { uint8_t y, cb, cr, pad; } vjpeg_ycc_t;

typedef struct {
    const uint8_t *px;           /* the frame: 8-bit indices (lut set) or RGB565 (lut 0, pitch in pixels) */
    const vjpeg_ycc_t *lut;      /* 256 entries, from vjpeg_lut_from_rgb; 0 = px is RGB565 (little-endian) */
    uint32_t w, h, pitch;        /* source size and row pitch in pixels */
    uint32_t zoom;               /* 1 or 2: the output is w*zoom x h*zoom */
    void (*yield)(void);         /* called once per MCU row, or 0 (the video core parks for flash writes there) */
    uint32_t ow, oh, ox, oy;     /* zoom 1 with a palette: the output's size and the frame's place in it, black
                                  * around; ow 0: the output is the frame */
} vjpeg_src_t;

typedef struct {
    uint8_t *out;
    uint32_t cap, pos;
    uint32_t bitbuf, nbits;
    int32_t dc[3];
    uint8_t overflow;
    uint8_t qt[2][64];           /* quantisation tables, zigzag order (as written in DQT) */
    uint16_t recip[2][64];       /* 65536 / (8 q), natural order, for the quantiser */
} vjpeg_t;

#define VJ_CONST_BITS 13
#define VJ_PASS1_BITS 2
#define VJ_F_0_298 2446   /* FIX(0.298631336) */
#define VJ_F_0_390 3196   /* FIX(0.390180644) */
#define VJ_F_0_541 4433   /* FIX(0.541196100) */
#define VJ_F_0_765 6270   /* FIX(0.765366865) */
#define VJ_F_0_899 7373   /* FIX(0.899976223) */
#define VJ_F_1_175 9633   /* FIX(1.175875602) */
#define VJ_F_1_501 12299  /* FIX(1.501321110) */
#define VJ_F_1_847 15137  /* FIX(1.847759065) */
#define VJ_F_1_961 16069  /* FIX(1.961570560) */
#define VJ_F_2_053 16819  /* FIX(2.053119869) */
#define VJ_F_2_562 20995  /* FIX(2.562915447) */
#define VJ_F_3_072 25172  /* FIX(3.072711026) */
#define VJ_DESCALE(x, n) (((x) + (1 << ((n) - 1))) >> (n))

/* the palette: JFIF (BT.601 full range) Y Cb Cr from 8-bit RGB, integers */
static void vjpeg_lut_from_rgb(const uint8_t (*rgb)[3], vjpeg_ycc_t *lut, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t r = rgb[i][0], g = rgb[i][1], b = rgb[i][2];
        lut[i].y = (uint8_t)((77 * r + 150 * g + 29 * b + 128) >> 8);
        lut[i].cb = (uint8_t)(128 + ((-43 * r - 85 * g + 128 * b + 128) >> 8));
        lut[i].cr = (uint8_t)(128 + ((128 * r - 107 * g - 21 * b + 128) >> 8));
        lut[i].pad = 0;
    }
}

static void vjpeg_ycc_565(uint32_t c, int32_t *y, int32_t *cb, int32_t *cr)
{
    int32_t r = ((int32_t)((c >> 11) & 31u) * 527 + 23) >> 6, g = ((int32_t)((c >> 5) & 63u) * 259 + 33) >> 6,
            b = ((int32_t)(c & 31u) * 527 + 23) >> 6;
    *y = (77 * r + 150 * g + 29 * b + 128) >> 8;
    *cb = 128 + ((-43 * r - 85 * g + 128 * b + 128) >> 8);
    *cr = 128 + ((128 * r - 107 * g - 21 * b + 128) >> 8);
}

/* ---- the output ---- */
#define VJ_INLINE static inline __attribute__((always_inline))   /* (the firmware builds -Os: these are the hot path,
                                                         * two a coefficient; a call each cost most of the encode) */
VJ_INLINE void vj_byte(vjpeg_t *j, uint32_t b)
{
    if (j->pos < j->cap)
        j->out[j->pos] = (uint8_t)b;
    else
        j->overflow = 1;
    j->pos++;
}
static void vj_u16(vjpeg_t *j, uint32_t v) { vj_byte(j, v >> 8); vj_byte(j, v & 0xFFu); }

VJ_INLINE void vj_bits(vjpeg_t *j, uint32_t code, uint32_t len)   /* len 1..16 */
{
    uint32_t nb = j->nbits + len, buf = (j->bitbuf << len) | (code & ((1u << len) - 1u));
    while (nb >= 8u) {
        uint32_t b;
        nb -= 8u;
        b = (buf >> nb) & 0xFFu;
        if (j->pos + 2u <= j->cap) {                    /* room for the byte and its stuffing: no checks */
            j->out[j->pos++] = (uint8_t)b;
            if (b == 0xFFu)
                j->out[j->pos++] = 0;                   /* byte stuffing */
        } else {
            vj_byte(j, b);
            if (b == 0xFFu)
                vj_byte(j, 0);
        }
    }
    j->bitbuf = buf & ((1u << nb) - 1u);
    j->nbits = nb;
}
static void vj_flush(vjpeg_t *j)
{
    if (j->nbits)
        vj_bits(j, 0x7Fu, 8u - j->nbits);                   /* pad with ones */
}

/* ---- the DCT (IJG jfdctint), in place on 64 int32 in natural order; output = 8 x DCT ---- */
static void vj_fdct(int32_t *d)
{
    int32_t tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp10, tmp11, tmp12, tmp13, z1, z2, z3, z4, z5;
    int32_t *p;
    uint32_t i;
    for (i = 0, p = d; i < 8u; i++, p += 8) {               /* rows */
        tmp0 = p[0] + p[7]; tmp7 = p[0] - p[7];
        tmp1 = p[1] + p[6]; tmp6 = p[1] - p[6];
        tmp2 = p[2] + p[5]; tmp5 = p[2] - p[5];
        tmp3 = p[3] + p[4]; tmp4 = p[3] - p[4];
        tmp10 = tmp0 + tmp3; tmp13 = tmp0 - tmp3;
        tmp11 = tmp1 + tmp2; tmp12 = tmp1 - tmp2;
        p[0] = (tmp10 + tmp11) * (1 << VJ_PASS1_BITS);      /* (a multiply: a left shift of a negative value is UB) */
        p[4] = (tmp10 - tmp11) * (1 << VJ_PASS1_BITS);
        z1 = (tmp12 + tmp13) * VJ_F_0_541;
        p[2] = VJ_DESCALE(z1 + tmp13 * VJ_F_0_765, VJ_CONST_BITS - VJ_PASS1_BITS);
        p[6] = VJ_DESCALE(z1 - tmp12 * VJ_F_1_847, VJ_CONST_BITS - VJ_PASS1_BITS);
        z1 = tmp4 + tmp7; z2 = tmp5 + tmp6; z3 = tmp4 + tmp6; z4 = tmp5 + tmp7;
        z5 = (z3 + z4) * VJ_F_1_175;
        tmp4 *= VJ_F_0_298; tmp5 *= VJ_F_2_053; tmp6 *= VJ_F_3_072; tmp7 *= VJ_F_1_501;
        z1 *= -VJ_F_0_899; z2 *= -VJ_F_2_562; z3 *= -VJ_F_1_961; z4 *= -VJ_F_0_390;
        z3 += z5; z4 += z5;
        p[7] = VJ_DESCALE(tmp4 + z1 + z3, VJ_CONST_BITS - VJ_PASS1_BITS);
        p[5] = VJ_DESCALE(tmp5 + z2 + z4, VJ_CONST_BITS - VJ_PASS1_BITS);
        p[3] = VJ_DESCALE(tmp6 + z2 + z3, VJ_CONST_BITS - VJ_PASS1_BITS);
        p[1] = VJ_DESCALE(tmp7 + z1 + z4, VJ_CONST_BITS - VJ_PASS1_BITS);
    }
    for (i = 0, p = d; i < 8u; i++, p++) {                  /* columns */
        tmp0 = p[0] + p[56]; tmp7 = p[0] - p[56];
        tmp1 = p[8] + p[48]; tmp6 = p[8] - p[48];
        tmp2 = p[16] + p[40]; tmp5 = p[16] - p[40];
        tmp3 = p[24] + p[32]; tmp4 = p[24] - p[32];
        tmp10 = tmp0 + tmp3; tmp13 = tmp0 - tmp3;
        tmp11 = tmp1 + tmp2; tmp12 = tmp1 - tmp2;
        p[0] = VJ_DESCALE(tmp10 + tmp11, VJ_PASS1_BITS);
        p[32] = VJ_DESCALE(tmp10 - tmp11, VJ_PASS1_BITS);
        z1 = (tmp12 + tmp13) * VJ_F_0_541;
        p[16] = VJ_DESCALE(z1 + tmp13 * VJ_F_0_765, VJ_CONST_BITS + VJ_PASS1_BITS);
        p[48] = VJ_DESCALE(z1 - tmp12 * VJ_F_1_847, VJ_CONST_BITS + VJ_PASS1_BITS);
        z1 = tmp4 + tmp7; z2 = tmp5 + tmp6; z3 = tmp4 + tmp6; z4 = tmp5 + tmp7;
        z5 = (z3 + z4) * VJ_F_1_175;
        tmp4 *= VJ_F_0_298; tmp5 *= VJ_F_2_053; tmp6 *= VJ_F_3_072; tmp7 *= VJ_F_1_501;
        z1 *= -VJ_F_0_899; z2 *= -VJ_F_2_562; z3 *= -VJ_F_1_961; z4 *= -VJ_F_0_390;
        z3 += z5; z4 += z5;
        p[56] = VJ_DESCALE(tmp4 + z1 + z3, VJ_CONST_BITS + VJ_PASS1_BITS);
        p[40] = VJ_DESCALE(tmp5 + z2 + z4, VJ_CONST_BITS + VJ_PASS1_BITS);
        p[24] = VJ_DESCALE(tmp6 + z2 + z3, VJ_CONST_BITS + VJ_PASS1_BITS);
        p[8] = VJ_DESCALE(tmp7 + z1 + z4, VJ_CONST_BITS + VJ_PASS1_BITS);
    }
}

/* the DCT of a block whose rows and columns come in equal pairs (a source pixel 2 x 2 in the output, zoom 2):
 * the same sums as vj_fdct, bit for bit, with the equal halves folded into four distinct inputs a b c e per
 * line: four row transforms (the odd rows copies) and eight column transforms, about a quarter of the work,
 * and the fourth coefficient of every line is zero */
#define VJ_F2_A 15136     /* 2 F_0_541 + F_0_765 */
#define VJ_F2_B 6271      /* F_1_847 - 2 F_0_541 */
#define VJ_F2_S7 4176     /* F_0_899 + F_1_961 - 2 F_1_175 */
#define VJ_F2_S5 4925     /* F_2_562 + F_0_390 - 2 F_1_175 */
#define VJ_F2_S3 17798    /* F_2_562 + F_1_961 - 2 F_1_175 */
#define VJ_F2_S1 8697     /* 2 F_1_175 - F_0_899 - F_0_390 */
static void vj_fdct2(int32_t *d)
{
    int32_t a, b, c, e, s, t10, t13, dae, dbc, *p;
    uint32_t i;
    for (i = 0; i < 8u; i += 2u) {                          /* rows 0 2 4 6; rows 1 3 5 7 are their copies */
        p = d + i * 8u;
        a = p[0]; b = p[2]; c = p[4]; e = p[6];
        t10 = (a + e) + (b + c); t13 = (a + e) - (b + c);
        dae = a - e; dbc = b - c; s = dae + dbc;
        p[0] = (2 * t10) * (1 << VJ_PASS1_BITS);
        p[4] = 0;
        p[2] = VJ_DESCALE(t13 * VJ_F2_A, VJ_CONST_BITS - VJ_PASS1_BITS);
        p[6] = VJ_DESCALE(-t13 * VJ_F2_B, VJ_CONST_BITS - VJ_PASS1_BITS);
        p[7] = VJ_DESCALE(dbc * VJ_F_0_298 - s * VJ_F2_S7, VJ_CONST_BITS - VJ_PASS1_BITS);
        p[5] = VJ_DESCALE(dbc * VJ_F_2_053 - s * VJ_F2_S5, VJ_CONST_BITS - VJ_PASS1_BITS);
        p[3] = VJ_DESCALE(dae * VJ_F_3_072 - s * VJ_F2_S3, VJ_CONST_BITS - VJ_PASS1_BITS);
        p[1] = VJ_DESCALE(dae * VJ_F_1_501 + s * VJ_F2_S1, VJ_CONST_BITS - VJ_PASS1_BITS);
        p[8] = p[0]; p[9] = p[1]; p[10] = p[2]; p[11] = p[3]; p[12] = p[4]; p[13] = p[5]; p[14] = p[6]; p[15] = p[7];
    }
    for (i = 0, p = d; i < 8u; i++, p++) {                  /* columns: the rows in pairs (0 1) (2 3) (4 5) (6 7) */
        a = p[0]; b = p[16]; c = p[32]; e = p[48];
        t10 = (a + e) + (b + c); t13 = (a + e) - (b + c);
        dae = a - e; dbc = b - c; s = dae + dbc;
        p[0] = VJ_DESCALE(2 * t10, VJ_PASS1_BITS);
        p[32] = 0;
        p[16] = VJ_DESCALE(t13 * VJ_F2_A, VJ_CONST_BITS + VJ_PASS1_BITS);
        p[48] = VJ_DESCALE(-t13 * VJ_F2_B, VJ_CONST_BITS + VJ_PASS1_BITS);
        p[56] = VJ_DESCALE(dbc * VJ_F_0_298 - s * VJ_F2_S7, VJ_CONST_BITS + VJ_PASS1_BITS);
        p[40] = VJ_DESCALE(dbc * VJ_F_2_053 - s * VJ_F2_S5, VJ_CONST_BITS + VJ_PASS1_BITS);
        p[24] = VJ_DESCALE(dae * VJ_F_3_072 - s * VJ_F2_S3, VJ_CONST_BITS + VJ_PASS1_BITS);
        p[8] = VJ_DESCALE(dae * VJ_F_1_501 + s * VJ_F2_S1, VJ_CONST_BITS + VJ_PASS1_BITS);
    }
}

/* ---- one 8 x 8 block: level shift done by the caller (d = sample - 128), DCT, quantise, code ---- */
/* the bit length of a, 1..2047 (a coefficient or a DC difference): a table for the low byte, 8 more above */
static const uint8_t VJ_NBITS[256] = {
    0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8};
VJ_INLINE uint32_t vj_cat(uint32_t a)                       /* bit length of a (a >= 1) */
{
    return a < 256u ? VJ_NBITS[a] : 8u + VJ_NBITS[(a >> 8) & 255u];
}

static int32_t vj_y[4][64], vj_cb[64], vj_cr[64], vj_q[64];   /* the MCU workspace: static, off the caller's stack */

static void vj_dc(vjpeg_t *j, int32_t q0, uint32_t comp, const uint32_t *dc_tab)   /* the DC: the difference to the
                                                         * previous block of this component */
{
    int32_t diff = q0 - j->dc[comp];
    uint32_t t;
    j->dc[comp] = q0;
    if (diff == 0) {
        t = dc_tab[0];
        vj_bits(j, t & 0xFFFFu, t >> 16);
    } else {
        uint32_t a = (uint32_t)(diff < 0 ? -diff : diff), n = vj_cat(a);
        t = dc_tab[n];
        vj_bits(j, t & 0xFFFFu, t >> 16);
        vj_bits(j, diff < 0 ? (uint32_t)(diff - 1) : (uint32_t)diff, n);
    }
}

/* a flat block, its DCT dc (64 x the level-shifted value; nothing else): the DC quantised as vj_block does, then
 * the end of block: the same bits as the whole path, without its DCT or the 63 zeros */
static void vj_flat(vjpeg_t *j, int32_t dc, uint32_t comp, const uint32_t *dc_tab, const uint32_t *ac_tab)
{
    uint32_t a = (uint32_t)(dc < 0 ? -dc : dc), t;
    a = (a * j->recip[comp ? 1 : 0][0] + 0x8000u) >> 16;
    vj_dc(j, dc < 0 ? -(int32_t)a : (int32_t)a, comp, dc_tab);
    t = ac_tab[0x00];
    vj_bits(j, t & 0xFFFFu, t >> 16);
}

static void vj_block(vjpeg_t *j, int32_t *d, uint32_t comp, const uint32_t *dc_tab, const uint32_t *ac_tab, uint32_t dup)
{
    const uint16_t *rc = j->recip[comp ? 1 : 0];
    int32_t *q = vj_q;
    uint32_t i, run, t;
    if (dup)
        vj_fdct2(d);
    else {
        int32_t v = d[0];
        for (i = 1; i < 64u && d[i] == v; i++)
            ;
        if (i == 64u) {                                     /* flat (black around a placed frame, the interface's
                                                             * ground): its DCT is 64 x the value at DC */
            vj_flat(j, v * 64, comp, dc_tab, ac_tab);
            return;
        }
        vj_fdct(d);
    }
    for (i = 0; i < 64u; i++) {                             /* quantise: round(d / (8 q)), into zigzag order */
        int32_t v = d[i], neg = v < 0;
        uint32_t a = (uint32_t)(neg ? -v : v);
        a = (a * rc[i] + 0x8000u) >> 16;
        q[VJ_ZIGZAG[i]] = neg ? -(int32_t)a : (int32_t)a;
    }
    vj_dc(j, q[0], comp, dc_tab);
    for (i = 1, run = 0; i < 64u; i++) {                    /* AC: run lengths, ZRL, EOB */
        int32_t v = q[i];
        if (v == 0) {
            run++;
            continue;
        }
        while (run > 15u) {
            t = ac_tab[0xF0];
            vj_bits(j, t & 0xFFFFu, t >> 16);
            run -= 16u;
        }
        {
            uint32_t a = (uint32_t)(v < 0 ? -v : v), n = vj_cat(a);
            t = ac_tab[(run << 4) | n];
            vj_bits(j, t & 0xFFFFu, t >> 16);
            vj_bits(j, v < 0 ? (uint32_t)(v - 1) : (uint32_t)v, n);
        }
        run = 0;
    }
    if (run) {
        t = ac_tab[0x00];
        vj_bits(j, t & 0xFFFFu, t >> 16);
    }
}

/* ---- the headers ---- */
static void vj_quant_tables(vjpeg_t *j, uint32_t quality)
{
    uint32_t s = quality < 1u ? 1u : quality > 100u ? 100u : quality, t, i;
    s = s < 50u ? 5000u / s : 200u - 2u * s;
    for (t = 0; t < 2u; t++) {
        const uint8_t *base = t ? VJ_CQT : VJ_YQT;
        for (i = 0; i < 64u; i++) {
            uint32_t q = (base[i] * s + 50u) / 100u;
            q = q < 1u ? 1u : q > 255u ? 255u : q;
            j->qt[t][VJ_ZIGZAG[i]] = (uint8_t)q;
            j->recip[t][i] = (uint16_t)((65536u + 4u * q) / (8u * q));
        }
    }
}

static void vj_dht(vjpeg_t *j, uint32_t id, const uint8_t *nr, const uint8_t *val, uint32_t nval)
{
    uint32_t i;
    vj_u16(j, 0xFFC4u);
    vj_u16(j, 2u + 1u + 16u + nval);
    vj_byte(j, id);
    for (i = 0; i < 16u; i++)
        vj_byte(j, nr[i]);
    for (i = 0; i < nval; i++)
        vj_byte(j, val[i]);
}

static void vj_headers(vjpeg_t *j, uint32_t w, uint32_t h)
{
    uint32_t t, i;
    vj_u16(j, 0xFFD8u);                                     /* SOI */
    vj_u16(j, 0xFFE0u);                                     /* APP0 JFIF 1.01, no density, no thumbnail */
    vj_u16(j, 16u);
    vj_byte(j, 'J'); vj_byte(j, 'F'); vj_byte(j, 'I'); vj_byte(j, 'F'); vj_byte(j, 0);
    vj_byte(j, 1); vj_byte(j, 1); vj_byte(j, 0); vj_u16(j, 1); vj_u16(j, 1); vj_byte(j, 0); vj_byte(j, 0);
    for (t = 0; t < 2u; t++) {                              /* DQT, 8-bit, zigzag order */
        vj_u16(j, 0xFFDBu);
        vj_u16(j, 67u);
        vj_byte(j, t);
        for (i = 0; i < 64u; i++)
            vj_byte(j, j->qt[t][i]);
    }
    vj_u16(j, 0xFFC0u);                                     /* SOF0: 8 bit, 3 components, Y 2x2 (4:2:0) */
    vj_u16(j, 17u);
    vj_byte(j, 8);
    vj_u16(j, h);
    vj_u16(j, w);
    vj_byte(j, 3);
    vj_byte(j, 1); vj_byte(j, 0x22); vj_byte(j, 0);
    vj_byte(j, 2); vj_byte(j, 0x11); vj_byte(j, 1);
    vj_byte(j, 3); vj_byte(j, 0x11); vj_byte(j, 1);
    vj_dht(j, 0x00, VJ_YDC_NR, VJ_YDC_VAL, 12);
    vj_dht(j, 0x10, VJ_YAC_NR, VJ_YAC_VAL, 162);
    vj_dht(j, 0x01, VJ_CDC_NR, VJ_CDC_VAL, 12);
    vj_dht(j, 0x11, VJ_CAC_NR, VJ_CAC_VAL, 162);
    vj_u16(j, 0xFFDAu);                                     /* SOS */
    vj_u16(j, 12u);
    vj_byte(j, 3);
    vj_byte(j, 1); vj_byte(j, 0x00);
    vj_byte(j, 2); vj_byte(j, 0x11);
    vj_byte(j, 3); vj_byte(j, 0x11);
    vj_byte(j, 0); vj_byte(j, 63); vj_byte(j, 0);
}

/* ---- the frame ---- */
static int32_t vjpeg_encode(const vjpeg_src_t *src, uint32_t quality, uint8_t *out, uint32_t cap)
{
    static vjpeg_t jb;                                      /* one encoder at a time (the video core's) */
    vjpeg_t *j = &jb;
    int32_t (*y)[64] = vj_y, *cb = vj_cb, *cr = vj_cr;
    uint32_t zoom = src->zoom == 2u ? 2u : 1u, ow = src->w * zoom, oh = src->h * zoom, mx, my, bx, by, px, py;
    static const vjpeg_ycc_t VJ_BLACK = {0, 128, 128, 0};
    uint32_t placed = zoom == 1u && src->lut && src->ow, fx = 0, fy = 0;
    if (placed) {                                           /* the frame at fx, fy in a bigger, black output */
        ow = src->ow; oh = src->oh; fx = src->ox; fy = src->oy;
    }
    j->out = out; j->cap = cap; j->pos = 0; j->bitbuf = 0; j->nbits = 0; j->overflow = 0;
    j->dc[0] = j->dc[1] = j->dc[2] = 0;
    vj_quant_tables(j, quality);
    vj_headers(j, ow, oh);
    for (my = 0; my < oh; my += 16u) {
        if (src->yield)
            src->yield();
        for (mx = 0; mx < ow; mx += 16u) {
            if (placed && (mx + 16u <= fx || mx >= fx + src->w || my + 16u <= fy || my >= fy + src->h)) {
                vj_flat(j, -128 * 64, 0, VJ_YDC, VJ_YAC);  /* wholly outside the placed frame: six black blocks */
                vj_flat(j, -128 * 64, 0, VJ_YDC, VJ_YAC);
                vj_flat(j, -128 * 64, 0, VJ_YDC, VJ_YAC);
                vj_flat(j, -128 * 64, 0, VJ_YDC, VJ_YAC);
                vj_flat(j, 0, 1, VJ_CDC, VJ_CAC);
                vj_flat(j, 0, 2, VJ_CDC, VJ_CAC);
                if (j->overflow)
                    return -1;
                continue;
            }
            if (placed) {
                /* the placed frame (zoom 1, a palette): 2 x 2 pixels at a time, the chroma their average; black
                 * outside it, the output's last row and column repeated (as the general path below, bit for bit) */
                for (by = 0; by < 16u; by += 2u) {
                    uint32_t ra = my + by < oh ? my + by : oh - 1u, rb = my + by + 1u < oh ? my + by + 1u : oh - 1u;
                    const uint8_t *pa = ra >= fy && ra - fy < src->h ? src->px + (ra - fy) * src->pitch : 0;
                    const uint8_t *pb = rb >= fy && rb - fy < src->h ? src->px + (rb - fy) * src->pitch : 0;
                    int32_t *ya = y[(by >> 3) << 1] + (by & 7u) * 8u, *cbr = cb + (by >> 1) * 8u, *crr = cr + (by >> 1) * 8u;
                    for (bx = 0; bx < 16u; bx += 2u) {
                        uint32_t ca = mx + bx < ow ? mx + bx : ow - 1u, cc = mx + bx + 1u < ow ? mx + bx + 1u : ow - 1u;
                        uint32_t ina = ca >= fx && ca - fx < src->w, inc = cc >= fx && cc - fx < src->w, k = (bx >> 3) * 64u + (bx & 7u);
                        const vjpeg_ycc_t *p00 = pa && ina ? &src->lut[pa[ca - fx]] : &VJ_BLACK;
                        const vjpeg_ycc_t *p01 = pa && inc ? &src->lut[pa[cc - fx]] : &VJ_BLACK;
                        const vjpeg_ycc_t *p10 = pb && ina ? &src->lut[pb[ca - fx]] : &VJ_BLACK;
                        const vjpeg_ycc_t *p11 = pb && inc ? &src->lut[pb[cc - fx]] : &VJ_BLACK;
                        ya[k] = (int32_t)p00->y - 128;
                        ya[k + 1u] = (int32_t)p01->y - 128;
                        ya[k + 8u] = (int32_t)p10->y - 128;
                        ya[k + 9u] = (int32_t)p11->y - 128;
                        cbr[bx >> 1] = (((int32_t)p00->cb + p01->cb + p10->cb + p11->cb + 2) >> 2) - 128;
                        crr[bx >> 1] = (((int32_t)p00->cr + p01->cr + p10->cr + p11->cr + 2) >> 2) - 128;
                    }
                }
                vj_block(j, y[0], 0, VJ_YDC, VJ_YAC, 0);
                vj_block(j, y[1], 0, VJ_YDC, VJ_YAC, 0);
                vj_block(j, y[2], 0, VJ_YDC, VJ_YAC, 0);
                vj_block(j, y[3], 0, VJ_YDC, VJ_YAC, 0);
                vj_block(j, cb, 1, VJ_CDC, VJ_CAC, 0);
                vj_block(j, cr, 2, VJ_CDC, VJ_CAC, 0);
                if (j->overflow)
                    return -1;
                continue;
            }
            if (zoom == 2u && src->lut) {
                /* zoom 2 from a palette: the MCU is 8 x 8 source pixels, each 2 x 2 in the output; the chroma
                 * block is those pixels themselves (the 2 x 2 average of one pixel), edges repeated */
                for (by = 0; by < 8u; by++) {
                    uint32_t sy = (my >> 1) + by, yrow;
                    int32_t *yb, *cbrow = cb + by * 8u, *crrow = cr + by * 8u;
                    if (sy >= src->h)
                        sy = src->h - 1u;
                    yrow = sy * src->pitch;
                    yb = y[(by >> 2) << 1] + (by & 3u) * 16u;      /* the block row pair's first row */
                    for (bx = 0; bx < 8u; bx++) {
                        uint32_t sx = (mx >> 1) + bx;
                        const vjpeg_ycc_t *c;
                        int32_t Y, *q4;
                        if (sx >= src->w)
                            sx = src->w - 1u;
                        c = &src->lut[src->px[yrow + sx]];
                        Y = (int32_t)c->y - 128;
                        q4 = yb + (bx >> 2) * 64u + (bx & 3u) * 2u;
                        q4[0] = q4[1] = q4[8] = q4[9] = Y;
                        cbrow[bx] = (int32_t)c->cb - 128;
                        crrow[bx] = (int32_t)c->cr - 128;
                    }
                }
                vj_block(j, y[0], 0, VJ_YDC, VJ_YAC, 1);
                vj_block(j, y[1], 0, VJ_YDC, VJ_YAC, 1);
                vj_block(j, y[2], 0, VJ_YDC, VJ_YAC, 1);
                vj_block(j, y[3], 0, VJ_YDC, VJ_YAC, 1);
                vj_block(j, cb, 1, VJ_CDC, VJ_CAC, 0);
                vj_block(j, cr, 2, VJ_CDC, VJ_CAC, 0);
                if (j->overflow)
                    return -1;
                continue;
            }
            /* gather the 16 x 16 MCU: Y into four blocks, Cb/Cr as 2 x 2 averages, edges repeated */
            for (by = 0; by < 16u; by++) {
                uint32_t oy = my + by, sy = (oy >= oh ? oh - 1u : oy) >> (zoom - 1u), yrow = sy * src->pitch;
                int32_t *yb = y[(by >> 3) << 1], *cbrow = cb + (by >> 1) * 8u, *crrow = cr + (by >> 1) * 8u;
                for (bx = 0; bx < 16u; bx++) {
                    uint32_t ox = mx + bx, sx = (ox >= ow ? ow - 1u : ox) >> (zoom - 1u);
                    int32_t Y, Cb, Cr;
                    if (src->lut) {
                        const vjpeg_ycc_t *c = &src->lut[src->px[yrow + sx]];
                        Y = c->y; Cb = c->cb; Cr = c->cr;
                    } else {
                        const uint16_t *p16 = (const uint16_t *)(const void *)src->px;
                        vjpeg_ycc_565(p16[yrow + sx], &Y, &Cb, &Cr);
                    }
                    yb[(bx >> 3) * 64u + (by & 7u) * 8u + (bx & 7u)] = Y - 128;
                    if ((by & 1u) == 0u && (bx & 1u) == 0u) {
                        cbrow[bx >> 1] = Cb; crrow[bx >> 1] = Cr;
                    } else {
                        cbrow[bx >> 1] += Cb; crrow[bx >> 1] += Cr;
                    }
                }
            }
            for (px = 0; px < 64u; px++) {                   /* the chroma averages, level shifted */
                cb[px] = ((cb[px] + 2) >> 2) - 128;
                cr[px] = ((cr[px] + 2) >> 2) - 128;
            }
            vj_block(j, y[0], 0, VJ_YDC, VJ_YAC, 0);
            vj_block(j, y[1], 0, VJ_YDC, VJ_YAC, 0);
            vj_block(j, y[2], 0, VJ_YDC, VJ_YAC, 0);
            vj_block(j, y[3], 0, VJ_YDC, VJ_YAC, 0);
            vj_block(j, cb, 1, VJ_CDC, VJ_CAC, 0);
            vj_block(j, cr, 2, VJ_CDC, VJ_CAC, 0);
            if (j->overflow)
                return -1;
        }
        (void)py;
    }
    vj_flush(j);
    vj_u16(j, 0xFFD9u);                                     /* EOI */
    return j->overflow ? -1 : (int32_t)j->pos;
}
