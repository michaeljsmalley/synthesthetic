# Synthesthetic

<p align="center"><img src="assets/logo/synthesthetic-logo.svg" alt="Synthesthetic" width="590"></p>

**Synthesthetic** is a custom firmware for the M-VAVE FM-1 pocket synth that turns it into an audio-visual
instrument in the spirit of the Critter & Guitari EYESY: the FM-1 keeps its synth engines, sequencer and drum
machine, draws audio-reactive visuals on its own screen, and, plugged into a computer, is a **USB webcam**
(UVC, MJPEG 320×180 at 30 fps) next to its USB-MIDI and USB-audio ports, so the visuals go straight into
Photo Booth, OBS, Zoom, QuickTime or a projector app. Firmware updates over USB-MIDI SysEx keep working.

- **Site:** [michaeljsmalley.github.io/synthesthetic](https://michaeljsmalley.github.io/synthesthetic/): the web
  installer, the web editor and the handbook.
- **Play it in the browser:** [the Play page](https://michaeljsmalley.github.io/synthesthetic/play/) runs the
  firmware itself, compiled to WebAssembly, on a 3D FM-1, with its webcam picture beside it. No FM-1 needed.
- **Handbook:** [HANDBOOK.md](HANDBOOK.md): installing, the visuals, SCREEN, the webcam, MIDI, recovery, how it
  works, building. Everything in one place.

Synthesthetic is based on [SLOOP](https://github.com/isod89/sloop-fm1) 2.7.1 by isod89, which is based on
[Felucca](https://github.com/kurogedelic/felucca) by Leo Kuroshita (Hügelton Instruments). GPL-3.0-only; see
[LICENSING.md](LICENSING.md). The instrument side (engines, sequencer, drums, the web editor) is SLOOP's; the
visualiser, the webcam, the video core and the screen's visualiser are Synthesthetic's own.

## What it does

- **Thirty visualiser modes**, all drawn from the sound by the FM-1's second core: the classic scopes and
  spectra (WAVE, BARS, HEAT, XY, METERS), geometric ones (RING, SPIRAL, GRID, TUNNEL, MIRROR, STARS, ORBITS,
  MOIRE), the drum shapes (KIT, SQUARES, SHARDS: a kick, a snare, a hat, a cymbal each their own shape), and
  fourteen procedural ones on a Milkdrop-style feedback warp engine and demo-scene effects (WARP, VORTEX,
  RIPPLE, KALEIDO, MELT, SMOKE, WORMHOLE, PLASMA, FIRE, RIDGES, LAVA, CELLS, WIREFRAME, FRESNEL).
- **SCREEN**: the webcam shows the FM-1's own display instead, to stream a set or a tutorial as you work.
- **Nine palettes** with colour cycling, and a quick fade between modes.
- **The autopilot**: a new mode, variation and palette every 4, 8, 16 or 32 bars, on the bar.
- **The EYESY's panel**: SELECT the mode, PRESETS the palette, ALGORITHM the autopilot, KNOB 1 AMOUNT, KNOB 2
  RATE, KNOB 3 TRAILS, KNOB 4 COLOR. Tap HOME on the TRACKS screen to open the visualiser on the FM-1's screen;
  the webcam shows the same picture either way, and the attract mode opens it after a minute idle.
- **USB**: MIDI (EP1), audio input 16-bit stereo 44.1 kHz (EP3), video 320×180 MJPEG at 30 fps (EP4). The FM-1
  appears as "Synthesthetic". The camera's brightness, saturation, contrast and hue controls are the four
  knobs; an overlay can show the tempo, the bar and a title card; a MIDI channel of its own drives the visuals.
- **The web pages**: the editor's VJ page reads and sets every visual setting (the title too), and the installer
  page sets the webcam's title on a running FM-1.

## Install

The easiest way is [the web installer](https://michaeljsmalley.github.io/synthesthetic/webapp/installer/) in
Chrome or Edge. From a terminal, on an FM-1 running official V15, Dinghy or SLOOP:
`python3 tools/fm1_install.py build/felucca-0.1.fwsc`. From any other firmware, go back to official V15 first
(M-UPGRADE), as for every Felucca-family firmware. Recovery: hold OCT- while switching on for the USB rescue
updater; two boot crashes open it by themselves. The handbook's
[Recovery and safety](HANDBOOK.md#recovery-and-safety) section has the whole ladder, and its
[Building and testing](HANDBOOK.md#building-and-testing) section the rules to keep before touching USB, flash or
boot code.

## Build

```sh
AC79_SDK=$PWD/vendor/ac79-sdk-min JIELI_TOOLCHAIN=$HOME/.jieli/toolchain FELUCCA_UVC=1 sh build.sh
```

The JieLi toolchain runs in Docker (linux/amd64). `FELUCCA_UVC=1` builds the webcam (the default build is
audio-only, as SLOOP). Host tests: `sh tests/run_tests.sh` (the instrument) and `sh tests/run_video_tests.sh`
(the JPEG encoder, the visualiser with a contact sheet, the UVC function). Tools: `tools/fm1_install.py`
(install, `--info`), `tools/fm1_vcdiag.py` (the video core's counters, `stop`, `start`, `style N`),
`tools/fm1_capture.py` (capture the webcam while playing notes), `tools/fm1_avrate.py` (the frame rate macOS
delivers), `tools/fm1_vj.py` (every visual setting, the title too), `tools/fm1_title.py` (the title card's
text and the overlay), `tools/fm1_uvcctl.py` (the camera controls),
`tools/fm1_soak.py` (a soak on the unit).

## Documents

- [HANDBOOK.md](HANDBOOK.md): everything about Synthesthetic, from installing to how it works (also on the site).
- [docs/upstream-sloop/GUIDE.md](docs/upstream-sloop/GUIDE.md): SLOOP's manual, which covers the instrument
  (its visualiser section no longer applies).
- [UPSTREAM.md](UPSTREAM.md): what differs from SLOOP, file by file.
