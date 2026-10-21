/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the USB video function in src/usb.c (FELUCCA_UVC=1, FELUCCA_UAC=0):
 *   descriptors  the configuration parsed as a host does: every bLength adds up to wTotalLength and to the
 *                array, bNumInterfaces, the two IADs (audio+MIDI, video), the VC interface with its header's
 *                wTotalLength and collection, the camera and output terminals, the VS interface's input header
 *                total, the MJPEG format and frame (UVC_W x UVC_H, 30 fps), colour matching, alt 1's isochronous
 *                endpoint 0x84 of UVC_PKT bytes; the device descriptor (misc class, PID 0x0003)
 *   probe        the 26-byte block: format 1, frame 1, the interval, the frame size cap, the packet size
 *   packets      uvc_packet_next over three frames of the test card: 2-byte headers (EOH, FID steady within a
 *                frame and toggling between frames, EOF only on the last packet), sizes <= UVC_PKT, the
 *                payloads re-assembling the JPEG byte for byte, the 33 ms gap between frames
 * The SIE is never touched (uvc_service and usb_poll are not called). Exit status: the failed checks. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifndef HALF_FRAMES
#define HALF_FRAMES 256u
#endif
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 0
#define FELUCCA_CDC 0
#ifndef T_UAC
#define T_UAC 0                           /* 1: the combined build, audio input on EP3 beside the video on EP4 */
#endif
#define FELUCCA_UAC T_UAC
#define FELUCCA_UVC 1
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"   /* SIE register macros (never touched here) */
static uint32_t uvc_test_clock;                   /* half-milliseconds: one a poll */
#define UVC_HALF_MS() (uvc_test_clock)
#include "../firmware/src/vis_set.h"                  /* (Synthesthetic: the visual settings) */
#include "../firmware/src/usb.c"

static int fails;
static int check(const char *what, int ok)
{
    printf("uvc: %-70s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
    return ok;
}
static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

int main(void)
{
    const uint8_t *d = CFG_DESC;
    uint32_t n = sizeof CFG_DESC, off, sum = 0, nif = 0, iads = 0, vc_off = 0, vs0_off = 0, vs1_off = 0, ep_off = 0;
    uint32_t fmt_off = 0, frm_off = 0, col_off = 0, vch_off = 0, vsh_off = 0, it_off = 0, ot_off = 0, pu_off = 0;
    /* device descriptor */
    check("device: misc class EF/02/01 (IADs), bcdUSB 2.00", DEV_DESC[4] == 0xEF && DEV_DESC[5] == 2 && DEV_DESC[6] == 1 && DEV_DESC[3] == 2);
    check("device: PID 0x0003 (the video build's own)", le16(DEV_DESC + 10) == 0x0003);
    check("device: bcdDevice has the video bit (0x40)", (DEV_DESC[12] & 0x40u) != 0u);
    /* walk the configuration */
    check("config: wTotalLength = sizeof = CFG_LEN", le16(d + 2) == n && n == CFG_LEN);
    for (off = 0; off + 1u < n && d[off] >= 2u && off + d[off] <= n; off += d[off]) {
        const uint8_t *p = d + off;
        sum += p[0];
        if (p[1] == 4u && p[3] == 0u)
            nif++;
        if (p[1] == 0x0Bu)
            iads++;
        if (p[1] == 4u && p[5] == 0x0Eu && p[6] == 1u) vc_off = off;
        if (p[1] == 4u && p[5] == 0x0Eu && p[6] == 2u && p[3] == 0u) vs0_off = off;
        if (p[1] == 4u && p[5] == 0x0Eu && p[6] == 2u && p[3] == 1u) vs1_off = off;
        if (p[1] == 0x24u && p[2] == 1u && vc_off && !vs0_off && !vch_off) vch_off = off;
        if (p[1] == 0x24u && p[2] == 2u && vc_off && !vs0_off) it_off = off;
        if (p[1] == 0x24u && p[2] == 3u && vc_off && !vs0_off) ot_off = off;
        if (p[1] == 0x24u && p[2] == 5u && vc_off && !vs0_off) pu_off = off;
        if (p[1] == 0x24u && p[2] == 1u && vs0_off && !vsh_off) vsh_off = off;
        if (p[1] == 0x24u && p[2] == 6u) fmt_off = off;
        if (p[1] == 0x24u && p[2] == 7u) frm_off = off;
        if (p[1] == 0x24u && p[2] == 0x0Du) col_off = off;
        if (p[1] == 5u && p[2] == 0x84u) ep_off = off;
    }
    check("config: every descriptor's bLength adds up to the total", sum == n && off == n);
    check("config: bNumInterfaces = the alt-0 interfaces found (4, 5 with the audio input)", nif == d[4] && nif == 4u + T_UAC);
    check("config: two IADs (audio + MIDI, video)", iads == 2u);
    check("VC interface: the IF after the audio function, alt 0, no endpoints, class 0E/01", vc_off && d[vc_off + 2] == UVC_VC_IF && d[vc_off + 4] == 0u);
    if (vch_off) {
        uint32_t t = le16(d + vch_off + 5), s = 0, o;
        for (o = vch_off; o < vs0_off; o += d[o]) s += d[o];
        check("VC header: bcdUVC 1.00, wTotalLength = the CS VC descriptors", d[vch_off + 3] == 0 && d[vch_off + 4] == 1 && t == s && t == UVC_VC_CS_LEN);
        check("VC header: collection of one streaming interface, the VS IF", d[vch_off + 11] == 1u && d[vch_off + 12] == UVC_VS_IF);
    } else
        check("VC header present", 0);
    check("camera input terminal 1 (0x0201), 3 control bytes, none set", it_off && d[it_off + 3] == 1u && le16(d + it_off + 4) == 0x0201u && d[it_off + 14] == 3u);
    check("processing unit 3 from terminal 1: brightness, contrast, hue, saturation (bmControls 0x000F)",
          pu_off && d[pu_off] == 11u && d[pu_off + 3] == UVC_PU_ID && d[pu_off + 4] == 1u && d[pu_off + 7] == 2u &&
          d[pu_off + 8] == 0x0Fu && d[pu_off + 9] == 0u);
    check("output terminal 2 (0x0101) from the processing unit", ot_off && d[ot_off + 3] == 2u && le16(d + ot_off + 4) == 0x0101u && d[ot_off + 7] == UVC_PU_ID);
    check("camera controls: brightness AMOUNT, saturation RATE, contrast TRAILS, hue COLOR, nothing else",
          uvc_pu_knob(2) == 0 && uvc_pu_knob(7) == 1 && uvc_pu_knob(3) == 2 && uvc_pu_knob(6) == 3 && uvc_pu_knob(4) < 0 &&
          uvc_pu_knob(1) < 0);
    check("device: bcdDevice 4.x for the camera with controls", DEV_DESC[13] == 4u);
    check("VS interface alt 0: the IF after VC, no endpoints, class 0E/02", vs0_off && d[vs0_off + 2] == UVC_VS_IF && d[vs0_off + 4] == 0u);
    if (vsh_off) {
        uint32_t t = le16(d + vsh_off + 4), s = 0, o;
        for (o = vsh_off; o < vs1_off; o += d[o]) s += d[o];
        check("VS input header: one format, wTotalLength = the CS VS descriptors, EP 0x84, terminal 2",
              d[vsh_off + 3] == 1u && t == s && t == UVC_VS_CS_LEN && d[vsh_off + 6] == 0x84u && d[vsh_off + 8] == 2u);
    } else
        check("VS input header present", 0);
    check("MJPEG format 1 with one frame, default frame 1", fmt_off && d[fmt_off + 3] == 1u && d[fmt_off + 4] == 1u && d[fmt_off + 6] == 1u);
    check("MJPEG frame 1: UVC_W x UVC_H, 30 fps default, one interval", frm_off && le16(d + frm_off + 5) == UVC_W && le16(d + frm_off + 7) == UVC_H
          && le32(d + frm_off + 21) == 333333u && d[frm_off + 25] == 1u && le32(d + frm_off + 26) == 333333u);
    check("MJPEG frame 1: dwMaxVideoFrameBufferSize = the cap", frm_off && le32(d + frm_off + 17) == UVC_MAX_FRAME);
    check("colour matching: sRGB, BT.601", col_off && d[col_off + 3] == 1u && d[col_off + 5] == 4u);
    check("VS alt 1: one endpoint", vs1_off && d[vs1_off + 4] == 1u && d[vs1_off + 3] == 1u);
    check("EP4 IN isochronous async, UVC_PKT bytes, interval 1, in alt 1", ep_off && ep_off > vs1_off && d[ep_off + 3] == 0x05u
          && le16(d + ep_off + 4) == UVC_PKT && d[ep_off + 6] == 1u);
#if T_UAC
    {
        uint32_t o, a_off = 0;
        for (o = 0; o + 1u < n && d[o] >= 2u; o += d[o])
            if (d[o + 1] == 5u && d[o + 2] == 0x83u) a_off = o;
        check("audio input: EP3 IN isochronous (0x83), UAC_MAXP bytes, before the video function", a_off && a_off < vc_off && d[a_off + 3] == 0x05u && le16(d + a_off + 4) == UAC_MAXP);
        check("audio input: its class requests and CLEAR_FEATURE address EP3", UAC_EP_ADDR == 0x83u);
    }
#endif
    /* probe / commit */
    {
        uint8_t b[UVC_PROBE_LEN], set[UVC_PROBE_LEN];
        uvc.interval = UVC_INTERVAL;
        uvc_probe_fill(b);
        check("probe: format 1, frame 1, interval 333333, max frame = cap, max payload = UVC_PKT",
              b[2] == 1u && b[3] == 1u && le32(b + 4) == 333333u && le32(b + 18) == UVC_MAX_FRAME && le32(b + 22) == UVC_PKT);
        memset(set, 0, sizeof set);
        set[2] = 1; set[3] = 1; set[4] = 0x15; set[5] = 0x16; set[6] = 0x05;   /* 333333 */
        uvc_probe_set(set, UVC_PROBE_LEN);
        uvc_probe_fill(b);
        check("probe: a SET_CUR of our interval is kept", le32(b + 4) == 333333u);
    }
    /* the packetizer over three frames */
    {
        static uint8_t pkt[UVC_PKT], re[65536];
        uint32_t frames = 0, polls = 0, gap = 0, maxgap = 0, mingap = 0xFFFFFFFFu, ok_hdr = 1, ok_size = 1, ok_fid = 1, ok_eof = 1;
        uint32_t t_first = 0, period_min = 0xFFFFFFFFu, period_max = 0;   /* polls from a frame's first packet to the next's */
        uint32_t fid_prev = 0xFF, rlen = 0, in_frame = 0, ok_reassembly = 1, pkts = 0;
        uvc_stream(1);
        while (frames < 3u && polls < 20000u) {
            uint32_t len;
            uvc_test_clock++;
            uvc_tick();
            len = uvc_packet_next(pkt);
            polls++;
            if (!len) {
                gap++;
                continue;
            }
            pkts++;
            ok_size &= len <= UVC_PKT && len > 2u;
            ok_hdr &= pkt[0] == 2u && (pkt[1] & 0x80u) != 0u;
            if (!in_frame) {                            /* first packet of a frame */
                if (frames) {
                    uint32_t period = polls - t_first;
                    if (gap > maxgap) maxgap = gap;
                    if (gap < mingap) mingap = gap;
                    if (period > period_max) period_max = period;
                    if (period < period_min) period_min = period;
                }
                t_first = polls;
                ok_fid &= fid_prev == 0xFFu || (pkt[1] & 1u) != (fid_prev & 1u);
                fid_prev = pkt[1] & 1u;
                in_frame = 1;
                rlen = 0;
                gap = 0;
            } else
                ok_fid &= (pkt[1] & 1u) == fid_prev;
            memcpy(re + rlen, pkt + 2, len - 2u);
            rlen += len - 2u;
            if (pkt[1] & 0x02u) {                       /* EOF */
                ok_eof &= rlen == UVC_TESTCARD_LEN;
                ok_reassembly &= rlen == UVC_TESTCARD_LEN && memcmp(re, UVC_TESTCARD, rlen) == 0;
                frames++;
                in_frame = 0;
            } else
                ok_eof &= rlen < UVC_TESTCARD_LEN;
        }
        check("packets: three frames sent, each a whole test card re-assembled byte for byte", frames == 3u && ok_reassembly);
        check("packets: 2-byte header with EOH, EOF only on the last packet of a frame", ok_hdr && ok_eof);
        check("packets: FID steady within a frame, toggling between frames", ok_fid);
        check("packets: every packet <= UVC_PKT bytes", ok_size);
        printf("uvc: %u packets for 3 frames of %u B; gap between frames %u..%u polls, period %u..%u (expect %u)\n", pkts,
               UVC_TESTCARD_LEN, mingap, maxgap, period_min, period_max, UVC_FRAME_POLLS);
        check("packets: a frame starts one frame interval (66 polls = 33 ms) after the last started", period_min == UVC_FRAME_POLLS && period_max == UVC_FRAME_POLLS);
        check("packets: the test card fits in the interval (the gap after it is what is left)", mingap + pkts / 3u == UVC_FRAME_POLLS);
    }
    {   /* a stream stopped mid-frame: the frame is abandoned (its buffer released), the next one gets a new FID */
        static uint8_t pkt[UVC_PKT];
        uint32_t k, fid0;
        uvc_stream(1);
        for (k = 0; k < 200u && !uvc_packet_next(pkt); k++)
            uvc_tick();                                  /* (the first packet of a frame) */
        uvc_packet_next(pkt);
        fid0 = uvc.fid;
        check("a frame half sent", uvc.off > 0u && uvc.src != 0);
        uvc_frame_drop();                                /* (the host stopped reading, alt still 1: uvc_service) */
        check("host stalled mid-frame: the frame abandoned, the next one a new FID", uvc.off == 0u && uvc.src == 0 && uvc.fid != fid0);
        for (k = 0; k < 200u && !uvc_packet_next(pkt); k++)
            uvc_tick();
        uvc_packet_next(pkt);
        uvc_stream(0);
        check("stream stopped mid-frame: the frame abandoned, FID from 0 again", uvc.off == 0u && uvc.src == 0 && uvc.fid == 0u);
    }
    printf("uvc: %d failed\n", fails);
    return fails;
}
