/* SPDX-License-Identifier: GPL-3.0-only */
/* The video core's JPEG encoder (firmware/src/vjpeg.c) on the host: synthetic frames through a 256-colour
 * palette and as RGB565, written as JPEG next to a PPM of what was encoded; tests/vjpeg_check.py decodes
 * them with Pillow and measures the PSNR and the size. Also: the size cap (-1, never a write past cap), zoom 2
 * (120 x 120 in, 240 x 240 out), odd sizes (edges repeated), and the time per frame here, for scale only.
 *
 *   cc -O2 -Wall -o build/host/vjpeg_test tests/vjpeg_test.c && build/host/vjpeg_test build/host
 * Exit status: the number of failed checks. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../firmware/src/vjpeg.c"

static int bad;
static void check(int ok, const char *what) { printf("vjpeg: %-66s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

static uint8_t pal_rgb[256][3];
static vjpeg_ycc_t lut[256];
static uint8_t frame[240 * 240];
static uint16_t frame565[240 * 240];
static uint8_t out[64 * 1024];

static void palette_wheel(void)                             /* a hue wheel with darkness: index = hue 0..63 | shade */
{
    uint32_t i;
    for (i = 0; i < 256u; i++) {
        uint32_t hue = i & 63u, shade = i >> 6, r, g, b, seg = hue / 11u, f = (hue % 11u) * 255u / 11u;
        switch (seg) {
        case 0: r = 255; g = f; b = 0; break;
        case 1: r = 255 - f; g = 255; b = 0; break;
        case 2: r = 0; g = 255; b = f; break;
        case 3: r = 0; g = 255 - f; b = 255; break;
        case 4: r = f; g = 0; b = 255; break;
        default: r = 255; g = 0; b = 255 - f; break;
        }
        pal_rgb[i][0] = (uint8_t)(r * (shade + 1u) / 4u);
        pal_rgb[i][1] = (uint8_t)(g * (shade + 1u) / 4u);
        pal_rgb[i][2] = (uint8_t)(b * (shade + 1u) / 4u);
    }
    vjpeg_lut_from_rgb(pal_rgb, lut, 256);
}

static uint32_t rnd_s = 0x2545F491u;
static uint32_t rnd(void) { rnd_s ^= rnd_s << 13; rnd_s ^= rnd_s >> 17; rnd_s ^= rnd_s << 5; return rnd_s; }

static void frame_plasma(uint32_t w, uint32_t h, uint32_t t)
{
    uint32_t x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int32_t v = (int32_t)((x * 3u + t) & 255u) + (int32_t)((y * 5u + t * 2u) & 255u) + (int32_t)(((x * y) >> 6) & 255u);
            frame[y * w + x] = (uint8_t)((v >> 1) & 255u);
        }
}
static void frame_shapes(uint32_t w, uint32_t h)
{
    uint32_t x, y, i;
    memset(frame, 0, w * h);
    for (i = 0; i < 24u; i++) {
        int32_t cx = (int32_t)(rnd() % w), cy = (int32_t)(rnd() % h), r = 6 + (int32_t)(rnd() % 40);
        uint8_t c = (uint8_t)(64u + rnd() % 192u);
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                int32_t dx = (int32_t)x - cx, dy = (int32_t)y - cy;
                if (dx * dx + dy * dy < r * r)
                    frame[y * w + x] = c;
            }
    }
}
static void frame_noise(uint32_t w, uint32_t h)
{
    uint32_t i;
    for (i = 0; i < w * h; i++)
        frame[i] = (uint8_t)rnd();
}

static void write_ppm(const char *path, uint32_t w, uint32_t h, uint32_t zoom, int from565)
{
    FILE *f = fopen(path, "wb");
    uint32_t x, y;
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", w * zoom, h * zoom);
    for (y = 0; y < h * zoom; y++)
        for (x = 0; x < w * zoom; x++) {
            uint32_t sx = x / zoom, sy = y / zoom;
            uint8_t rgb[3];
            if (from565) {
                uint32_t c = frame565[sy * w + sx];
                rgb[0] = (uint8_t)((((c >> 11) & 31u) * 527u + 23u) >> 6);
                rgb[1] = (uint8_t)((((c >> 5) & 63u) * 259u + 33u) >> 6);
                rgb[2] = (uint8_t)(((c & 31u) * 527u + 23u) >> 6);
            } else
                memcpy(rgb, pal_rgb[frame[sy * w + sx]], 3);
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

static int32_t encode_to(const char *dir, const char *name, uint32_t w, uint32_t h, uint32_t zoom, uint32_t q, int from565, double *ms)
{
    vjpeg_src_t s = {from565 ? (const uint8_t *)frame565 : frame, from565 ? 0 : lut, w, h, w, zoom};
    char path[512];
    clock_t t0 = clock();
    int32_t n = vjpeg_encode(&s, q, out, sizeof out);
    *ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
    if (n > 0) {
        FILE *f;
        snprintf(path, sizeof path, "%s/vjpeg-%s-q%u.jpg", dir, name, q);
        f = fopen(path, "wb");
        if (f) { fwrite(out, 1, (size_t)n, f); fclose(f); }
    }
    snprintf(path, sizeof path, "%s/vjpeg-%s.ppm", dir, name);
    write_ppm(path, w, h, zoom, from565);
    return n;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/host";
    double ms;
    int32_t n;
    uint32_t q, i;
    char what[128];
    palette_wheel();
    /* zigzag table: the standard order, derived by its rule */
    {
        static uint8_t zz[64];
        uint32_t x = 0, y = 0, k, ok = 1;
        for (k = 0; k < 64u; k++) {
            zz[y * 8u + x] = (uint8_t)k;
            if ((x + y) & 1u) { if (y == 7u) x++; else if (x == 0u) y++; else { x--; y++; } }
            else { if (x == 7u) y++; else if (y == 0u) x++; else { x++; y--; } }
        }
        for (k = 0; k < 64u; k++) ok &= zz[k] == VJ_ZIGZAG[k];
        check(ok, "zigzag table = the standard order");
    }
    /* every Huffman entry the encoder can use has a length 2..16 */
    {
        uint32_t ok = 1, r, s;
        for (i = 0; i < 12u; i++) ok &= (VJ_YDC[i] >> 16) >= 2u && (VJ_YDC[i] >> 16) <= 16u && (VJ_CDC[i] >> 16) >= 2u && (VJ_CDC[i] >> 16) <= 16u;
        ok &= (VJ_YAC[0] >> 16) >= 2u && (VJ_YAC[0xF0] >> 16) >= 2u && (VJ_CAC[0] >> 16) >= 2u && (VJ_CAC[0xF0] >> 16) >= 2u;
        for (r = 0; r < 16u; r++) for (s = 1; s <= 10u; s++) ok &= (VJ_YAC[(r << 4) | s] >> 16) >= 2u && (VJ_CAC[(r << 4) | s] >> 16) >= 2u;
        check(ok, "Huffman tables: every DC category and AC run/size has a code");
    }
    /* plasma 240 x 240 at three qualities */
    frame_plasma(240, 240, 7);
    for (q = 50; q <= 90u; q += 20u) {
        n = encode_to(dir, "plasma240", 240, 240, 1, q, 0, &ms);
        snprintf(what, sizeof what, "plasma 240x240 q%u: %d B, %.2f ms on the host", q, n, ms);
        check(n > 600 && n < 60000, what);
    }
    frame_shapes(240, 240);
    n = encode_to(dir, "shapes240", 240, 240, 1, 75, 0, &ms);
    snprintf(what, sizeof what, "shapes 240x240 q75: %d B, %.2f ms", n, ms);
    check(n > 600, what);
    frame_noise(240, 240);
    n = encode_to(dir, "noise240", 240, 240, 1, 75, 0, &ms);
    snprintf(what, sizeof what, "noise 240x240 q75 (worst case): %d B, %.2f ms", n, ms);
    check(n > 600, what);
    /* zoom 2: a 120 x 120 frame as 240 x 240 */
    frame_plasma(120, 120, 3);
    n = encode_to(dir, "plasma120x2", 120, 120, 2, 80, 0, &ms);
    snprintf(what, sizeof what, "plasma 120x120 zoom 2 -> 240x240 q80: %d B, %.2f ms", n, ms);
    check(n > 600, what);
    {   /* the folded DCT of zoom 2 (vj_fdct2) is the plain one's sums, bit for bit: the same frame upsampled by
         * hand and encoded at zoom 1 through the general path gives the same bytes */
        static uint8_t up[240 * 240], out2[64 * 1024];
        uint32_t x, yy, n2, same = 1, i;
        vjpeg_src_t s1 = {frame, lut, 120, 120, 120, 2, 0}, s2 = {up, lut, 240, 240, 240, 1, 0};
        int32_t n1;
        for (yy = 0; yy < 240u; yy++)
            for (x = 0; x < 240u; x++)
                up[yy * 240u + x] = frame[(yy / 2u) * 120u + x / 2u];
        n1 = vjpeg_encode(&s1, 80, out, sizeof out);
        n2 = (uint32_t)vjpeg_encode(&s2, 80, out2, sizeof out2);
        for (i = 0; i < n2 && same; i++)
            same = out[i] == out2[i];
        snprintf(what, sizeof what, "zoom 2's folded DCT equals the plain path on the upsampled frame, byte for byte (%d B)", n1);
        check(n1 > 0 && (uint32_t)n1 == n2 && same, what);
    }
    {   /* placement (SCREEN): a 180 x 180 frame at 72, 0 in a 320 x 180 output, black around it, gives the bytes of
         * that picture composed by hand; an interface-like frame (a black ground: flat blocks skip the DCT) */
        static uint8_t small[180 * 180], comp[320 * 180], o2[64 * 1024];
        static uint8_t prgb[256][3];
        static vjpeg_ycc_t plut[256];
        uint32_t x, yy, i2, same = 1;
        int32_t n1, n2;
        vjpeg_src_t a = {small, plut, 180, 180, 180, 1, 0, 320, 180, 72, 0}, b = {comp, plut, 320, 180, 320, 1, 0};
        for (i2 = 0; i2 < 256u; i2++) {
            prgb[i2][0] = (uint8_t)(i2 ? (i2 * 37u) & 255u : 0u);
            prgb[i2][1] = (uint8_t)(i2 ? (i2 * 91u) & 255u : 0u);
            prgb[i2][2] = (uint8_t)(i2 ? (i2 * 53u) & 255u : 0u);
        }
        vjpeg_lut_from_rgb((const uint8_t (*)[3])prgb, plut, 256);
        memset(small, 0, sizeof small);
        for (yy = 0; yy < 180u; yy++)
            for (x = 0; x < 180u; x++) {
                if (yy % 24u == 3u || (x % 30u == 7u && yy > 20u))
                    small[yy * 180u + x] = 200;                 /* lines */
                if (yy >= 40u && yy < 56u && x >= 10u && x < 170u && ((x * 7u + yy * 3u) % 11u) < 4u)
                    small[yy * 180u + x] = (uint8_t)(1u + (x / 8u) % 200u);   /* "text" */
                if (yy >= 100u && yy < 160u && x >= 60u && x < 150u)
                    small[yy * 180u + x] = (uint8_t)(30u + (x + yy) % 120u);  /* a graph */
            }
        memset(comp, 0, sizeof comp);
        for (yy = 0; yy < 180u; yy++)
            memcpy(comp + yy * 320u + 72u, small + yy * 180u, 180u);
        n1 = vjpeg_encode(&a, 80, out, sizeof out);
        n2 = vjpeg_encode(&b, 80, o2, sizeof o2);
        for (i2 = 0; n1 > 0 && i2 < (uint32_t)n1 && same; i2++)
            same = out[i2] == o2[i2];
        snprintf(what, sizeof what, "placement: 180x180 at 72,0 in 320x180 == the composed picture, byte for byte (%d B)", n1);
        check(n1 > 0 && n1 == n2 && same, what);
    }
    /* an odd size: edges repeated, no reads past the frame (a guard page would be better; ASan covers it) */
    frame_plasma(100, 76, 1);
    n = encode_to(dir, "odd100x76", 100, 76, 1, 75, 0, &ms);
    snprintf(what, sizeof what, "odd 100x76 q75: %d B", n);
    check(n > 300, what);
    /* RGB565 source */
    for (i = 0; i < 240u * 240u; i++) {
        uint32_t x = i % 240u, y = i / 240u;
        frame565[i] = (uint16_t)(((x >> 3) << 11) | ((y >> 2) << 5) | (((x + y) >> 4) & 31u));
    }
    n = encode_to(dir, "rgb565", 240, 240, 1, 75, 1, &ms);
    snprintf(what, sizeof what, "RGB565 240x240 q75: %d B, %.2f ms", n, ms);
    check(n > 600, what);
    /* the cap: the noise frame into a small buffer refuses without writing past the end */
    {
        vjpeg_src_t s = {frame, lut, 240, 240, 240, 1};
        uint8_t small[4096 + 16];
        frame_noise(240, 240);
        memset(small, 0xA5, sizeof small);
        n = vjpeg_encode(&s, 90, small, 4096);
        check(n == -1, "size cap: a frame that does not fit returns -1");
        check(small[4096] == 0xA5 && small[4096 + 15] == 0xA5, "size cap: nothing written past the buffer");
    }
    printf("vjpeg: %d failed\n", bad);
    return bad;
}
