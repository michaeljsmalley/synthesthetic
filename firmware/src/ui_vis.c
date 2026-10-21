/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthesthetic's visualiser on the FM-1's screen, and its controls. HOME on the TRACKS screen opens it: the
 * frame the video core draws for the webcam (vrender.c, vproc.c; 160 x 90 palette indices, each frame its own
 * palette) is shown 1.5 x (240 x 135) between a top bar (the mode, or a message; AUTO when the autopilot runs)
 * and a bottom bar (the knobs). The screen and the webcam show the same picture (the webcam's overlay aside).
 *
 *   SELECT       the mode (31, vis_set.h VIS_NAME; the last, SCREEN, streams the display itself: a card here)
 *   PRESETS      the palette (9)
 *   ALGORITHM    the autopilot: OFF, a new picture every 4, 8, 16 or 32 bars
 *   KNOB 1..4    AMOUNT, RATE, TRAILS, COLOR (the knobs of the EYESY)
 *   HOME, or a page button: closes it
 * All of it is kept with the settings (vis_set, the side record). The attract mode opens the visualiser after
 * a while without input on the TRACKS screen (menu VJ > ATTRACT); any button or key closes it again.
 * vis_on, vis_shown_last, vis_name_t: ui_draw.c; vis_style: panel.c. Without the video core (an audio-only
 * build, or while it is stopped for an update) the screen says so. */
static const char *const VIS_KNOB[4] = {"AMOUNT ", "RATE ", "TRAILS ", "COLOR "};
static const char *const VIS_KNOB_S[4] = {"AMT ", "RATE ", "TRL ", "COL "};
static const char *const VIS_AUTO_NAME[5] = {"OFF", "4 BARS", "8 BARS", "16 BARS", "32 BARS"};
static uint8_t vis_attract;                             /* opened by the attract mode: any button or key closes it */
static uint32_t vis_attract_in;                         /* ui_input_ms then (a knob turned since closes it too) */
static uint32_t vis_rnd_s = 0x2545F491u;
static uint32_t vis_rnd(void) { vis_rnd_s = vis_rnd_s * 1103515245u + 12345u; return vis_rnd_s >> 8; }
#if FELUCCA_VCORE
#define VIS_Y 52u                                       /* the picture: 240 x 135 at 1.5 x, between the two bars */
#define VIS_PH 135u
#define VIS_ROWS 30u                                    /* frame rows per pass: 45 screen rows, three passes */
static uint8_t vis_copy[VF_W * VIS_ROWS] __attribute__((section(".pool")));
static uint32_t vis_frames_shown;                       /* vc1.frames of the frame on the screen */
static uint8_t vis_bars_due, vis_top_msg;               /* redraw the bars; the top bar shows a message */
static uint8_t vis_card;                                /* SCREEN's card is on the screen */
#endif

static int vis_shown(void) { return vis_on && !ui.home && !ui.menu && cur_page()->scope == SC_TRK; }
static void vis_open(void)
{
    vis_on = 1;
    vis_name_t = 30;
    ui.force = 1;
}

static void vis_changed(void)                           /* a setting of the user's: kept with the settings */
{
    vis_dirty = 1;
    settings_later = 1;
#if FELUCCA_VCORE
    vis_bars_due = 1;
#endif
}

/* a knob turned on the visualiser: s detents; COLOR wraps, the others stop at the ends */
static void vis_knob_turn(uint32_t k, int32_t s)
{
    char v[8];
    int32_t n = (int32_t)vis_knob[k] + s * 8;
    vis_knob[k] = (uint8_t)(k == 3u ? n & 255 : clamp(n, 0, 255));
    fmt_int(v, (int32_t)vis_knob[k] * 100 / 255);
    ui_say(VIS_KNOB[k], v);                             /* "AMOUNT 50": the top bar a second */
    vis_changed();
}

/* SELECT on the visualiser: the next / previous mode (round) */
static void vis_select(int32_t s)
{
    vis_style = (uint8_t)((vis_style + (s > 0 ? 1u : VIS_N - 1u)) % VIS_N);
    vis_set.mode = vis_style;
    vis_var = 0;                                        /* (by hand: the mode as it is; the autopilot varies it) */
    vis_name_t = 30;
    vis_changed();
}

/* PRESETS on the visualiser: the palette */
static void vis_palette_turn(int32_t s)
{
    vis_set.palette = (uint8_t)((vis_set.palette + (s > 0 ? 1u : VIS_NPAL - 1u)) % VIS_NPAL);
    vis_pal_live = vis_set.palette;
    ui_say("PALETTE ", VIS_PAL_NAME[vis_set.palette]);
    vis_changed();
}

/* ALGORITHM on the visualiser: the autopilot, OFF .. 32 BARS */
static void vis_auto_turn(int32_t s)
{
    vis_set.autopilot = (uint8_t)clamp((int32_t)vis_set.autopilot + (s > 0 ? 1 : -1), 0, 4);
    ui_say("AUTOPILOT ", VIS_AUTO_NAME[vis_set.autopilot]);
    vis_changed();
}

/* the attract mode: any button or key closes the visualiser it opened (ui_input.c, before the input is used:
 * the button or key then does what it does on the TRACKS screen; HOME's tap is eaten) */
static uint8_t home_eat;                                /* HOME pressed to unlock (ui_input.c) or to close: its tap is eaten */
static void vis_attract_close(uint32_t pressed)
{
    vis_attract = 0;
    vis_on = 0;
    ui.force = 1;
    if ((pressed >> panel.btn[B_HOME]) & 1u)
        home_eat = 1;
}

/* the main loop, every pass (vcore.c vcore_poll): MIDI's and the camera's requests, the autopilot, the attract
 * mode */
static uint32_t vis_auto_last = 0xFFFFFFFFu;            /* the bar of the autopilot's last change */
static uint32_t vis_auto_n;                             /* its changes, counted (every other one: the palette too) */
static void vis_tick(void)
{
    static uint8_t knob_seen[4], seen_ok;
    uint32_t r, k;
    if (!seen_ok) {                                     /* (the knobs as loaded: not a change) */
        for (k = 0; k < 4u; k++)
            knob_seen[k] = vis_set.knob[k];
        seen_ok = 1;
    }
    /* MIDI's (the visuals' channel) and the camera's (USB) changes count as the panel's: kept with the settings */
    if ((r = vis_req_mode) != 0u) {                     /* MIDI CC 24 / Program Change on the visuals' channel */
        vis_req_mode = 0;
        vis_style = (uint8_t)((r - 1u) % VIS_N);
        vis_set.mode = vis_style;
        vis_var = 0;
        vis_name_t = 30;
        vis_changed();
    }
    if ((r = vis_req_pal) != 0u) {
        vis_req_pal = 0;
        vis_pal_live = (uint8_t)((r - 1u) % VIS_NPAL);
        vis_set.palette = vis_pal_live;
        vis_changed();
    }
    if ((r = vis_req_auto) != 0u) {
        vis_req_auto = 0;
        vis_set.autopilot = (uint8_t)((r - 1u) % 5u);
        vis_changed();
    }
    if ((r = vis_req_hud) != 0u) {
        vis_req_hud = 0;
        vis_set.hud = (uint8_t)((r - 1u) & 3u);
        vis_changed();
    }
    for (k = 0; k < 4u; k++)                            /* (the knobs: CC 20..23, the camera's controls, the panel) */
        if (vis_set.knob[k] != knob_seen[k]) {
            knob_seen[k] = vis_set.knob[k];
            vis_changed();
        }
    if (vis_req_title) {                                /* a title over SysEx (tools/fm1_title.py): kept */
        vis_req_title = 0;
        vis_changed();
    }
    if (vis_req_redraw) {                               /* SCREEN's copy starts (vcore.c): everything drawn once */
        vis_req_redraw = 0;
        ui.force = 1;
    }
#if FELUCCA_VCORE
    {   /* the autopilot: on a bar that is a multiple of its length (the song's bars when playing; the tempo's
         * when stopped), another mode, a variation of it, and every other time another palette */
        uint32_t n = VIS_AUTO_BARS[vis_set.autopilot % 5u], bar = vc0.beat_q8 >> 10;
        if (!n || !vcore_running || vis_style == VIS_SCREEN) {   /* (SCREEN: the display stays on) */
            vis_auto_last = 0xFFFFFFFFu;
        } else if (vis_auto_last == 0xFFFFFFFFu) {
            vis_auto_last = bar;                        /* (counting from here) */
        } else if (bar != vis_auto_last && bar % n == 0u) {
            uint32_t m;
            vis_auto_last = bar;
            do
                m = vis_rnd() % VIS_N;
            while (m == vis_style || m == 4u || m == VIS_SCREEN);   /* (not the same, not the meters, a picture) */
            vis_style = (uint8_t)m;
            vis_var = vis_rnd();
            if (++vis_auto_n & 1u) {
                uint32_t p;
                do
                    p = vis_rnd() % VIS_NPAL;
                while (p == vis_pal_live);
                vis_pal_live = (uint8_t)p;
            }
            vis_name_t = 30;
            vis_bars_due = 1;
        }
    }
#endif
    {   /* the attract mode */
        uint32_t secs = VIS_ATTRACT_S[vis_set.attract % 5u];
        if (vis_attract) {
            if (!vis_on)
                vis_attract = 0;
            else if (ui_input_ms != vis_attract_in)     /* a knob turned */
                vis_attract_close(0);
        } else if (secs && !vis_on && !ui.home && !ui.menu && cur_page()->scope == SC_TRK && !rec_wait && !ft_on &&
                   vis_style != VIS_SCREEN && fm1_ms - ui_input_ms >= secs * 1000u) {   /* (SCREEN streams the display:
                                                         * the visualiser's card would replace it) */
            vis_open();
            vis_attract = 1;
            vis_attract_in = ui_input_ms;
        }
    }
}

#if FELUCCA_VCORE
static void vis_pass_rows(const uint16_t *pal)          /* 45 screen rows from the 30 frame rows in vis_copy */
{
    uint32_t s, c;
    for (s = 0; s < 45u; s++) {
        const uint8_t *src = vis_copy + (s * 2u / 3u) * VF_W;
        uint16_t *d = cv_px + s * 240u;
        for (c = 0; c < 240u; c++)
            d[c] = pal[src[c * 2u / 3u]];
    }
}

static void vis_bars_draw(void)                         /* the top bar: the mode (or a message); the bottom: the knobs */
{
    char n[12], v[12];
    const char *s = ui.msg_t ? ui.msg : VIS_NAME[vis_style % VIS_N];
    uint32_t k;
    int32_t x = 6;
    cv_begin(240, VIS_Y, C_BLACK);
    cv_text(120 - text_w(&FONT_S, s) / 2, 20, &FONT_S, s, C_WHITE);
    fmt_int(n, (int32_t)(vis_style % VIS_N) + 1);
    str_cpy(n + str_len(n), "/", 2);
    fmt_int(n + str_len(n), (int32_t)VIS_N);
    cv_text(6, 20, &FONT_S, n, C_DIM);
    if (vis_set.autopilot && vis_style != VIS_SCREEN)
        cv_text(234 - text_w(&FONT_S, "AUTO"), 20, &FONT_S, "AUTO", C_AMB);
    cv_blit(0, 0);
    cv_begin(240, 240u - VIS_Y - VIS_PH, C_BLACK);
    for (k = 0; k < 4u; k++) {
        fmt_int(v, (int32_t)vis_knob[k] * 100 / 255);
        x = cv_text(x, 20, &FONT_S, VIS_KNOB_S[k], C_DIM);
        x = cv_text(x, 20, &FONT_S, v, C_WHITE) + 10;
    }
    cv_blit(0, VIS_Y + VIS_PH);
    vis_top_msg = ui.msg_t != 0u;
    vis_bars_due = 0;
}

/* SCREEN: the webcam shows this display; the picture's place says so (the name's colours turning) */
static void vis_screen_card(void)
{
    if (!vis_card) {
        vis_card = 1;
        cv_begin(240, VIS_PH - 48u, C_BLACK);
        cv_text(120 - text_w(&FONT_S, "THE WEBCAM SHOWS") / 2, 6, &FONT_S, "THE WEBCAM SHOWS", C_DIM);
        cv_text(120 - text_w(&FONT_S, "THIS DISPLAY") / 2, 26, &FONT_S, "THIS DISPLAY", C_WHITE);
        cv_text(120 - text_w(&FONT_S, "ANY PAGE STREAMS LIVE") / 2, 60, &FONT_S, "ANY PAGE STREAMS LIVE", C_AMB);
        cv_blit(0, VIS_Y + 48u);
    }
    if (!(ui.frame & 3u)) {
        cv_begin(240, 48, C_BLACK);
        rainbow_text(120 - text_w(&FONT_L, "SCREEN") / 2, 10, &FONT_L, "SCREEN", ui.frame * 2u, 30u);
        cv_blit(0, VIS_Y);
    }
}
#endif

static void vis_draw(void)
{
    if (!vis_shown_last || ui.force) {
        vis_shown_last = 1;
        lcd_fill(0, 0, 240, 240, C_BLACK);
#if FELUCCA_VCORE
        vis_frames_shown = 0;
        vis_bars_due = 1;
        vis_card = 0;
#endif
    }
#if FELUCCA_VCORE
    if (vis_bars_due || vis_name_t || vis_top_msg != (ui.msg_t != 0u))
        vis_bars_draw();
    vis_name_t = 0;
    if (vcore_running && (vis_style == VIS_SCREEN || vcore_mirroring())) {   /* SCREEN (or on the way out of it: the
                                                         * frames' RAM still holds the display's copy) */
        vis_screen_card();
        return;
    }
    if (vis_card) {                                     /* (back from SCREEN: the next frame paints over the card) */
        vis_card = 0;
        vis_frames_shown = 0;
    }
    fm1_c1_sync();
    if (vcore_running && vc1.frame_ready && vc1.frame_ready <= 2u) {
        uint32_t f = vc1.frames, pass, fr = vc1.frame_ready - 1u;
        const uint8_t *src = vf_px[fr];
        if (f == vis_frames_shown)
            return;                                     /* nothing new */
        vis_frames_shown = f;
        for (pass = 0; pass < 3u; pass++) {
            memcpy(vis_copy, src + pass * VIS_ROWS * VF_W, VIS_ROWS * VF_W);
            cv_begin(240, 45, C_BLACK);
            vis_pass_rows(vf_pal565[fr]);
            cv_blit(0, VIS_Y + pass * 45u);
        }
        return;
    }
#endif
    if (ui.frame & 1u)
        return;
    cv_begin(240, 120, C_BLACK);
    cv_text(120 - text_w(&FONT_S, "NO VIDEO CORE") / 2, 50, &FONT_S, "NO VIDEO CORE", C_DIM);
    cv_blit(0, 60);
}
