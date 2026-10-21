/* SPDX-License-Identifier: GPL-3.0-only */
/* The Synthesthetic wordmark: its letters across the spectrum (red, orange, yellow, green, blue, indigo, violet),
 * the spectrum turning with time so every letter fades into the next colour. The hue is 0..255 round the
 * circle: 0 red, 43 yellow, 85 green, 128 cyan, 171 blue, 213 magenta. */
UI_SMALL static uint16_t rainbow565(uint32_t h)
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
    return RGB(r, g, b);
}

/* the text, a hue a letter from phase, step apart (14: the thirteen letters span red to violet); returns the x after it */
UI_SMALL static int32_t rainbow_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint32_t phase, uint32_t step)
{
    char c[2] = {0, 0};
    uint32_t i;
    for (i = 0; s[i]; i++) {
        c[0] = s[i];
        x = cv_text(x, y, f, c, rainbow565(phase + i * step));
    }
    return x;
}
