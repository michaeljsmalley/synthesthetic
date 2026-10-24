/* SPDX-License-Identifier: GPL-3.0-only */
/* The USB video function (UVC 1.0) of the FM-1: the descriptors usb.c puts into its configuration, as one
 * byte list, and the probe/commit block. One camera input terminal, a processing unit (its four controls are
 * the visualiser's knobs), one output terminal, one MJPEG format,
 * one UVC_W x UVC_H frame at 30 fps, one isochronous IN endpoint of UVC_PKT bytes on EP4; no status endpoint
 * (optional in the class, and the chip has no sixth endpoint). The shape follows JieLi's own full-speed UVC
 * device for this controller (AC79 SDK apps/common/usb/device/uvc.c: EP4, 768-byte packets).
 *
 *   UVC_DESC_BLOCK(vc, vs, istr)   the bytes: IAD, VC interface + header + terminals, VS interface alt 0 +
 *                                  input header + MJPEG format + frame + colour matching, VS alt 1 + endpoint
 *   UVC_DESC_LEN                   their count (154)
 * Hosts cache descriptors per VID:PID:bcdDevice: usb.c gives the video build a PID of its own. */
#pragma once

#define UVC_PKT 1016u                                  /* isochronous packet (full speed: up to 1023): 2-byte header + 1014 payload */
#define UVC_W 320u                                     /* 4:3, so SCREEN shows the display's 240 rows whole (vcore.c: */
#define UVC_H 240u                                     /* the visual modes' 160 x 90 at 2 x, letterboxed; 0.1: 320 x 180) */
#define UVC_INTERVAL 333333u                           /* 100 ns units: 30 fps */
#define UVC_MAX_FRAME 49152u                           /* dwMaxVideoFrameSize: the JPEG size cap */
#define UVC_MIN_BPS 1000000u
#define UVC_MAX_BPS 6144000u                           /* 768 B/ms */
#define UVC_CLOCK 48000000u                            /* dwClockFrequency: informational */
#define UVC_U16(v) ((v) & 0xFFu), (((v) >> 8) & 0xFFu)
#define UVC_U32(v) ((v) & 0xFFu), (((v) >> 8) & 0xFFu), (((v) >> 16) & 0xFFu), (((v) >> 24) & 0xFFu)

#define UVC_PU_ID 3                                    /* the processing unit: the knobs as camera controls */
#define UVC_VC_CS_LEN (13 + 18 + 11 + 9)               /* header, camera terminal, processing unit, output terminal: 51 */
#define UVC_VS_CS_LEN (14 + 11 + 30 + 6)               /* input header, MJPEG format, frame, colour: 61 */
#define UVC_DESC_LEN (8 + 9 + UVC_VC_CS_LEN + 9 + UVC_VS_CS_LEN + 9 + 7)   /* 154 */

#define UVC_DESC_BLOCK(vc, vs, istr)                                                                        \
    8, 0x0B, (vc), 2, 0x0E, 0x03, 0x00, (istr),                /* IAD: video interface collection, VC + VS */ \
    9, 4, (vc), 0, 0, 0x0E, 0x01, 0x00, (istr),                /* VC interface, no status endpoint */         \
    13, 0x24, 0x01, 0x00, 0x01, UVC_U16(UVC_VC_CS_LEN), UVC_U32(UVC_CLOCK), 1, (vs),   /* VC header 1.00 */   \
    18, 0x24, 0x02, 1, 0x01, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0,   /* camera terminal 1, no controls */ \
    11, 0x24, 0x05, UVC_PU_ID, 1, 0, 0, 2, 0x0F, 0x00, 0,     /* processing unit 3 from 1: brightness, contrast, \
                                                                  hue, saturation (usb.c: the visualiser's knobs) */ \
    9, 0x24, 0x03, 2, 0x01, 0x01, 0, UVC_PU_ID, 0,             /* output terminal 2: USB streaming, from the PU */ \
    9, 4, (vs), 0, 0, 0x0E, 0x02, 0x00, 0,                     /* VS interface alt 0: no bandwidth */          \
    14, 0x24, 0x01, 1, UVC_U16(UVC_VS_CS_LEN), 0x84, 0, 2, 0, 0, 0, 1, 0,   /* VS input header: EP4, OT 2 */  \
    11, 0x24, 0x06, 1, 1, 0, 1, 0, 0, 0, 0,                    /* MJPEG format 1: one frame, default 1 */      \
    30, 0x24, 0x07, 1, 0, UVC_U16(UVC_W), UVC_U16(UVC_H), UVC_U32(UVC_MIN_BPS), UVC_U32(UVC_MAX_BPS),           \
        UVC_U32(UVC_MAX_FRAME), UVC_U32(UVC_INTERVAL), 1, UVC_U32(UVC_INTERVAL),   /* frame 1: UVC_W x UVC_H, 30 fps */ \
    6, 0x24, 0x0D, 1, 1, 4,                                    /* colour: sRGB primaries, BT.601 matrix */     \
    9, 4, (vs), 1, 1, 0x0E, 0x02, 0x00, 0,                     /* VS interface alt 1: the stream */            \
    7, 5, 0x84, 0x05, UVC_U16(UVC_PKT), 1                      /* EP4 IN isochronous async, 1 ms */
