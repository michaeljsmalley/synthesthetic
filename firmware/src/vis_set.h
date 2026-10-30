/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthesthetic's visual settings and names, shared by the video core (vrender.c, vproc.c), the visualiser and
 * its controls (ui_vis.c, ui_menu.c), MIDI (seq.c), USB video controls (usb.c) and the settings record
 * (ui.c side record, project.c). Included early (felucca.c, before vcore.c).
 *
 * vis_set is what the user set: it is saved with the settings (the side record, ui.c side_fill / side_take)
 * when it changes (vis_dirty). The autopilot, MIDI and the camera's controls change the picture live without
 * touching it: vis_style (panel.c) is the mode shown, vis_pal_live the palette shown. */
#pragma once
#include <stdint.h>

#define VIS_N 31u                                       /* the modes */
static const char *const VIS_NAME[VIS_N] = {
    "WAVE", "BARS", "HEAT", "XY", "METERS", "RING", "SPIRAL", "GRID", "KIT", "TUNNEL", "MIRROR", "STARS",
    "SQUARES", "SHARDS", "ORBITS", "MOIRE",
    "WARP", "VORTEX", "RIPPLE", "KALEIDO", "MELT", "SMOKE", "WORMHOLE", "PLASMA", "FIRE", "RIDGES", "LAVA",
    "CELLS", "WIREFRAME", "FRESNEL", "SCREEN"};
#define VIS_FIRST_PROC 16u                              /* 16..29: the procedural modes (vproc.c), the intensity palette */
#define VIS_SCREEN 30u                                  /* not a picture: the webcam shows the display itself (mirror.c);
                                                         * the autopilot and the attract mode leave it alone */

#define VIS_NPAL 9u                                     /* the palettes (vproc.c VR_PAL_KEY) */
static const char *const VIS_PAL_NAME[VIS_NPAL] = {"SPECTRUM", "NEON", "FIRE", "OCEAN", "CRT", "VAPOR", "ACID",
                                                   "THERMAL", "MONO"};

#define VIS_TITLE_N 20u                                 /* the title card's text, NUL-padded (19 characters) */
#define VIS_SET_MAGIC 0x31534956u                       /* "VIS1" */
typedef struct {
    uint32_t magic;
    uint8_t mode;                                       /* the mode, 0 .. VIS_N - 1 */
    uint8_t palette;                                    /* 0 .. VIS_NPAL - 1 */
    uint8_t autopilot;                                  /* 0 off; 1..4: a new picture every 4, 8, 16, 32 bars */
    uint8_t attract;                                    /* 0 off; 1..4: the visualiser opens after 30 s, 1, 2, 5 min idle */
    uint8_t hud;                                        /* the overlay on the webcam: 0 off, 1 info, 2 title, 3 both */
    uint8_t midi_ch;                                    /* the visuals' MIDI channel: 0 off, 1..16 */
    uint8_t rsv[2];
    uint8_t knob[4];                                    /* AMOUNT, RATE, TRAILS, COLOR, 0..255 */
    char title[VIS_TITLE_N];
    uint32_t crc;                                       /* (st_crc32 of everything before it) */
} vis_set_t;
#define VIS_SET_INIT {VIS_SET_MAGIC, VIS_FIRST_PROC, 0, 2, 2, 0, 16, {0, 0}, {128, 128, 160, 0}, "SYNTHESTHETIC", 0}
static vis_set_t vis_set = VIS_SET_INIT;              /* (the defaults: project.c settings_defaults too) */
static uint8_t vis_dirty;                               /* vis_set changed since the settings were written */
static volatile uint8_t vis_pal_live;                   /* the palette shown (the autopilot's, MIDI's or vis_set's) */
static volatile uint32_t vis_var;                       /* the variation of the mode shown (the autopilot picks it) */
#define vis_knob vis_set.knob

/* MIDI (seq.c, in the render) and the camera's controls (usb.c, in the USB poll) ask; the main loop does it
 * (vcore.c vcore_poll): 0 nothing, else the value + 1 */
static volatile uint8_t vis_req_mode, vis_req_pal, vis_req_auto, vis_req_hud;
static volatile uint32_t vis_pulse;                     /* the visuals' MIDI channel's notes, counted (a beat for the picture) */
static volatile uint8_t vis_pulse_note;                 /* the last of them */
static volatile uint8_t vis_req_title;                  /* the title was set over SysEx (usb.c): keep it */
static volatile uint8_t vis_req_redraw;                 /* SCREEN's copy starts (vcore.c): the UI draws everything once */
static const uint8_t VIS_AUTO_BARS[5] = {0, 4, 8, 16, 32};

/* the visuals' MIDI channel (seq.c, in the render: only flags and bytes here): CC 20..23 AMOUNT, RATE, TRAILS,
 * COLOR; CC 24 the mode, 25 the palette, 26 the autopilot (0 off .. 127 32 bars), 27 the overlay; Program
 * Change n: mode n; a note: a beat for the picture (the shape modes and the warps take it as a kick). Changes
 * that come this way count as the panel's (ui_vis.c vis_tick: kept with the settings once quiet). 1: taken; 0:
 * not one of these (other CCs, the REMOTE's, pitch bend...: they go on as before) */
static int vis_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (st == 0xB0u && d1 >= 20u && d1 <= 27u) {
        if (d1 <= 23u)
            vis_set.knob[d1 - 20u] = (uint8_t)(d2 * 2u + (d2 >> 6));
        else if (d1 == 24u)
            vis_req_mode = (uint8_t)(d2 * VIS_N / 128u + 1u);
        else if (d1 == 25u)
            vis_req_pal = (uint8_t)(d2 * VIS_NPAL / 128u + 1u);
        else if (d1 == 26u)
            vis_req_auto = (uint8_t)(d2 * 5u / 128u + 1u);
        else
            vis_req_hud = (uint8_t)((d2 >> 5) + 1u);
        return 1;
    }
    if (st == 0xC0u) {
        vis_req_mode = (uint8_t)(d1 % VIS_N + 1u);
        return 1;
    }
    if (st == 0x90u || st == 0x80u) {
        if (st == 0x90u && d2) {
            vis_pulse_note = (uint8_t)d1;
            vis_pulse++;
        }
        return 1;
    }
    return 0;
}
static const uint16_t VIS_ATTRACT_S[5] = {0, 30, 60, 120, 300};
