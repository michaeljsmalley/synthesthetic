/* SPDX-License-Identifier: GPL-3.0-only */
/* The Synthesthetic boot screen: a video signal coming on. A line opens into colour bars and closes again, the
 * ring draws itself round, the name types in across the spectrum (rainbow.c), then the viewfinder's corners,
 * a LIVE tag and what it is. While the logo stays (main.c, synthesthetic_splash_tick) the ring lives: two
 * rainbows chase round it, a bright lobe circles the other way, a thin inner rim turns against it, the whole
 * ring breathes with a glowing halo and rays, sparks fly, the wave inside pulses and cycles its colour, the
 * name's spectrum turns and LIVE blinks.
 * The ring is drawn live, pixel by pixel (no bitmap: tools/gen_logo.py makes the same icon for the web). */
#ifndef UI_SMALL                 /* compiled for size: not the audio path (gfx.c UI_SMALL); the host tests: nothing */
#define UI_SMALL
#endif
#ifndef FELUCCA_LOGO_TEXT       /* 1: no ring, the name only (tools/build.py builds so by itself when the flash is full) */
#define FELUCCA_LOGO_TEXT 0
#endif
#ifndef SPLASH_STEP
#define SPLASH_STEP(ms) fm1_delay_ms(ms)   /* a step of the animation (the host test dumps a frame instead) */
#endif
#define RING_N 124u                                     /* the ring's square (the canvas holds 124 rows) */
#define RING_C 62                                       /* its centre */
#define SPLASH_ICON_Y 6u
#define SPLASH_NAME_Y (FELUCCA_LOGO_TEXT ? 80u : 134u)
#define SPLASH_RED RGB(255, 40, 40)

UI_SMALL static uint32_t splash_angle(int32_t dx, int32_t dy)   /* 0 up, clockwise, 1024 a turn */
{
    int32_t u = dx, v = -dy, au = u < 0 ? -u : u, av = v < 0 ? -v : v, t, th;
    if (!au && !av)
        return 0;
    t = au >= av ? 128 * av / au : 256 - 128 * au / av;     /* the first quadrant, from +x */
    th = u >= 0 ? (v >= 0 ? t : 1024 - t) : (v >= 0 ? 512 - t : 512 + t);
    return (uint32_t)(256 - th + 1024) & 1023u;
}

UI_SMALL static uint16_t splash_hsv(uint32_t h, uint32_t br)   /* hue 0..255 at brightness 0..255 */
{
    uint32_t seg = (h & 255u) / 43u, f = ((h & 255u) % 43u) * 255u / 42u, r, g, b;
    switch (seg) {
    case 0: r = 255; g = f; b = 0; break;
    case 1: r = 255 - f; g = 255; b = 0; break;
    case 2: r = 0; g = 255; b = f; break;
    case 3: r = 0; g = 255 - f; b = 255; break;
    case 4: r = f; g = 0; b = 255; break;
    default: r = 255; g = 0; b = 255 - f; break;
    }
    return RGB(r * br / 255u, g * br / 255u, b * br / 255u);
}

#if !FELUCCA_LOGO_TEXT
/* the ring's rows r0..r1 to the screen, drawn up to sweep (0..1024; 1024 whole) at time t (main.c: 13 a tick) */
UI_SMALL static void logo_ring_rows(uint32_t r0, uint32_t r1, uint32_t sweep, uint32_t t)
{
    int32_t p = SINE[(t * 6u) & 1023u];                 /* the breath, Q15 */
    int32_t R = 46 + ((5 * p) >> 15), r = 30 - ((3 * p) >> 15), H = R + 9;   /* outer, inner, the halo's edge */
    int32_t R2 = R * R, r2 = r * r, H2 = H * H, i2 = (r - 3) * (r - 3), glow = 130 + ((90 * p) >> 15);
    uint32_t y, x, k, seed = t * 2654435761u + 12345u;
    int32_t px = 0, py = 0;
    cv_begin(RING_N, r1 - r0, C_BLACK);
    for (y = r0; y < r1; y++) {
        int32_t dy = (int32_t)y - RING_C;
        uint16_t *row = cv_px + (y - r0) * RING_N;
        for (x = 0; x < RING_N; x++) {
            int32_t dx = (int32_t)x - RING_C, d2 = dx * dx + dy * dy, lobe;
            uint32_t a, hue, br;
            if (d2 > H2 || d2 < i2)
                continue;
            a = splash_angle(dx, dy);
            if (sweep < 1024u && a > sweep)
                continue;
            if (d2 < r2) {                                  /* the inner rim: a thin rainbow turning the other way */
                hue = 255u - (a >> 2) + t * 3u;
                br = 230u;
            } else {
                hue = (a >> 1) + t * 2u;                    /* two rainbows round the ring, chasing */
                lobe = SINE[(a + 256u + t * 5u) & 1023u];   /* a bright lobe circling the other way */
                if (d2 <= R2)
                    br = 150u + (uint32_t)((lobe + 32768) >> 9) * 105u / 128u;   /* 150..255 */
                else
                    br = (uint32_t)glow * (uint32_t)(H2 - d2) / (uint32_t)(H2 - R2);   /* the halo, fading, breathing */
            }
            row[x] = swap16(splash_hsv(hue, br));
        }
    }
    for (k = 0; k < 8u; k++) {                          /* rays off the ring, turning, their length breathing */
        uint32_t a = (k * 128u + t * 7u) & 1023u;
        int32_t l0 = R + 3, l1 = R + 7 + ((6 * p) >> 15), ca = SINE[(a + 256u) & 1023u], sa = SINE[a];
        if (sweep < 1024u && a > sweep)
            continue;
        if (l1 > 61)
            l1 = 61;
        cv_line(RING_C + ((l0 * ca) >> 15), RING_C - (int32_t)r0 - ((l0 * sa) >> 15), RING_C + ((l1 * ca) >> 15),
                RING_C - (int32_t)r0 - ((l1 * sa) >> 15), splash_hsv((a >> 1) + t * 2u, 255));
    }
    for (k = 0; k < 24u; k++) {                         /* sparks on the ring */
        uint32_t a = seed & 1023u;
        int32_t rr, sx, sy;
        seed = seed * 1103515245u + 12345u;
        rr = r + 2 + (int32_t)(seed % (uint32_t)(R - r - 3));
        seed = seed * 1103515245u + 12345u;
        if (sweep < 1024u && a > sweep)
            continue;
        sx = RING_C + ((rr * SINE[(a + 256u) & 1023u]) >> 15);
        sy = RING_C - ((rr * SINE[a]) >> 15);
        if (sy >= (int32_t)r0 && sy < (int32_t)r1 && sx < (int32_t)RING_N - 1) {
            cv_px[(sy - (int32_t)r0) * (int32_t)RING_N + sx] = swap16(C_WHITE);
            cv_px[(sy - (int32_t)r0) * (int32_t)RING_N + sx + 1] = swap16((k & 1u) ? C_WHITE : RGB(255, 240, 120));
        }
    }
    {                                                   /* the wave inside: pulsing, its colour cycling; a parabola
                                                         * envelope keeps it well inside the rim (zero at the ends) */
        int32_t ri = r - 4, w = ri - 5, amp = 10 + ((4 * p) >> 15), cy = RING_C - (int32_t)r0;
        uint16_t c = splash_hsv(t * 3u + 128u, 255);
        for (k = 0; k <= (uint32_t)w; k++) {
            int32_t xr = (int32_t)k * 2 - w, env = w * w - xr * xr;
            int32_t wx = RING_C + xr, wy = cy - (((amp * env / (w * w)) * SINE[(t * 6u + k * 1024u / (uint32_t)w) & 1023u]) >> 15);
            if (k) {
                cv_line(px, py - 1, wx, wy - 1, c);
                cv_line(px, py, wx, wy, C_WHITE);
                cv_line(px, py + 1, wx, wy + 1, c);
            }
            px = wx;
            py = wy;
        }
    }
    cv_blit((240u - RING_N) / 2u, SPLASH_ICON_Y + r0);
}
#endif

/* colour bars h rows high across the middle of the screen (0: none); the rows the last bars took are cleared
 * with them, whatever the rounding */
static uint32_t splash_bars_y0 = 120u, splash_bars_y1 = 120u;   /* the last bars' rows (y1 exclusive) */
UI_SMALL static void splash_bars(uint32_t h)
{
    static const uint16_t BAR[7] = {RGB(235, 235, 235), RGB(235, 235, 0), RGB(0, 235, 235), RGB(0, 235, 0),
                                    RGB(235, 0, 235), RGB(235, 0, 0), RGB(0, 0, 235)};
    uint32_t y0 = 120u - h / 2u, y1 = y0 + h, b0 = y0 < splash_bars_y0 ? y0 : splash_bars_y0, b1 = y1 > splash_bars_y1 ? y1 : splash_bars_y1, i;
    if (b1 - b0 > 120u)
        b1 = b0 + 120u;
    cv_begin(240, b1 - b0, C_BLACK);
    for (i = 0; i < 7u; i++)
        cv_rect((int32_t)(i * 240u / 7u), (int32_t)(y0 - b0), (int32_t)((i + 1u) * 240u / 7u - i * 240u / 7u), (int32_t)h, BAR[i]);
    cv_blit(0, b0);
    splash_bars_y0 = y0;
    splash_bars_y1 = y1;
}

/* the name's first n letters across the spectrum (rainbow.c), turned by phase */
UI_SMALL static void logo_name_draw(uint32_t y0, uint32_t phase, uint32_t n)
{
    static const char NAME[] = "SYNTHESTHETIC";
    char part[14];
    const felucca_font_t *f = text_w(&FONT_L, NAME) <= 236 ? &FONT_L : &FONT_S;
    uint32_t h = f == &FONT_L ? 36u : 20u, i;
    for (i = 0; i < n && i < 13u; i++)
        part[i] = NAME[i];
    part[i] = 0;
    cv_begin(240, h, C_BLACK);
    rainbow_text(120 - text_w(f, NAME) / 2, 0, f, part, phase, 14);
    cv_blit(0, y0);
}

UI_SMALL static void splash_corners(void)              /* the viewfinder's brackets */
{
    lcd_fill(0, 0, 18, 2, C_WHITE); lcd_fill(0, 0, 2, 18, C_WHITE);
    lcd_fill(222, 0, 18, 2, C_WHITE); lcd_fill(238, 0, 2, 18, C_WHITE);
    lcd_fill(0, 238, 18, 2, C_WHITE); lcd_fill(0, 222, 2, 18, C_WHITE);
    lcd_fill(222, 238, 18, 2, C_WHITE); lcd_fill(238, 222, 2, 18, C_WHITE);
}

UI_SMALL static void splash_live(uint32_t on)          /* a red dot and LIVE, top right */
{
    cv_begin(64, 16, C_BLACK);
    if (on)
        cv_rect(2, 4, 8, 8, SPLASH_RED);
    cv_text(16, 1, &FONT_S, "LIVE", on ? SPLASH_RED : RGB(110, 40, 40));
    cv_blit(170, 6);
}

/* while the logo stays: the ring lives, the spectrum turns, LIVE blinks (main.c, every few ms) */
UI_SMALL static void synthesthetic_splash_tick(uint32_t t)
{
    logo_name_draw(SPLASH_NAME_Y, t, 13);
#if !FELUCCA_LOGO_TEXT
    logo_ring_rows(0, RING_N, 1024u, t);
#endif
    splash_live(((t / 64u) & 1u) == 0u);
}

/* the boot screen */
UI_SMALL static void synthesthetic_splash(void)
{
    static const uint8_t H[8] = {2, 6, 14, 26, 44, 66, 92, 120};   /* the signal opening */
    const char *v = FELUCCA_VERSION;
    uint32_t i, ph;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    splash_bars_y0 = splash_bars_y1 = 120u;
    for (i = 0; i < 8u; i++) {
        splash_bars(H[i]);
        SPLASH_STEP(14);
    }
    SPLASH_STEP(70);
    for (i = 8; i-- > 0;) {                             /* and closing */
        splash_bars(H[i] / 2u);
        SPLASH_STEP(8);
    }
    splash_bars(0);
#if !FELUCCA_LOGO_TEXT
    for (i = 1; i <= 12u; i++) {                        /* the ring draws itself round, alive already */
        logo_ring_rows(0, RING_N, i * 1024u / 12u, i * 9u);
        SPLASH_STEP(10);
    }
#endif
    for (i = 1, ph = 0; i <= 13u; i++, ph += 9u) {      /* the name types in */
        logo_name_draw(SPLASH_NAME_Y, ph, i);
        SPLASH_STEP(22);
    }
    splash_corners();
    splash_live(1);
    while (*v && *v != ' ')
        v++;                                            /* "SYNTHESTHETIC 0.1" -> "0.1" */
    while (*v == ' ')
        v++;
    cv_begin(240, 52, C_BLACK);
    cv_text(120 - text_w(&FONT_S, "AUDIO + VIDEO SYNTH") / 2, 0, &FONT_S, "AUDIO + VIDEO SYNTH", C_AMB);
    cv_text(120 - text_w(&FONT_S, v) / 2, 18, &FONT_S, v, RGB(196, 196, 204));
    cv_text(120 - text_w(&FONT_S, "based on sloop + felucca") / 2, 36, &FONT_S, "based on sloop + felucca", RGB(96, 96, 104));
    cv_blit(0, 174);
    SPLASH_STEP(1);
}
