#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""What macOS itself delivers from a camera: frames and drops reported by AVFoundation in a few seconds
(the ground truth behind Photo Booth, OBS and QuickTime; ffmpeg's avfoundation input has its own buffer).
    .venv/bin/python tools/fm1_avrate.py [Synthesthetic] [seconds]"""
import sys
import time

import objc
import AVFoundation as AV
from Foundation import NSRunLoop, NSDate, NSObject
import CoreMedia


class Counter(NSObject):
    def init(self):
        self = objc.super(Counter, self).init()
        self.n = 0
        self.dropped = 0
        self.t = []
        self.pts = []                                   # the frames' presentation times (s): the camera's own clock
        return self

    def captureOutput_didOutputSampleBuffer_fromConnection_(self, output, sbuf, conn):
        self.n += 1
        self.t.append(time.time())
        p = CoreMedia.CMSampleBufferGetPresentationTimeStamp(sbuf)
        if p.timescale:
            self.pts.append(p.value / p.timescale)

    def captureOutput_didDropSampleBuffer_fromConnection_(self, output, sbuf, conn):
        self.dropped += 1


def main():
    want = sys.argv[1] if len(sys.argv) > 1 else "Synthesthetic"
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 5.0
    devs = AV.AVCaptureDevice.devicesWithMediaType_(AV.AVMediaTypeVideo)
    dev = next((d for d in devs if want.lower() in str(d.localizedName()).lower()), None)
    if dev is None:
        print("no camera named", want, "; have:", [str(d.localizedName()) for d in devs]); return 3
    fmts = [(str(f.formatDescription()), [(r.minFrameRate(), r.maxFrameRate()) for r in f.videoSupportedFrameRateRanges()]) for f in dev.formats()]
    print("device:", dev.localizedName(), "formats:", fmts[:4])
    sess = AV.AVCaptureSession.alloc().init()
    inp, err = AV.AVCaptureDeviceInput.deviceInputWithDevice_error_(dev, None)
    if err:
        print("input error", err); return 4
    sess.addInput_(inp)
    out = AV.AVCaptureVideoDataOutput.alloc().init()
    out.setAlwaysDiscardsLateVideoFrames_(False)
    c = Counter.alloc().init()
    import dispatch  # noqa: F401  (libdispatch via pyobjc)
    q = dispatch.dispatch_queue_create(b"cam", None)
    out.setSampleBufferDelegate_queue_(c, q)
    sess.addOutput_(out)
    sess.startRunning()
    NSRunLoop.currentRunLoop().runUntilDate_(NSDate.dateWithTimeIntervalSinceNow_(1.0))   # warm up
    n0, d0 = c.n, c.dropped
    t0 = time.time()
    NSRunLoop.currentRunLoop().runUntilDate_(NSDate.dateWithTimeIntervalSinceNow_(secs))
    dt = time.time() - t0
    n, d = c.n - n0, c.dropped - d0
    sess.stopRunning()
    def spread(t):
        g = sorted(b - a for a, b in zip(t, t[1:]))
        return (f"min {g[0] * 1000:.1f} median {g[len(g) // 2] * 1000:.1f} p90 {g[int(len(g) * .9)] * 1000:.1f} "
                f"max {g[-1] * 1000:.1f}") if g else "-"
    print(f"{want}: {n} frames in {dt:.2f} s = {n / dt:.1f} fps, {d} dropped by the OS")
    print(f"  gaps by the frames' presentation times (ms): {spread(c.pts[n0:])}")
    print(f"  gaps as this script's callbacks saw them (ms; Python bunches them): {spread(c.t[n0:])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
