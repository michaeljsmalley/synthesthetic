# Developing Synthesthetic

This is for anyone who wants to build Synthesthetic, change it or see how it works inside. To install and use it,
read the [handbook](HANDBOOK.md).

## How it works

### The hardware

| Part | Details |
| --- | --- |
| Chip | JieLi AC7911B, with two 32-bit pi32v2 cores at 240 MHz and no floating-point unit |
| Memory | About 450 KB of on-chip RAM and 1 MiB of flash |
| Screen | 240 × 240 ST7789 on SPI, which Synthesthetic runs at 30 MHz (SLOOP uses 12 MHz) |
| USB | Full speed (12 Mbit/s) only, with five endpoints |
| Controls | 27 keys, 14 lit buttons, 7 encoders and the MASTER pot, plus a TRS MIDI input |

Because USB is full speed only, the video goes out as MJPEG. Uncompressed video at this size would only manage about 9
frames per second, and every computer's camera stack accepts MJPEG from a full-speed camera.

### Two cores

The first core runs the instrument, which means the audio, the sequencer, the screen and USB. The second core runs a
single long job that draws a frame, encodes it as a JPEG, publishes it and starts over. The cores share state
through small mailboxes, and each mailbox is written by only one core, so neither can overwrite the other's updates.

Both cores run their code from the flash. When the first core writes to the flash, during an autosave for example,
the flash can't be read for a moment. Before that happens, the second core parks itself in a small loop that runs
from RAM and waits there until the write is done. A long soak test, with autosaves running under a live video
stream, has never seen a park fail.

### The picture

Each frame is 160 × 90 pixels of palette indexes, one byte per pixel. Every frame builds its own 256-color palette,
once in YCbCr for the JPEG and once in RGB565 for the screen, which makes color cycling and fades almost free. The
USB video gets the frame doubled to 320 × 180, with a black band above and below in its 320 × 240 picture. The
FM-1's screen shows the frame at one and a half times its size.

The analyzer reads the mix at 22 kHz, before the MASTER knob. It runs a 1024-point FFT and measures 32 bands of about
a quarter octave each, from 40 Hz to 10 kHz, in decibels. Note-ons for each track, drum hits by type, track levels
and the beat come straight from the sequencer.

The feedback engine behind WARP, VORTEX, RIPPLE, KALEIDO, MELT, SMOKE and WORMHOLE works the same way Milkdrop's
does. An 11 × 13 mesh of points describes where each part of the last frame moves (zoom, rotation, swirl, ripple,
drift or kaleidoscope folds). The frame is resampled through that mesh with bilinear blending, and TRAILS sets how
quickly it fades. Fades between modes dim the palette instead of the pixels, so they cost nothing.

### The JPEG encoder

The chip has a JPEG block, but its driver is closed source, so Synthesthetic encodes JPEG in software using only
integer math. It writes baseline JPEG with 4:2:0 color and the standard tables. Because the doubled frame repeats
every pixel as a 2 × 2 block, the encoder folds the matching halves of the DCT together and does about a quarter of
the work, with exactly the same output as the full transform. Blocks of a single color skip the transform
altogether. The quality adjusts to the output size. It goes up while frames fit in half of the 16 KB buffer and comes
down when one overflows. Encoding takes 18 to 25 ms per frame, which leaves room for 30 frames per second in every
mode.

### USB

The FM-1 is a single composite USB device, with USB-MIDI on endpoint 1, the audio input on endpoint 3 and video on
endpoint 4. Video goes out in isochronous packets of 1016 bytes, one per millisecond, with a new frame every 33 ms.
If a camera app closes in the middle of a frame, that frame is released, so the video core never waits for a frame
nobody is reading. The four camera controls come from a standard UVC processing unit.

### How SCREEN works

While SCREEN is on, everything the FM-1 draws on its screen is also drawn into a 240 × 240 copy, one byte per pixel,
so the video shows the display pixel for pixel. Colors go through a 256-color palette of 216 colors and 40 grays,
with exact black and white. The copy shares one block of RAM with the picture frames. The rest of that block holds
things only the visualizers use, the sound analysis and the overlay's scratch space among them, and those stay idle
while SCREEN is on. The second core centers the copy in the 320 × 240 picture and encodes it like any other frame.

### Settings

Your visual settings are stored in the flash in a side record next to SLOOP's settings, so older firmware ignores
them. The web editor reads and writes them as backup object 12 in version 13 of the editor protocol, which is
documented in `web/EDITOR_PROTOCOL.md`.

## Building and testing

The toolchain is JieLi's pi32v2 clang, which runs on Linux x86-64. On a Mac, the build runs each tool in a
linux/amd64 Docker container. The three files it needs from JieLi's AC79 SDK are included in `vendor/ac79-sdk-min`.
JieLi's download server can be slow or unreachable from some networks, so if a download stalls, resume it with
`curl -C -`.

```sh
# Firmware with USB video (a plain build.sh makes the audio-only build)
AC79_SDK=$PWD/vendor/ac79-sdk-min JIELI_TOOLCHAIN=$HOME/.jieli/toolchain FELUCCA_UVC=1 sh build.sh
# A release: identity FM-1_951, version SYNTHESTHETIC 0.1 BETA, output build/felucca-0.1.fwsc
FELUCCA_UVC=1 sh build.sh --release 0.1
# The browser version for the Play page (needs Emscripten, or clang with wasm-ld)
python3 tools/build_web.py --release 0.1
# Tests for the instrument, the video code, the web pages and the browser version
sh tests/run_tests.sh
sh tests/run_video_tests.sh
node web/test_web.mjs
node tests/web_test.mjs
# The website in docs/, built from a release package
python3 web/make_site.py build/felucca-0.1.fwsc 0.1 docs
```

The host tests run the real firmware code on your computer. They check the UI on a framebuffer, the audio engines
against reference renders, the JPEG encoder against libjpeg, every visual mode against a test mix, the USB video
function's descriptors and packets, and the web pages against a simulated FM-1. `tests/web_test.mjs` boots the
browser version, plays it, captures every mode and checks that a project survives a reload.

Every push to GitHub runs the web page tests, the site build test and `tests/web_test.mjs` on the published Play
page firmware (`.github/workflows/tests.yml`). The host tests need a device build, so run them yourself before you
push a firmware change.

### Tools

| Tool | What it does |
| --- | --- |
| `build_web.py` | Builds the browser version of the firmware (`firmware/host/web.c`) into the Play page's `synthesthetic.wasm` |
| `songlib.py` | Writes a song for the Play page in Python, as the FM-1's own sections, steps and song mode. The demo is `tools/songs/demo.py` |
| `song_render.mjs` | Renders a song with the browser build to a WAV file, each track on its own if you ask, with every section's level |
| `fm1_install.py` | Installs a package over USB-MIDI. With `--info`, it shows what the FM-1 is running |
| `fm1_vj.py` | Reads and changes every visual setting |
| `fm1_title.py` | Sets the title and the overlay |
| `fm1_uvcctl.py` | Reads and changes the camera controls |
| `fm1_vcdiag.py` | Shows the video core's counters, and can `stop` or `start` it or set `style N` |
| `fm1_capture.py` | Records the video while playing notes and makes a contact sheet |
| `fm1_avrate.py` | Measures the frame rate macOS actually delivers |
| `fm1_soak.py` | Runs a long test on the FM-1, with autosaves under running video |
| `fm1_softkey.py` | Sends the soft key that opens the ROM download mode |

### Rules

The build enforces a few rules inherited from Dinghy. There's no floating point, no 64-bit division and no calls into
the chip's ROM, hardware registers are only touched in `firmware/hal/`, and RAM use has to stay within budget. This
project adds some of its own.

- Never write to the first 16 KB of the flash.
- Keep the update and rescue paths working, and test the OCT− rescue after any USB change.
- Check the package's hash before installing, because a failed build leaves the previous package in place.
- Feed the watchdog. The main loop has to come around at least every 8 seconds.

`UPSTREAM.md` lists what's different from SLOOP, file by file, and `web/EDITOR_PROTOCOL.md` documents the editor's
SysEx protocol.
