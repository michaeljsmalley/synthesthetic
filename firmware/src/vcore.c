/* SPDX-License-Identifier: GPL-3.0-only */
/* The video core: the FM-1's second core renders the picture and encodes it for USB.
 *
 * Core 1 runs vcore_job() as one long job (cpu1.h cpu1_run): frame after frame into one of two 8-bit
 * VF_W x VF_H (160 x 90) buffers through a 256-entry palette, each encoded as a 2 x zoomed JPEG (vjpeg.c) into one of two
 * output buffers, then published. On core 0, the USB poll takes the latest complete JPEG for the webcam stream
 * (usb.c uvc_source) and the main loop may paint the latest frame on the LCD. While the job runs the audio
 * render keeps to core 0 (cpu1_taken: cpu1_on() says no), so no audio job is ever handed to a busy core.
 *
 * The flash: core 0 turns it off for every erase, program and raw read (fm1_flash.h), and a core 1 that fetches
 * code or tables from it then would fault. So core 0 parks the job first (vcore_park: a request the job sees
 * between rows and at every MCU row of the encoder; it then spins in a RAM-resident loop until vcore_resume).
 * A park that is not acknowledged in time makes the flash operation fail instead of risking the core.
 *
 * Shared state is split by writer, each on cache lines of its own, with csync around every hand-over, as
 * hal/fm1_cpu1.h does for its mailbox (the cores share the caches, but a line written by both would lose
 * updates). The buffers are written by core 1 and read by core 0 after the flag that publishes them. */
#define VF_W 160u                                       /* the frame: 16:9, as webcams and the EYESY are */
#define VF_H 90u
#define VF_N (VF_W * VF_H)
#define VJ_CAP 16384u                                   /* a JPEG output buffer; the quality follows the size */
#define VC_PARK_US 20000u                               /* core 0 waits this long for the job to park */
#define VC_WORK_US 31000u                               /* a frame's render + encode for 30 fps (33.3 ms, a margin) */

/* the two frames, or (SCREEN) the display's 180 x 180 copy (mirror.c): never both, so one block of RAM */
static uint8_t vf_mem[MIR_N > 2u * VF_N ? MIR_N : 2u * VF_N] __attribute__((section(".pool"), aligned(64)));
static uint8_t *const vf_px[2] = {vf_mem, vf_mem + VF_N};
static uint8_t vj_out[2][VJ_CAP] __attribute__((section(".pool"), aligned(64)));
static vjpeg_ycc_t vf_lut[2][256];                      /* each frame's palette (vproc.c builds it), in Y Cb Cr */
static uint16_t vf_pal565[2][256];                      /* and in RGB565, byte-swapped for the LCD (ui_vis.c) */

static struct {                                         /* written by core 0 only */
    volatile uint32_t run;                              /* keep rendering */
    volatile uint32_t park_req;                         /* stop touching the flash: spin in RAM */
    volatile uint32_t jpeg_busy;                        /* index + 1 of the JPEG the USB poll is sending, 0 none */
    volatile uint32_t level;                            /* the master output's peak, Q15, decaying (audio ISR) */
    volatile uint32_t notes;                            /* the note keys held, bit n = key n (main loop) */
    volatile uint32_t buttons;                          /* the buttons held (main loop) */
    volatile uint32_t knob;                             /* the visualiser's knobs, 8 bits each (ui_vis.c vis_knob) */
    volatile uint32_t hitc[4];                          /* note-ons per track, counted (voice.c vis_hit) */
    volatile uint32_t kickc;                            /* kicks, counted */
    volatile uint32_t note;                             /* the last note per track, 8 bits each */
    volatile uint32_t tpk[4];                           /* the tracks' peaks since the last poll, Q15 */
    volatile uint32_t beat_q8;                          /* the beat, in 1/256 (free-running when stopped) */
    volatile uint32_t playing;
    volatile uint32_t drumc[9];                         /* drum hits per family, counted (voice.c vis_drum) */
    volatile uint32_t var;                              /* the mode's variation (vis_set.h vis_var) */
    volatile uint32_t pal;                              /* the palette shown (vis_pal_live) */
    volatile uint32_t bpm;                              /* the tempo (the overlay) */
    volatile uint32_t hud;                              /* the overlay: vis_set.hud */
    volatile uint32_t pulsec;                           /* the visuals' MIDI channel's notes, counted */
    volatile uint32_t mirror_on;                        /* SCREEN: the LCD's writes go to the copy (mir_px set) */
    uint32_t pad[15];
} vc0 __attribute__((aligned(64)));

static struct {                                         /* written by core 1 only */
    volatile uint32_t parked;                           /* spinning in RAM */
    volatile uint32_t done;                             /* the job returned */
    volatile uint32_t jpeg_ready;                       /* index + 1 of the latest complete JPEG, 0 none */
    volatile uint32_t jpeg_len[2];
    volatile uint32_t frame_ready;                      /* index + 1 of the latest rendered frame */
    volatile uint32_t frames, parks, drops, quality, us_render, us_encode;   /* diagnostics */
    volatile uint32_t mirror;                           /* SCREEN: the job wants the display's copy in vf_mem */
    uint32_t pad[4];
} vc1 __attribute__((aligned(64)));

static uint8_t vcore_running;                           /* core 0: the job was handed over */
static uint32_t vc_src_sent;                            /* vc1.frames when the last JPEG was taken: each goes once */
static uint32_t vc_src_calls, vc_src_live, vc_src_bad, vc_park_fail;   /* diagnostics (usb.c vcore_diag_send) */

/* ---- core 1 ---- */

/* the park loop: in RAM (.c1_text), no calls: the only code core 1 runs while the flash is off */
static void __attribute__((section(".c1_text"), noinline, used)) vcore_spin(void)
{
    vc1.parked = 1;
    fm1_c1_sync();
    while (vc0.park_req) {
        fm1_c1_sync();
    }
    vc1.parked = 0;
    fm1_c1_sync();
}

/* the call into RAM goes through a pointer: a direct jump from the XIP window (0x02000000) to RAM
 * (0x01C00000) is out of the ISA's direct-call range (the linker refuses it; fm1_flash.h's FL_FAR is the
 * same idea) */
static void (*volatile vcore_spin_ptr)(void);

static void vcore_yield(void)                           /* between rows, at MCU rows of the encoder */
{
    fm1_c1_sync();
    if (vc0.park_req) {
        vc1.parks++;
        vcore_spin_ptr();
    }
}

/* SCREEN: the job asks for the display's copy (on) or gives it back (off); 1 once core 0 is there (vcore_poll
 * sets mir_px and starts copying, or stops) */
static int vcore_mirror(uint32_t on)
{
    if (vc1.mirror != on) {
        vc1.mirror = on;
        fm1_c1_sync();
    }
    fm1_c1_sync();
    return vc0.mirror_on == on;
}

/* the pictures: vrender.c and vproc.c (included after ui_vis.c, where the scope rings and the mode are);
 * vrender_frame -> 1: SCREEN, nothing drawn, the frame is the display's copy */
static int vrender_frame(uint8_t *dst, const uint8_t *prev, vjpeg_ycc_t *lut, uint16_t *p565);
static void vrender_palette_init(vjpeg_ycc_t *lut, uint16_t *p565);
static void vr_overlay(uint8_t *f);                     /* the webcam's overlay: drawn for the encode, then taken off */
static void vr_overlay_restore(uint8_t *f);
static int vis_shown(void);                             /* ui_vis.c: the visualiser is on the screen */

static void vcore_job(uint32_t arg)
{
    uint32_t fi = 0, ji = 0, q = 75, qt = 85, slow = 0, fast = 0;   /* q by the JPEG's size; qt SCREEN's by time */
    (void)arg;
    vc1.done = 0;
    vc1.quality = q;
    fm1_c1_sync();
    while (vc0.run) {
        vjpeg_src_t src;
        int32_t n;
        uint32_t t0 = fm1_ticks(), t1, t2, work, screen, eff, parks0 = vc1.parks;
        vcore_yield();
        screen = (uint32_t)vrender_frame(vf_px[fi], vf_px[fi ^ 1u], vf_lut[fi], vf_pal565[fi]);
        t1 = fm1_ticks();
        while (vc0.jpeg_busy == ji + 1u) {              /* the USB poll still sends this buffer: wait */
            vcore_yield();
            if (!vc0.run)
                goto out;
        }
        if (screen) {                                   /* SCREEN: the display's copy, centred, as it is (no overlay) */
            src.px = vf_mem; src.w = MIR_W; src.h = MIR_W; src.pitch = MIR_W; src.zoom = 1;
            src.ow = 2u * VF_W; src.oh = 2u * VF_H; src.ox = MIR_OX; src.oy = 0;
        } else {
            vr_overlay(vf_px[fi]);                      /* the overlay for the webcam only */
            src.px = vf_px[fi]; src.w = VF_W; src.h = VF_H; src.pitch = VF_W; src.zoom = 2;
            src.ow = 0; src.oh = 0; src.ox = 0; src.oy = 0;
        }
        src.lut = vf_lut[fi];
        src.yield = vcore_yield;
        t2 = fm1_ticks();
        eff = q < qt ? q : qt;
        n = vjpeg_encode(&src, eff, vj_out[ji], VJ_CAP);
        if (!screen)
            vr_overlay_restore(vf_px[fi]);              /* (the next frame's feedback and the LCD: without it) */
        fm1_c1_sync();
        vc1.frame_ready = fi + 1u;
        fm1_c1_sync();
        vc1.us_render = (t1 - t0) / FM1_TICKS_PER_US;
        vc1.us_encode = (fm1_ticks() - t2) / FM1_TICKS_PER_US;
        work = vc1.us_render + vc1.us_encode;           /* (the wait for the USB aside) */
        if (!screen)
            qt = 85u;
        else if (vc1.parks == parks0) {                 /* SCREEN on a busy page (text all over: many coefficients):
                                                         * the quality gives way to 30 fps; one slow frame does not
                                                         * count, nor one the flash parked */
            if (work > VC_WORK_US) {
                fast = 0;
                if (++slow >= 3u && qt > 50u) {
                    qt -= 2u;
                    slow = 0;
                }
            } else {
                slow = 0;
                if (work < VC_WORK_US - 3000u && ++fast >= 30u && qt < 85u) {
                    qt++;
                    fast = 0;
                }
            }
        }
        if (n < 0) {                                    /* did not fit: lower the quality, try the next frame */
            q = eff > 40u ? eff - 10u : 30u;
            vc1.drops++;
        } else {
            if (n < (int32_t)(VJ_CAP / 2u) && q < 85u)
                q++;
            vc1.jpeg_len[ji] = (uint32_t)n;
            fm1_c1_sync();
            vc1.jpeg_ready = ji + 1u;
            fm1_c1_sync();
            ji ^= 1u;
            vc1.frames++;
        }
        vc1.quality = eff;
        fi ^= 1u;
    }
out:
    vc1.done = 1;
    fm1_c1_sync();
}

/* ---- core 0 ---- */

static void vcore_palette(void)
{
    vrender_palette_init(vf_lut[0], vf_pal565[0]);
    vrender_palette_init(vf_lut[1], vf_pal565[1]);
}

static int vcore_start(void)                            /* after cpu1_boot: 1 = the job runs on core 1 */
{
    if (vcore_running || !cpu1_on())
        return 0;
    vcore_palette();
    vcore_spin_ptr = vcore_spin;
    vc0.run = 1;
    vc0.park_req = 0;
    vc0.jpeg_busy = 0;
    vc0.mirror_on = 0;
    mir_px = 0;
    vc1.parked = vc1.done = vc1.jpeg_ready = vc1.frame_ready = vc1.mirror = 0;
    vc_src_sent = vc1.frames;
    cpu1_taken = 1;                                     /* the audio render stays on core 0 from here */
    fm1_c1_sync();
    if (!cpu1_run(vcore_job, 0)) {
        cpu1_taken = 0;
        return 0;
    }
    vcore_running = 1;
    return 1;
}

static void vcore_stop(void)                            /* the job returns within a frame; then core 1 is free */
{
    uint32_t t0;
    if (!vcore_running)
        return;
    vc0.run = 0;
    vc0.park_req = 0;
    fm1_c1_sync();
    for (t0 = fm1_ticks(); !vc1.done && fm1_ticks() - t0 < 200000u * FM1_TICKS_PER_US;)
        fm1_c1_sync();
    cpu1_wait();                                        /* the job is done: returns at once */
    vcore_running = 0;
    cpu1_taken = 0;
    mir_px = 0;                                         /* (SCREEN: no copy without the job) */
    vc0.mirror_on = 0;
    fm1_c1_sync();
}

/* before a flash operation: 1 = core 1 is parked in RAM (or there is no video job), 0 = it did not answer
 * (the caller must not touch the flash) */
static int vcore_park(void)
{
    uint32_t t0;
    if (!vcore_running)
        return 1;
    vc0.park_req = 1;
    fm1_c1_sync();
    for (t0 = fm1_ticks(); fm1_ticks() - t0 < VC_PARK_US * FM1_TICKS_PER_US;) {
        fm1_c1_sync();
        if (vc1.parked || vc1.done)
            return 1;
    }
    vc0.park_req = 0;
    fm1_c1_sync();
    vc_park_fail++;
    return 0;
}

static void vcore_resume(void)
{
    vc0.park_req = 0;
    fm1_c1_sync();
}

/* the audio ISR, after the mix: the block's peak, decaying (Q15, stereo interleaved out) */
static void vcore_audio(const int32_t *out, uint32_t n)
{
    uint32_t i, pk = 0, l;
    for (i = 0; i < 2u * n; i++) {
        int32_t v = out[i];
        uint32_t a = (uint32_t)(v < 0 ? -v : v);
        if (a > pk)
            pk = a;
    }
    if (pk > 32767u)
        pk = 32767u;
    l = vc0.level;
    l -= l >> 3;                                        /* ~8 blocks (46 ms) to fade */
    vc0.level = pk > l ? pk : l;
}

/* the main loop, every pass: what the panel holds, the knobs, the notes that started, the tracks' peaks
 * (taken, as the TRACKS meters take them, only while the visualiser is on the screen and they are not),
 * and the beat */
static void vcore_poll(void)
{
    uint32_t i, hit, kick, drum, shown;
    static uint32_t free_q8, last_ms, free_frac;
    shown = (uint32_t)vis_shown();
    vc0.notes = fm1_in.notes;
    vc0.buttons = fm1_in.buttons;
    vc0.knob = vis_knob[0] | (uint32_t)vis_knob[1] << 8 | (uint32_t)vis_knob[2] << 16 | (uint32_t)vis_knob[3] << 24;
    fm1_irq_off();
    hit = vis_hit;
    vis_hit = 0;
    kick = vis_kick_hit;
    vis_kick_hit = 0;
    drum = vis_drum;
    vis_drum = 0;
    for (i = 0; i < 4u; i++) {
        uint32_t pk = i < NPART ? (uint32_t)trk[i].peak : (uint32_t)drums.peak;
        if (shown) {
            if (i < NPART)
                trk[i].peak = 0;
            else
                drums.peak = 0;
        }
        vc0.tpk[i] = pk > 32767u ? 32767u : pk;
    }
    fm1_irq_on();
    for (i = 0; i < 4u; i++)
        if (hit & (1u << i))
            vc0.hitc[i]++;
    if (kick)
        vc0.kickc++;
    for (i = 0; i < 9u; i++)
        if (drum & (1u << i))
            vc0.drumc[i]++;
    vc0.note = vis_note[0] | (uint32_t)vis_note[1] << 8 | (uint32_t)vis_note[2] << 16 | (uint32_t)vis_note[3] << 24;
    vc0.playing = song.playing;
    if (song.playing)
        vc0.beat_q8 = clk_beat * 256u + clk_pos / (BEAT_U / 256u);
    else {
        uint32_t dt = fm1_ms - last_ms, acc;
        acc = (dt > 200u ? 200u : dt) * (uint32_t)song.g[G_BPM] * 256u + free_frac;   /* (the remainder kept: a
                                                         * division a pass would lose up to a tenth of the beat) */
        free_q8 += acc / 60000u;
        free_frac = acc % 60000u;
        vc0.beat_q8 = free_q8;
    }
    last_ms = fm1_ms;
    vc0.var = vis_var;
    vc0.pal = vis_pal_live;
    vc0.bpm = (uint32_t)song.g[G_BPM];
    vc0.hud = vis_set.hud;
    vc0.pulsec = vis_pulse;
    fm1_c1_sync();
    if (vc1.mirror && !mir_px) {                        /* SCREEN begins: the copy from black, then everything drawn */
        memset(vf_mem, 0, MIR_N);
        mir_px = vf_mem;
        vis_req_redraw = 1;
        vc0.mirror_on = 1;
        fm1_c1_sync();
    } else if (!vc1.mirror && mir_px) {                 /* SCREEN ends: the job takes the RAM back once this is seen */
        mir_px = 0;
        vc0.mirror_on = 0;
        fm1_c1_sync();
    }
}

/* the visualiser on the LCD (ui_vis.c): 1 while the frames' RAM holds the display's copy (paint nothing from it) */
static int vcore_mirroring(void)
{
    fm1_c1_sync();
    return vc1.mirror || mir_px;
}

/* the USB poll: the latest complete JPEG, held until vcore_source_done (0 = none new since the last) */
static const uint8_t *vcore_source(uint32_t *len)
{
    uint32_t r, n;
    vc_src_calls++;
    fm1_c1_sync();
    r = vc1.jpeg_ready;
    if (!r || r > 2u || vc1.frames == vc_src_sent)
        return 0;
    vc_src_sent = vc1.frames;
    n = vc1.jpeg_len[r - 1u];
    if (!n || n > VJ_CAP) {                             /* not a frame: the test card instead, and a count */
        vc_src_bad++;
        return 0;
    }
    vc0.jpeg_busy = r;
    fm1_c1_sync();
    *len = n;
    vc_src_live++;
    return vj_out[r - 1u];
}

static void vcore_source_done(void)
{
    vc0.jpeg_busy = 0;
    fm1_c1_sync();
}
