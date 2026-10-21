/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthesthetic in a web page: the firmware compiled to WebAssembly (tools/build_web.py -> build/web/synthesthetic.wasm)
 * and run by web/play/worklet.js inside an AudioWorklet. As firmware/src/felucca.c is the firmware's one compilation
 * unit, this file is the browser's: browser doubles in place of the hal/ headers, then the same sources in order.
 * No firmware source changes for it. After sabliran/sloop-web-emu and hericdk/fm1-quest (quest/src/fm1quest_wasm.c),
 * both GPL-3.0, which did this for SLOOP first.
 *
 *   web_boot          the power-on (fm1_main up to its loop): settings and the project from the flash image, the boot
 *                     screen (recorded, web_frame plays it at its pace), the video core
 *   web_frame         one pass of fm1_main's loop: the master knob, the video core's keys, input, LEDs, the screen,
 *                     autosave; during the boot, the boot screen's next step
 *   web_render(n)     n frames of audio as the audio interrupt makes them (audio.c audio_block), float stereo
 *   web_cam           one frame of the video core's job (vcore.c vcore_job, without the JPEG): the webcam's picture
 *   web_input, web_enc, web_master, web_midi   the panel (debounced already), the knobs, a MIDI keyboard
 *   web_flash_*       the flash's RAM image (projects, settings, presets): the page keeps it in IndexedDB
 * The LCD is a 240 x 240 RGB565 framebuffer (web_lcd_ptr), the area written since the last look in web_lcd_dirty. */
#include <stdint.h>
#include <string.h>
#define WEB(name) __attribute__((export_name(#name), used))

/* ------------------------------------------------------------------------------------------------ libc */
#define memset felucca_memset                    /* (the toolchain's own are used: its declarations take size_t) */
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "../src/libc.c"
#undef memset
#undef memcpy
#undef memcmp

/* ------------------------------------------------------------------------- time (hal/fm1_time.h) */
#define FM1_TICKS_PER_US 24u
static uint32_t web_tick;                        /* TIMER4, 24 MHz: the audio rendered moves it (web_render), and every
                                                  * read a microsecond, so a loop that waits on it always ends */
static uint32_t fm1_ticks(void) { return web_tick += FM1_TICKS_PER_US; }
static uint32_t fm1_micros(void) { return fm1_ticks() / FM1_TICKS_PER_US; }
static void fm1_delay_ms(uint32_t ms) { (void)ms; }   /* never waits: the page's clock is the audio */
static void fm1_delay_us(uint32_t us) { (void)us; }

/* --------------------------------------------------------- interrupts, system (fm1_irq.h, fm1_sys.h) */
static void fm1_irq_off(void) {}                 /* (one thread: the worklet's) */
static void fm1_irq_on(void) {}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static void fm1_wdt_feed(void) {}
static void fm1_reboot(void) {}
static void fm1_enter_uboot(void) {}

/* ----------------------------------------------------------------------- input (hal/fm1_input.h) */
/* the matrix scan and its debounce are the page's (a key is down or up); the rest as the HAL */
#define FM1_NCOL 11u
#define FM1_NKEY 41u                             /* ids: 0..13 buttons, 14..40 note keys */
#define FM1_NENC 7u
#include "web_hal.h"                             /* FM1_KEYMAP and the flash map, from hal/fm1_input.h and
                                                  * hal/fm1_flash.h (tools/build_web.py) */
enum { FM1_BTN_OCT_DOWN = 0, FM1_BTN_OCT_UP = 1 };
static volatile struct {
    uint32_t notes;                              /* bit n = note key n (0 = F3 .. 26 = G5) */
    uint32_t buttons;                            /* bit i = button i (0..13) */
    uint32_t pressed, released;                  /* button edges since the last fm1_input_edges() */
    uint32_t notes_pressed;                      /* note-key press edges since the last fm1_input_note_edges() */
    int16_t enc_steps[FM1_NENC];                 /* + = clockwise */
} fm1_in;
static volatile uint32_t fm1_remote_btn;         /* (2.7: buttons held over MIDI, GLO > MIDI 3 > REMOTE) */
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_bg[FM1_NCOL];
static volatile uint16_t fm1_led_bg_ns;
static void fm1_input_init(void) {}
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = fm1_in.enc_steps[e];
    fm1_in.enc_steps[e] = 0;
    return s;
}
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t p = fm1_in.pressed;
    if (released)
        *released = fm1_in.released;
    fm1_in.pressed = fm1_in.released = 0;
    return p;
}
static uint32_t fm1_input_note_edges(void)
{
    uint32_t p = fm1_in.notes_pressed;
    fm1_in.notes_pressed = 0;
    return p;
}
static void fm1_led_key(uint32_t id, int on)     /* LED of key id (button 0..13 or note key 14..40) */
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                if (on)
                    fm1_led[p] |= (uint8_t)(1u << r);
                else
                    fm1_led[p] &= (uint8_t)~(1u << r);
            }
}

/* ---------------------------------------------------------------------------- ADC (hal/fm1_adc.h) */
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
static int32_t web_master_v = 820;               /* the MASTER knob, 0..1023 (the page's) */
static int32_t fm1_adc_read(int c) { return c == FM1_ADC_MASTER ? web_master_v : c == FM1_ADC_BATT ? 600 : -1; }
static void fm1_adc_init(void) {}

/* ------------------------------------------------------------------------- the second core (fm1_cpu1.h) */
static void fm1_c1_sync(void) {}

/* ---------------------------------------------------------------------------------- LCD (lcd.c) */
#define FELUCCA_CPU1 0                           /* the audio renders on one core here */
#define FELUCCA_UVC 0                            /* no USB: the page shows the webcam's picture itself (web_cam) */
#define FELUCCA_VCORE 1                          /* the video core's job, a frame a call (web_cam) */
static uint8_t *mir_px;                          /* SCREEN: the display's copy (mirror.c), as felucca.c */
static void mir_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c);
static void mir_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px);
static uint16_t web_lcd[240u * 240u];            /* the panel, RGB565 */
static uint32_t web_dirty[4] = {240u, 240u, 0u, 0u};   /* x0, y0, x1, y1 (exclusive) written since web_lcd_dirty */
static uint32_t fm1_lcd_timeouts;
static void web_lcd_mark(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (x < web_dirty[0]) web_dirty[0] = x;
    if (y < web_dirty[1]) web_dirty[1] = y;
    if (x + w > web_dirty[2]) web_dirty[2] = x + w;
    if (y + h > web_dirty[3]) web_dirty[3] = y + h;
}
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    if (!w || !h || x >= 240u || y >= 240u)
        return;
    if (x + w > 240u)
        w = 240u - x;
    if (y + h > 240u)
        h = 240u - y;
    if (mir_px)
        mir_fill(x, y, w, h, c);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            web_lcd[(y + j) * 240u + x + i] = c;
    web_lcd_mark(x, y, w, h);
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)   /* px: byte-swapped */
{
    uint32_t i, j;
    if (!w || !h || x >= 240u || y >= 240u || x + w > 240u || y + h > 240u)
        return;
    if (mir_px)
        mir_blit(x, y, w, h, px);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            uint16_t v = px[j * w + i];
            web_lcd[(y + j) * 240u + x + i] = (uint16_t)(v >> 8 | v << 8);
        }
    web_lcd_mark(x, y, w, h);
}
static void lcd_init(void) { lcd_fill(0, 0, 240, 240, 0); }

/* the boot screen is drawn in one go here (fm1_delay_ms never waits): each step's change is kept with its time
 * (splash.c SPLASH_STEP), and web_frame shows them at that pace */
#define WEB_FILM_N (3u << 20)
static uint8_t web_film[WEB_FILM_N];
static uint32_t web_film_len, web_film_pos, web_film_due, web_film_full;
static void web_film_step(uint32_t ms)
{
    uint32_t x0 = web_dirty[0], y0 = web_dirty[1], x1 = web_dirty[2], y1 = web_dirty[3], y, n;
    if (x1 <= x0 || y1 <= y0) {
        x0 = y0 = 0;
        x1 = y1 = 1;
    }
    n = 8u + (x1 - x0) * (y1 - y0) * 2u;
    if (web_film_full || web_film_len + n > WEB_FILM_N) {
        web_film_full = 1;                       /* (no room: the rest shows at once) */
        return;
    }
    web_film[web_film_len++] = (uint8_t)x0;
    web_film[web_film_len++] = (uint8_t)y0;
    web_film[web_film_len++] = (uint8_t)(x1 - x0 - 1u);
    web_film[web_film_len++] = (uint8_t)(y1 - y0 - 1u);
    web_film[web_film_len++] = (uint8_t)ms;
    web_film[web_film_len++] = (uint8_t)(ms >> 8);
    web_film[web_film_len++] = 0;
    web_film[web_film_len++] = 0;
    for (y = y0; y < y1; y++) {
        memcpy(web_film + web_film_len, &web_lcd[y * 240u + x0], (x1 - x0) * 2u);
        web_film_len += (x1 - x0) * 2u;
    }
    web_dirty[0] = web_dirty[1] = 240u;
    web_dirty[2] = web_dirty[3] = 0;
}
#define SPLASH_STEP(ms) web_film_step(ms)

/* ------------------------------------------------------------------------- the firmware's sources */
#include "felucca_tables.h"
#include "../src/gfx.c"
#include "../src/rainbow.c"
#include "../src/mirror.c"
#include "../src/core.h"
#include "../src/cpu1.h"
static void vcore_job(uint32_t arg);
static int cpu1_run(cpu1_job_t fn, uint32_t arg) { (void)arg; return fn == vcore_job; }   /* the video core's job
                                                  * "runs" (web_cam is its loop); any other: run it yourself */
static int cpu1_wait(void) { return 0; }
static int cpu1_on(void) { return !cpu1_taken; }
static uint32_t cpu1_ticks(void) { return 0; }
/* the flash's RAM image: the store's offsets as on the FM-1 (0x90000..0xFFFFF: projects, autosave, user presets and
 * samples, the FM6 bank, settings); eng_sample.c and fm6_bank.c read it where it is, as they read the XIP window */
#define WEB_FLASH_BASE 0x90000u
#define WEB_FLASH_SIZE 0x70000u
static uint8_t web_flash[WEB_FLASH_SIZE];
static uint32_t web_flash_gen;                   /* every erase or program: the page saves the image */
#define WEB_XIP(off) ((const uint8_t *)web_flash + ((uint32_t)(off) - WEB_FLASH_BASE))
#define SMP_USER_XIP(k) WEB_XIP(SMP_USER_OFF((uint32_t)(k)))
#define FM6_BANK_XIP(copy) WEB_XIP(st_sector(OBJ_FM6BANK, copy) + ST_PAYLOAD_OFF)
#define ST_XIP_PTR(off) WEB_XIP(off)
#include "../src/engines.c"
#include "../src/drums.c"
#include "../src/params.c"
#include "../src/voice.c"
#include "../src/slicer.c"
#include "../src/fx.c"
#define FELUCCA_OTA 0
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0
#include "../src/vis_set.h"
#include "../src/vjpeg.c"
#include "../src/vcore.c"
#include "../src/usb.c"
#define FELUCCA_UART 1
#include "../src/midi_uart.c"
#define FELUCCA_ARRANGER 1
#include "../src/arranger.c"
#include "../src/seq.c"
#define FM1_AUDIO_HALF 1u                        /* (audio.c's interrupt glue: compiled, never run) */
static void fm1_audio_init(int32_t *b, uint32_t n, void (*isr)(void), uint32_t prio) { (void)b, (void)n, (void)isr, (void)prio; }
static uint8_t fm1_audio_pending(void) { return 0; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return 0; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_stop(void) {}
void isr_alnk0(void) {}
#include "../src/audio.c"
#include "../src/panel.c"
#include "../src/ui.c"
#include "../src/ui_song.c"
#include "../src/ui_studio.c"
#include "../src/icons.c"
#include "../src/ui_draw.c"
#include "../src/ui_vis.c"
#include "../src/vrender.c"
#include "../src/ui_layers.c"
#include "../src/ui_menu.c"
#include "../src/ui_input.c"
#define FELUCCA_FLASH 1
static uint8_t flash_ok = 1;
#define FL_FAR(f) f                              /* (fm1_flash.h: the RAM driver's calls) */
static uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* the FM-1's part: persist_boot keeps flash_ok */
static void fl_plain_window_init(void) {}
static int web_flash_in(uint32_t off, uint32_t n)
{
    return off >= WEB_FLASH_BASE && n <= WEB_FLASH_SIZE && off - WEB_FLASH_BASE <= WEB_FLASH_SIZE - n;
}
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (!web_flash_in(off, n))
        return -8;
    memcpy(dst, web_flash + (off - WEB_FLASH_BASE), n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (!FL_STORE_OK(off, 0x1000u) || !web_flash_in(off, 0x1000u))
        return -8;
    memset(web_flash + (off - WEB_FLASH_BASE), 0xFF, 0x1000u);
    web_flash_gen++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)   /* NOR: a program only clears bits */
{
    uint8_t *d;
    const uint8_t *s = src;
    if (!FL_STORE_OK(off, n) || !web_flash_in(off, n))
        return -8;
    for (d = web_flash + (off - WEB_FLASH_BASE); n; n--)
        *d++ &= *s++;
    web_flash_gen++;
    return 0;
}
#include "../src/storage.c"
#include "../src/upreset.c"
#include "../src/project.c"
#include "../src/splash.c"

/* -------------------------------------------------------------------------------------- the page's */
static uint8_t web_phase;                        /* 0 before web_boot, 1 the boot screen, 2 the logo's tick, 3 running */
static uint32_t web_ph;                          /* the logo's tick: 0..520 as main.c turns it */
static int32_t web_knob = 512 * 16;              /* the MASTER knob, smoothed as main.c does */

/* power-on: three parts with their default sounds, the drum track, empty patterns (main.c felucca_init) */
static void web_felucca_init(void)
{
    uint32_t i;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    fm6_init();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        if (i < NPART) {
            set_engine_of(t, TRK_DEF[i][0]);
            apply_preset_to(t, TRK_DEF[i][1]);
            t->engine = t->eng_req;
        }
        track_defaults_steps(t);
    }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    song.sel = 0;
    song.master_q12 = 2048;
    autosave_resume();
    song.g[G_SYNC] = (int16_t)lights_sync;
    song.g[G_MIDI] = (int16_t)lights_mout;
    song.g[G_ROUTE] = (int16_t)lights_min;
    song.g[G_CH1] = midi_set[0], song.g[G_CH2] = midi_set[1], song.g[G_CH3] = midi_set[2];
    song.g[G_DRCH] = midi_set[3], song.g[G_CHOTH] = midi_set[4], song.g[G_BEND] = midi_set[5];
    song.g[G_FXCH] = midi_set[6];
    song.g[G_DRMAP] = midi_set[7];
    song.g[G_CLKOUT] = midi_set[9];
    song.g[G_RMCH] = midi_set[8];
    layers_init();
    go_home();
    ui.force = 1;
}

/* a first visit: a song to press PLAY on, in the FM-1's song mode: four sections (project slots, as SAVE + a key
 * stores them) in an order that loops: INTRO 4 bars, VERSE 8, CHORUS 8, VERSE 8, BRIDGE 4, CHORUS 8, about 79 s of
 * deep house at 122 BPM in A minor, a little swing and the kick's duck throughout. STOP brings the verse back as the
 * loop to play with; SAVE held shows the song.
 *   INTRO (D)  the drums come in, the piano's Am9 under a low-pass, a noise riser into the verse
 *   VERSE (A)  FUNK BASS (a syncopated line, octaves sliding), M1 PIANO stabs over Am9 | Fmaj9 | Dm9 | E7sus4 -> E7b9,
 *              the vibes answering every other bar
 *   CHORUS (B) the lift: Fmaj9 | G6/9 | Em9 | Am9, the bass in disco octaves, open hats, the VIBES hook with the stabs
 *   BRIDGE (C) the breakdown: Dm9 | Em9 | Fmaj9 | E7, a WARM PAD and a SUB BASS held, the vibes slow, no kick for two
 *              bars, then the build: snares in eighths, then sixteenths rolling into the chorus (E7 -> F: the deceptive
 *              lift)
 * Rootless voicings round middle C, one note moving at a time. mask: the tracks written, bit k track k (the page asks
 * for all of them; the tests solo each) */
static void web_sound(uint32_t k, uint32_t engine, uint32_t preset)
{
    track_t *t = &trk[k];
    set_engine_of(t, engine);
    apply_preset_to(t, preset);
    t->engine = t->eng_req;
}
static void web_note(track_t *t, uint32_t i, uint32_t n, const uint8_t *notes, uint32_t len, uint32_t flags, uint32_t vel)
{
    step_t *s = &t->step[i];
    uint32_t k;
    s->n = (uint8_t)n;
    for (k = 0; k < 4u; k++)
        s->note[k] = k < n ? notes[k] : 0;
    s->time = ST_NOTE;
    s->flags = (uint8_t)flags;
    s->vel = (uint8_t)vel;
    s->lvl = s->rat = 0;
    for (k = 1; k < len && i + k < NSTEP; k++) {      /* held on: ties */
        t->step[i + k] = *s;
        t->step[i + k].time = ST_TIE;
    }
}
static void web_hit(uint32_t bar, uint32_t i, uint32_t note, uint32_t lvl, uint32_t rat)
{
    dstep_set(&TDRUM->dstep[bar * 16u + i], lane_of_note(note), lvl, rat);
}

enum { CH_AM9, CH_FM9, CH_DM9, CH_E7S4, CH_E7B9, CH_G69, CH_EM9 };
static const uint8_t WEB_CH[7][4] = {{60, 64, 67, 71}, {60, 64, 67, 69}, {60, 64, 65, 69}, {59, 62, 64, 69},
                                     {59, 62, 65, 68}, {59, 62, 64, 69}, {62, 66, 67, 71}};
/* the stabs: on the one, then the 3-3-4-3 that pulls against the kick; (step, steps held, velocity) */
static const uint8_t WEB_STAB[5][3] = {{0, 2, 104}, {3, 1, 86}, {6, 2, 96}, {10, 1, 84}, {13, 2, 92}};
typedef struct { uint8_t at, note, len, vel; } web_tone_t;

/* a section begins: every track empty, its sounds and mix */
static void web_section_sounds(uint32_t bass, uint32_t keys_e, uint32_t keys_p, uint32_t lead_e, uint32_t lead_p)
{
    uint32_t i;
    web_sound(0, 0, bass);
    web_sound(1, keys_e, keys_p);
    web_sound(2, lead_e, lead_p);
    TDRUM->p[P_E0] = 15;                               /* the HOUSE kit (drums.c DRUM_KIT_NAMES) */
    for (i = 0; i < NTRK; i++) {
        steps_clear(&trk[i]);
        memset(trk[i].micro, 0, sizeof trk[i].micro);
        trk[i].p[P_SLEN] = 64;
    }
    /* the mix (P_LEVEL: 112 is 0 dB, a step 1/2 dB), measured by rendering each part alone: the bass a little under
     * the kick, the keys and the vibes 2 to 3 dB under them; the keys high-passed (~200 Hz: the low mids are the bass's) */
    trk[0].p[P_SGATE] = 80;
    trk[0].p[P_LEVEL] = 100;
    trk[1].p[P_SGATE] = 46;
    trk[1].p[P_LEVEL] = 94;
    trk[1].p[P_TFLT] = 28;
    trk[1].p[P_REV] = 34;
    trk[1].p[P_CHOR] = 20;
    trk[1].p[P_PAN] = -8;
    trk[2].p[P_SGATE] = 90;
    trk[2].p[P_LEVEL] = 96;
    trk[2].p[P_TFLT] = 20;
    trk[2].p[P_DLY] = 42;
    trk[2].p[P_REV] = 36;
    trk[2].p[P_PAN] = 10;
}
static void web_stabs(uint32_t bar, uint32_t chord, uint32_t last)   /* last: the chord of the bar's last stab */
{
    uint32_t i;
    for (i = 0; i < 5u; i++)
        web_note(&trk[1], bar * 16u + WEB_STAB[i][0], 4, WEB_CH[i == 4u ? last : chord], WEB_STAB[i][1],
                 i == 0u ? SF_ACCENT : 0u, WEB_STAB[i][2]);
}
static void web_line(uint32_t bar, const web_tone_t *t, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && t[i].len; i++)
        web_note(&trk[2], bar * 16u + t[i].at, 1, &t[i].note, t[i].len, 0, t[i].vel);
}
/* the groove under the verse and the chorus: four on the floor, claps on 2 and 4 (a hair late), the hats on the
 * offbeats (open: every other bar; the chorus: all), a shaker between, a rim moving, congas in bar 3 */
static void web_groove(uint32_t open_all)
{
    uint32_t b, i;
    for (b = 0; b < 4u; b++) {
        for (i = 0; i < 16u; i += 4u)
            web_hit(b, i, 36, b == 0u && i == 0u ? LV_HARD : LV_NORM, 0);
        web_hit(b, 4, 39, LV_NORM, 0);
        web_hit(b, 12, 39, LV_NORM, 0);
        TDRUM->micro[b * 16u + 4u] = TDRUM->micro[b * 16u + 12u] = 3;
        for (i = 2; i < 16u; i += 4u)
            web_hit(b, i, open_all || ((b & 1u) && i == 14u) ? 46 : 42, LV_NORM, 0);
        for (i = 1; i < 16u; i += 2u)
            web_hit(b, i, 70, i % 4u == 3u ? LV_SOFT : LV_GHOST, 0);
        web_hit(b, (b & 1u) ? 7u : 3u, 37, LV_SOFT, 0);
        web_hit(b, (b & 1u) ? 10u : 11u, 37, LV_GHOST, 0);
    }
    web_hit(2, 9, 63, LV_SOFT, 0);
    web_hit(2, 14, 63, LV_NORM, 0);
    web_hit(0, 0, 49, LV_NORM, 0);                     /* the crash on the one */
    web_hit(1, 12, 38, LV_SOFT, 0);                    /* a snare under the fourth clap */
    web_hit(3, 12, 38, LV_SOFT, 0);
    web_hit(3, 13, 38, LV_GHOST, 1);                   /* the roll into the top */
    web_hit(3, 14, 38, LV_SOFT, 2);
    web_hit(3, 15, 38, LV_NORM, 3);
}

static void web_demo(uint32_t mask)
{
    /* VERSE: the bass, root root octave(slide) root fifth root and a note into the next bar's root */
    static const uint8_t VB_AT[7] = {0, 3, 6, 8, 11, 13, 14};
    static const uint8_t VB[4][7] = {{45, 45, 57, 45, 52, 45, 55}, {41, 41, 53, 41, 48, 41, 52},
                                     {38, 38, 50, 38, 45, 38, 41}, {40, 40, 52, 40, 47, 40, 44}};
    static const uint8_t V_CH[4] = {CH_AM9, CH_FM9, CH_DM9, CH_E7S4};
    static const web_tone_t V_ANSWER[2][4] = {{{8, 79, 2, 88}, {10, 76, 2, 84}, {12, 72, 4, 92}},            /* over F */
                                              {{8, 76, 2, 90}, {10, 74, 2, 84}, {12, 71, 2, 88}, {14, 68, 2, 92}}};   /* E */
    /* CHORUS: Fmaj9 G6/9 Em9 Am9 over F G E A; the hook (riding the stabs' rhythm) */
    static const uint8_t C_CH[4] = {CH_FM9, CH_G69, CH_EM9, CH_AM9};
    static const uint8_t C_ROOT[4] = {41, 43, 40, 45};
    static const web_tone_t HOOK[4][5] = {
        {{0, 69, 3, 100}, {3, 72, 3, 90}, {6, 76, 4, 104}, {10, 74, 2, 86}, {12, 72, 4, 94}},
        {{0, 71, 3, 100}, {3, 74, 3, 90}, {6, 76, 4, 104}, {10, 74, 2, 86}, {12, 71, 4, 94}},
        {{0, 71, 3, 100}, {3, 74, 3, 90}, {6, 79, 4, 108}, {10, 78, 2, 88}, {12, 76, 4, 96}},
        {{0, 76, 3, 104}, {3, 72, 3, 92}, {6, 71, 4, 98}, {10, 69, 6, 100}}};
    /* BRIDGE: Dm9 Em9 Fmaj9 E7 held; the vibes slow, falling then turning up */
    static const uint8_t B_CH[4] = {CH_DM9, CH_EM9, CH_FM9, CH_E7S4};
    static const uint8_t B_ROOT[4] = {38, 40, 41, 40};
    static const web_tone_t B_LINE[4][3] = {{{0, 77, 6, 92}, {8, 76, 8, 86}}, {{0, 74, 6, 92}, {8, 71, 8, 86}},
                                            {{0, 72, 6, 92}, {8, 76, 8, 90}}, {{0, 74, 4, 94}, {4, 71, 4, 88}, {8, 68, 8, 96}}};
    uint32_t b, i;
    song.g[G_BPM] = 122;
    song.g[G_SWING] = 22;                              /* (about 55 %: the 16ths lean, the offbeats bounce) */
    song.g[G_DUCK] = 34;                               /* every kick dips the synths a little: the pump */

    /* ---- INTRO (section D): the drums come in, the piano muffled, a riser */
    web_section_sounds(8, 1, 4, 11, 7);                /* FUNK BASS (silent here), M1 PIANO, NOISE RISER */
    trk[1].p[P_TFLT] = -34;                            /* the piano under a low-pass (the verse opens it) */
    trk[2].p[P_LEVEL] = 100;
    trk[2].p[P_TFLT] = 0;
    trk[2].p[P_DLY] = 0;
    if (mask & 2u)
        for (b = 0; b < 4u; b++)
            web_stabs(b, CH_AM9, CH_AM9);
    if (mask & 4u) {                                   /* the riser: held through the last bar, its FREQ locked a */
        web_note(&trk[2], 48, 1, (const uint8_t[]){60}, 16, 0, 100);   /* little higher on every step: a sweep up */
        for (i = 0; i < 16u; i++)
            lock_set(&trk[2], 48u + i, P_E0 + 2u, 24 + (int32_t)(i * i * 103u / 225u));
    }
    if (mask & 8u)
        for (b = 0; b < 4u; b++) {
            for (i = 0; i < 16u; i += 4u)
                web_hit(b, i, 36, LV_NORM, 0);
            for (i = 1; i < 16u; i += 2u)
                web_hit(b, i, 70, i % 4u == 3u ? LV_SOFT : LV_GHOST, 0);
            if (b)
                for (i = 2; i < 16u; i += 4u)
                    web_hit(b, i, 42, b == 1u ? LV_SOFT : LV_NORM, 0);
            web_hit(b, 3, 37, LV_SOFT, 0);
            web_hit(b, 11, 37, LV_GHOST, 0);
            if (b == 3u) {
                web_hit(b, 4, 39, LV_NORM, 0);
                web_hit(b, 12, 39, LV_NORM, 0);
            }
        }
    section_store(3);

    /* ---- VERSE (section A) */
    web_section_sounds(8, 1, 4, 4, 1);                 /* FUNK BASS, M1 PIANO, VIBES */
    trk[2].p[P_LEVEL] = 92;                            /* (the vibes answer here: under the stabs) */
    for (b = 0; b < 4u; b++) {
        if (mask & 1u)
            for (i = 0; i < 7u; i++)
                web_note(&trk[0], b * 16u + VB_AT[i], 1, &VB[b][i], i == 5u ? 1u : 2u,
                         (i == 0u || i == 2u ? SF_ACCENT : 0u) | (i == 2u || (i == 6u && b == 3u) ? SF_SLIDE : 0u), i == 0u ? 112 : 96);
        if (mask & 2u)
            web_stabs(b, V_CH[b], b == 3u ? CH_E7B9 : V_CH[b]);
    }
    if (mask & 4u) {
        web_line(1, V_ANSWER[0], 4);
        web_line(3, V_ANSWER[1], 4);
    }
    if (mask & 8u)
        web_groove(0);
    section_store(0);

    /* ---- CHORUS (section B) */
    web_section_sounds(8, 1, 4, 4, 1);
    trk[0].p[P_LEVEL] = 96;                            /* (octaves on every eighth: more of it, so a little lower) */
    trk[2].p[P_LEVEL] = 98;                            /* (the hook: up front) */
    for (b = 0; b < 4u; b++) {
        if (mask & 1u)
            for (i = 0; i < 16u; i += 2u) {            /* disco octaves on the eighths */
                uint8_t n = (uint8_t)(C_ROOT[b] + ((i & 2u) ? 12u : 0u));
                web_note(&trk[0], b * 16u + i, 1, &n, 2, (i % 8u) == 0u ? SF_ACCENT : 0u, (i & 2u) ? 92 : 108);
            }
        if (mask & 2u)
            web_stabs(b, C_CH[b], C_CH[b]);
        if (mask & 4u)
            web_line(b, HOOK[b], 5);
    }
    if (mask & 8u) {
        web_groove(1);
        for (b = 0; b < 4u; b++)
            web_hit(b, 12, 38, LV_SOFT, 0);            /* (the snare under every fourth clap: the chorus lifts) */
    }
    section_store(1);

    /* ---- BRIDGE (section C): the breakdown and the build */
    web_section_sounds(2, 0, 13, 4, 1);                /* SUB BASS, WARM PAD, VIBES */
    trk[0].p[P_SGATE] = 100;                           /* (held notes fill every moment: lower, so the bridge dips) */
    trk[0].p[P_LEVEL] = 92;
    trk[1].p[P_SGATE] = 100;                           /* (the pad held through each bar) */
    trk[1].p[P_LEVEL] = 90;
    trk[1].p[P_TFLT] = 24;
    trk[1].p[P_REV] = 56;
    trk[1].p[P_CHOR] = 30;
    trk[2].p[P_REV] = 50;
    for (b = 0; b < 4u; b++) {
        if (mask & 1u)
            web_note(&trk[0], b * 16u, 1, &B_ROOT[b], 16, 0, 100);
        if (mask & 2u) {
            if (b == 3u) {                             /* E7sus4, then E7b9 for the turn */
                web_note(&trk[1], 48, 4, WEB_CH[CH_E7S4], 8, 0, 96);
                web_note(&trk[1], 56, 4, WEB_CH[CH_E7B9], 8, SF_ACCENT, 100);
            } else
                web_note(&trk[1], b * 16u, 4, WEB_CH[B_CH[b]], 16, 0, 96);
        }
        if (mask & 4u)
            web_line(b, B_LINE[b], 3);
    }
    if (mask & 8u) {
        for (b = 0; b < 4u; b++) {
            for (i = 1; i < 16u; i += 2u)
                web_hit(b, i, 70, i % 4u == 3u ? LV_SOFT : LV_GHOST, 0);
            for (i = 2; i < 16u; i += 4u)
                web_hit(b, i, 42, LV_SOFT, 0);
            web_hit(b, 3, 37, LV_GHOST, 0);
            web_hit(b, 6, 63, LV_SOFT, 0);
            web_hit(b, 14, 63, LV_SOFT, 0);
        }
        for (i = 0; i < 16u; i += 4u) {                /* the build: the kick back, snares in eighths, then sixteenths */
            web_hit(2, i, 36, LV_SOFT, 0);
            web_hit(3, i, 36, LV_NORM, 0);
        }
        for (i = 0; i < 16u; i += 2u)
            web_hit(2, i, 38, i < 8u ? LV_GHOST : LV_SOFT, 0);
        for (i = 0; i < 16u; i++)
            web_hit(3, i, 38, i < 4u ? LV_SOFT : i < 12u ? LV_NORM : LV_HARD, i >= 12u ? 1u : 0u);
    }
    section_store(2);

    /* ---- the song, in song mode; the verse is the loop on the tracks (what STOP brings back) */
    {
        static const arr_entry_t ORDER[6] = {{3, 4}, {0, 8}, {1, 8}, {0, 8}, {2, 4}, {1, 8}};   /* (the bridge once: its
                                                         * build leads straight into the last chorus) */
        arrangement.count = 6;
        arrangement.loop = 1;
        for (i = 0; i < 6u; i++)
            arrangement.entry[i] = ORDER[i];
        arrangement_enabled = 1;
        song_dirty = 1;                                /* (kept with the settings: sections_write) */
    }
    section_load(0);
}

WEB(web_flash_ptr) uint32_t web_flash_ptr(void) { return (uint32_t)(uintptr_t)web_flash; }
WEB(web_flash_size) uint32_t web_flash_size(void) { return WEB_FLASH_SIZE; }
WEB(web_flash_gen) uint32_t web_flash_gen_get(void) { return web_flash_gen; }
WEB(web_flash_erase) void web_flash_erase(void) { memset(web_flash, 0xFF, sizeof web_flash); }   /* a new FM-1 */

/* fm1_main up to its loop; the page has put its saved flash image in place (or erased it: demo != 0 writes the song,
 * its bits the tracks: 15 all of them) */
WEB(web_boot) void web_boot(uint32_t demo)
{
    memset((void *)&felucca_dbg, 0, sizeof felucca_dbg);
    felucca_dbg.magic = DBG_MAGIC;
    persist_boot();
    if (vis_set.crc == 0u) {                     /* no visual settings stored yet: the defaults (vis_set.h) */
        vis_style = (uint8_t)(vis_set.mode % VIS_N);
        vis_pal_live = (uint8_t)(vis_set.palette % VIS_NPAL);
    }
    settings_init();
    lcd_init();
    web_dirty[0] = web_dirty[1] = 240u;          /* (the film starts from black) */
    web_dirty[2] = web_dirty[3] = 0;
    synthesthetic_splash();
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    web_felucca_init();
    if (demo) {
        web_demo(demo);
        persist_flush_now();                     /* the song, its sections and the verse as the loop: in the flash image
                                                  * now (once PLAY runs the song, autosave leaves the tracks alone) */
    }
    vcore_start();
    memset(web_lcd, 0, sizeof web_lcd);          /* the boot screen again, at its pace (web_frame) */
    web_lcd_mark(0, 0, 240, 240);
    web_film_pos = 0;
    web_film_due = fm1_ms;
    web_phase = 1;
}

/* the boot screen: the recorded steps at their pace, then the logo stays about 2 s (main.c) */
static void web_boot_frame(void)
{
    if (web_phase == 1u) {
        while (web_film_pos < web_film_len && (int32_t)(fm1_ms - web_film_due) >= 0) {
            const uint8_t *h = web_film + web_film_pos;
            uint32_t x0 = h[0], y0 = h[1], w = h[2] + 1u, hh = h[3] + 1u, ms = h[4] | (uint32_t)h[5] << 8, y;
            web_film_pos += 8u;
            for (y = 0; y < hh; y++) {
                memcpy(&web_lcd[(y0 + y) * 240u + x0], web_film + web_film_pos, w * 2u);
                web_film_pos += w * 2u;
            }
            web_lcd_mark(x0, y0, w, hh);
            web_film_due += ms;
        }
        if (web_film_pos >= web_film_len && (int32_t)(fm1_ms - web_film_due) >= 0) {
            web_phase = 2;
            web_ph = 0;
        }
        return;
    }
    synthesthetic_splash_tick(web_ph);
    web_ph += 13u * 16u / 12u;                   /* (main.c: 13 a step, a step every 12 ms; a frame here ~16 ms) */
    if (web_ph >= 520u) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
        web_phase = 3;
    }
}

/* one pass of fm1_main's loop (the page calls it about every 16 ms of audio) */
WEB(web_frame) void web_frame(void)
{
    vcore_poll();                                /* the keys to the video core */
    {
        int32_t b = fm1_adc_read(FM1_ADC_BATT);
        song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
    }
    {
        uint32_t k10;
        web_knob += (fm1_adc_read(FM1_ADC_MASTER) * 16 - web_knob) / 8;
        k10 = (uint32_t)(web_knob / 16);
        song.master_q12 = (k10 * k10) >> 8;
    }
    if (web_phase < 3u) {
        if (web_phase)
            web_boot_frame();
        return;
    }
    felucca_dbg.ui_frames++;
    ui_input();
    ui_leds();
    ui_draw();
    autosave_tick();
    sections_flush();
}

/* audio: n frames (at most 512) as the interrupt renders them, Q15 -> float */
static float web_audio[2u * 512u];
static int32_t web_block[2u * CTL];
static uint32_t web_ms_acc, web_tick_acc;
WEB(web_render) uint32_t web_render(uint32_t n)
{
    uint32_t k, i;
    if (n > 512u)
        n = 512u;
    n -= n % CTL;
    for (k = 0; k < n; k += CTL) {
        felucca_dbg.in_audio = 1;
        audio_block(web_block, CTL);
        felucca_dbg.in_audio = 0;
        for (i = 0; i < 2u * CTL; i++)
            web_audio[2u * k + i] = (float)web_block[i] * (1.0f / (float)(32768 << OUT_SHIFT));
    }
    web_ms_acc += n * 1000u;
    fm1_ms += web_ms_acc / FS;
    web_ms_acc %= FS;
    web_tick_acc += n * 24000000u % FS;          /* (24 MHz: 544.2 ticks a frame) */
    web_tick += n * (24000000u / FS) + web_tick_acc / FS;
    web_tick_acc %= FS;
    return n;
}
WEB(web_audio_ptr) uint32_t web_audio_ptr(void) { return (uint32_t)(uintptr_t)web_audio; }

/* the webcam: one frame of the video core's job (vcore.c vcore_job, the JPEG aside) -> web_cam_px with its palette in
 * web_cam_rgb; 0 = the picture (160 x 90, with the overlay, as the webcam sends it), 1 = SCREEN (the display's copy,
 * 180 x 180, placed at 72 in 320 x 180) */
static uint8_t web_cam_px[MIR_N > VF_N ? MIR_N : VF_N];
static uint8_t web_cam_rgb[2][256][3];
static uint32_t web_cam_fi, web_cam_cur;
WEB(web_cam) uint32_t web_cam(void)
{
    uint32_t fi = web_cam_fi, i;
    int screen;
    if (!vcore_running)
        return 2;
    vr_rgb_out = web_cam_rgb[fi];
    screen = vrender_frame(vf_px[fi], vf_px[fi ^ 1u], vf_lut[fi], vf_pal565[fi]);
    web_cam_cur = fi;
    if (screen) {
        for (i = 0; i < 256u; i++) {             /* (vr_screen_lut: MIR_RGB at vr_bright) */
            web_cam_rgb[fi][i][0] = (uint8_t)(MIR_RGB[i][0] * vr_bright / 256u);
            web_cam_rgb[fi][i][1] = (uint8_t)(MIR_RGB[i][1] * vr_bright / 256u);
            web_cam_rgb[fi][i][2] = (uint8_t)(MIR_RGB[i][2] * vr_bright / 256u);
        }
        if (mir_px)
            memcpy(web_cam_px, vf_mem, MIR_N);
        else
            memset(web_cam_px, 0, MIR_N);
    } else {
        vr_overlay(vf_px[fi]);                   /* the overlay: the webcam's only */
        memcpy(web_cam_px, vf_px[fi], VF_N);
        vr_overlay_restore(vf_px[fi]);
    }
    vc1.frame_ready = fi + 1u;
    vc1.frames++;
    web_cam_fi ^= 1u;
    return (uint32_t)screen;
}
WEB(web_cam_ptr) uint32_t web_cam_ptr(void) { return (uint32_t)(uintptr_t)web_cam_px; }
WEB(web_cam_rgb_ptr) uint32_t web_cam_rgb_ptr(void) { return (uint32_t)(uintptr_t)web_cam_rgb[web_cam_cur]; }

/* the panel: the keys and buttons held (the page debounces nothing: a press is a press) */
WEB(web_input) void web_input(uint32_t notes, uint32_t buttons)
{
    uint32_t pn = fm1_in.notes, pb = fm1_in.buttons;
    notes &= 0x7FFFFFFu;
    buttons &= 0x3FFFu;
    fm1_in.notes = notes;
    fm1_in.buttons = buttons;
    fm1_in.notes_pressed |= notes & ~pn;
    fm1_in.pressed |= buttons & ~pb;
    fm1_in.released |= pb & ~buttons;
}
WEB(web_enc) void web_enc(uint32_t e, int32_t steps)   /* matrix encoder e (panel.c: SELECT 0, ALGORITHM 1, KNOB 1..4
                                                         * 2..5, PRESETS 6), steps + = clockwise */
{
    if (e < FM1_NENC)
        fm1_in.enc_steps[e] = (int16_t)(fm1_in.enc_steps[e] + steps);
}
WEB(web_master) void web_master(int32_t v) { web_master_v = v < 0 ? 0 : v > 1023 ? 1023 : v; }
WEB(web_midi) void web_midi(uint32_t pkt)        /* a USB-MIDI event packet (cable 0), as usb.c queues them */
{
    midi_in_q[mi_w++ % MQ] = pkt;
}

/* the screen: the framebuffer, and the area written since the last call (x0 | y0 << 8 | w - 1 << 16 | h - 1 << 24;
 * 0xFFFFFFFF: nothing) */
WEB(web_lcd_ptr) uint32_t web_lcd_ptr(void) { return (uint32_t)(uintptr_t)web_lcd; }
WEB(web_lcd_dirty) uint32_t web_lcd_dirty(void)
{
    uint32_t r;
    if (web_dirty[2] <= web_dirty[0] || web_dirty[3] <= web_dirty[1])
        return 0xFFFFFFFFu;
    r = web_dirty[0] | web_dirty[1] << 8 | (web_dirty[2] - web_dirty[0] - 1u) << 16 | (web_dirty[3] - web_dirty[1] - 1u) << 24;
    web_dirty[0] = web_dirty[1] = 240u;
    web_dirty[2] = web_dirty[3] = 0;
    return r;
}

/* the LEDs: key id 0..40 -> 0 off, 1 backlight, 2 glow, 3 lit (the layers of hal/fm1_input.h) */
WEB(web_led) uint32_t web_led(uint32_t id)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id)
                return (fm1_led[p] >> r) & 1u ? 3u : (fm1_led_dim[p] >> r) & 1u ? 2u : (fm1_led_bg[p] >> r) & 1u && fm1_led_bg_ns ? 1u : 0u;
    return 0;
}
/* the webcam's settings, as the VJ SysEx and MIDI set them (vis_set.h; kept with the settings): kind 0 the mode
 * (0..30), 1 the palette (0..8), 2 the autopilot (0 off, 4 8 16 32 bars), 3 the overlay (0 off, 1 info, 2 title,
 * 3 both); web_vj_get reads them back (4: the visualiser is on the screen) */
WEB(web_vj) void web_vj(uint32_t kind, uint32_t v)
{
    if (kind == 0u)
        vis_req_mode = (uint8_t)(v % VIS_N + 1u);
    else if (kind == 1u)
        vis_req_pal = (uint8_t)(v % VIS_NPAL + 1u);
    else if (kind == 2u)
        vis_req_auto = (uint8_t)(v % 5u + 1u);
    else if (kind == 3u)
        vis_req_hud = (uint8_t)((v & 3u) + 1u);
}
WEB(web_vj_get) uint32_t web_vj_get(uint32_t kind)
{
    return kind == 0u ? vis_style : kind == 1u ? vis_pal_live : kind == 2u ? vis_set.autopilot : kind == 3u ? vis_set.hud
         : (uint32_t)vis_shown();
}
static char web_title_in[VIS_TITLE_N];
WEB(web_title_ptr) uint32_t web_title_ptr(void) { return (uint32_t)(uintptr_t)web_title_in; }
WEB(web_title) void web_title(uint32_t n)        /* the title card's text (web_title_ptr, n bytes), as SysEx 6 sets it */
{
    uint32_t k;
    for (k = 0; k < VIS_TITLE_N; k++) {
        uint8_t c = k < n ? (uint8_t)web_title_in[k] : 0u;
        vis_set.title[k] = (char)(c >= 0x20u && c < 0x7Fu ? c : (k < n ? '?' : 0));
    }
    vis_set.title[VIS_TITLE_N - 1u] = 0;
    vis_req_title = 1;
}
/* the page is going away (hidden, closed): what is only in RAM into the flash image, as main.c does before an update
 * (the project as autosave keeps it, even while playing; the settings, which wait for the stop on the FM-1) */
WEB(web_flush) void web_flush(void)
{
    if (web_phase < 3u)
        return;
    persist_flush_now();
    settings_save();                             /* (unchanged: nothing written) */
}
WEB(web_ms) uint32_t web_ms(void) { return fm1_ms; }
/* song mode (SAVE held: SONG / LOOP): set < 0 reads it, 0 / 1 sets it (a stored song needed for 1; stopped only, as
 * the panel). The FM-1 powers on in loop mode; the page puts back what the visitor had (play.js) */
WEB(web_songmode) uint32_t web_songmode(int32_t set)
{
    if (set >= 0 && !song.playing && !transport_req)
        arrangement_enabled = (uint8_t)(set && arr_valid(&arrangement, arrangement_ready()));
    return arrangement_enabled;
}
/* song mode: the step of the song playing and its section (scene | index << 4 | running << 8; 0 when not in a song) */
WEB(web_song) uint32_t web_song(void)
{
    if (!arrangement_clock.running)
        return 0;
    return arrangement.entry[arrangement_clock.index].scene | (uint32_t)arrangement_clock.index << 4 | 1u << 8;
}
WEB(web_playing) uint32_t web_playing(void) { return song.playing; }
