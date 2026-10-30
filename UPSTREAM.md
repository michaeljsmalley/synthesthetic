# Upstream base

The firmware is **Synthesthetic** (since 2026-10-20; the project started as fm1drome).

Since 2026-10-29 this tree carries **SLOOP 2.8.3** by isod89 (merged, see the last entry below); from 2026-10-19 (evening)
it was **SLOOP 2.7.1**, copied verbatim:

- https://github.com/isod89/sloop-fm1 — 2.7.1 is commit `4ec1ac829fd2bdf2c571ea215fb3d62477d01220` (2026-10-16), 2.8.3 is
  `5eb24c1` (tag v2.8.3), GPL-3.0-only
- SLOOP's own README is kept as `docs/SLOOP-README.md`; its GUIDE.md, SLOOP.md, DUALCORE.md, RAM.md, BUILDING.md,
  LICENSES/ and LICENSING.md are unchanged at the top level.
- SLOOP's platform is Felucca's (Copyright (C) 2026 Leo Kuroshita, Hügelton Instruments) with SLOOP's changes.

The first base (2026-10-19 afternoon) was Dinghy (zvenson, commit d44df7db); its guide stays in
`docs/upstream-dinghy-README.md` and `docs/upstream-dinghy/`. The Dinghy package that proved the install, rescue and
update loop on this unit is kept as `backups/dinghy-990-rescuefix.fwsc`.

Local changes to SLOOP so far:

- `firmware/src/main.c`: one line from zvenson/dinghy PR #4 (sgarrettnelson, 2026-10-17): `fm1_lcd_hw_init()` before
  the OCT- rescue check. Without it no key reads at power-on on some boards (reproduced on this unit with Dinghy),
  so the rescue never opened.
- `assets/samples-cc0/` cut to BASS, VIBES, HORNS, STRGS, SCRCH (KIT, FLUTE and PIANO dropped): the Versilian CC0 sets
  cost 303 KB of the 568 KB app area in full; the curated set costs 190 KB (image 464,592 B, 117 KB free) and keeps
  GRAIN's sources. `tools/fetch_cc0.py` brings the rest back.
- **Identity (2026-10-20):** package identity `FM-1_950` (releases `FM-1_95Y`), version string
  `SYNTHESTHETIC 0.1`, USB product `Synthesthetic` (manufacturer `fm1drome`; the update loader keeps `Felucca Update`,
  the installers look for it), the boot screen `splash.c` and `tools/gen_logo.py` are Synthesthetic's (the SLOOP
  logo is gone), SLOOP's documents moved to `docs/upstream-sloop/`, the web editor renamed (its protocol and
  file formats unchanged). Samples: the GRAIN presets use the remaining sets, track 3 starts on VIBES
  (`ui.c TRK_DEF`), the bank lists lost the PIANO and FLUTE entries.
- `tools/fm1_softkey.py` (new): sends the stock soft key after showing the identity, dropping V15 or a
  Felucca-family firmware into ROM download mode (for M-Flasher backups).
- `vendor/ac79-sdk-min/`: the three Apache-2.0 JieLi SDK files the package builder needs (hashes as SLOOP/Dinghy pin
  them) plus the SDK LICENSE. Build with `AC79_SDK=$PWD/vendor/ac79-sdk-min ./build.sh`.
- **SCREEN, the fade, the VJ settings on the web (2026-10-20, evening):** `firmware/src/mirror.c` (new: the
  display's copy for the video (3/4 size at first, pixel for pixel since 2026-10-23), hooked into `lcd.c` `lcd_fill` / `lcd_blit`; `tools/gen_mirror.py` its
  palette), `vcore.c` (the frames' RAM shared with the copy, the SCREEN handshake), `vjpeg.c` (a placed frame with
  black around it; flat blocks skip the DCT, bit for bit), `vrender.c` (SCREEN, the fade between modes instead of
  the channel change), `ui_vis.c` (SCREEN's card; the autopilot and the attract mode leave it alone);
  `editor.c` protocol 13: backup object 12, the visual settings (`web/editor.html` VJ page, `tools/fm1_vj.py`);
  `usb.c` the overlay SysEx `F0 7D 56 43 07 n F7`; `web/index_pkg.html` the title overlay section and
  Synthesthetic's own front text; `felucca.c` sets the video defaults before `lcd.c`.
- **The visual engine v3 (2026-10-20):** `firmware/src/vproc.c` (per-frame palettes, the warp engine, the 14
  procedural modes, overlays, the fade between modes), `firmware/src/vis_set.h` (the visual settings, names, the
  visuals' MIDI channel), `firmware/src/ui_vis.c` (palette, autopilot, attract), `ui_menu.c` (the VJ section),
  `ui.c` / `project.c` (the visual settings in the settings' side record), `seq.c` (the visuals' channel after
  REMOTE and FX CH), `usb.c` + `uvc_desc.h` (a processing unit: the camera controls; the title SysEx; bcdDevice
  4.x), `tools/gen_font5.py` (the HUD font), `tools/fm1_title.py`, `tools/fm1_uvcctl.py`, `tools/fm1_avrate.py`;
  `lcd.c` runs the screen's SPI at 30 MHz. The host budgets (`tests/target_budget.txt`, `tests/cpu_baseline.txt`)
  were rebased with this toolchain on the audio-only build: the upstream file dated from SLOOP 2.4 and already
  flagged the 2.7.1 audio interrupt.
- **USB video (fm1drome's own work, 2026-10-19/20):** `firmware/src/uvc_desc.h`, the UVC function in
  `firmware/src/usb.c` (`FELUCCA_UVC=1`: EP4 isochronous IN, MJPEG 320×180, 320×240 since 2026-10-23, the audio input moves to EP3),
  `firmware/src/vjpeg.c` + `vjpeg_tables.h` (integer JPEG encoder), `firmware/src/vcore.c` (core 1 renders and
  encodes; park/resume around flash writes; `cpu1_taken` keeps the audio render on core 0),
  `firmware/src/vrender.c` (fm1drome's visualizer: analyser, 16 modes, knobs, triggers), `firmware/src/ui_vis.c`
  (**replaced**: SLOOP's twelve screen visualisers are gone; the file now shows the video core's frame on the
  LCD and handles SELECT and the knobs; `scratch.h`, `audio.c SCOPE_N` 1024, `fx.c` the averaged tap,
  `ui_input.c` the knobs, `panel.c` 16 modes in the settings), `fx.c DLY_LEN` halved to
  32768 for the RAM, `cpu1.c/h` (`cpu1_taken`), `hal/fm1_cpu1.h` (`fm1_c1_sync`), `felucca.c` (flash wrappers park
  the video core), `main.c` (video core start/stop, SysEx diagnostics), `tools/build.py` (the FELUCCA_UVC/VCORE
  flags, the test card), `tools/gen_testcard.py`, `tools/fm1_vcdiag.py`, `tools/fm1_capture.py`,
  `tests/vjpeg_test.c`, `tests/vjpeg_check.py`, `tests/uvc_test.c`, `tests/vrender_test.c`, `tests/vrender_sheet.py`,
  `tests/run_video_tests.sh`. The audio-only SLOOP build is unchanged by all of it (`FELUCCA_UVC=0`, the default).
- **SYSTEM > RESET (2026-10-26):** the menu's MIDI LEARN CLEAR (2.6) row is now RESET (`ui_menu.c`): KNOB 3 picks
  MIDI LEARN (the old clear, one OCT+), SETTINGS or EVERYTHING (OCT+ twice within 3 s). `project.c`
  `settings_defaults` and `factory_reset` (the work saved first, or the projects, presets, FM6 bank and sample
  slots erased; the panel's calibration kept; the RAM project slots cleared), `storage.c` `st_wipe`, `main.c`
  `reset_restart` (USB detached, the boot guard cleared, a reboot), `web.c` and `web/play/worklet.js` (the
  browser's FM-1 restarts on its flash image), `vis_set.h` `VIS_SET_INIT`; tests in `tests/ui_pages_test.c`,
  `tests/storage_test.c`, `tests/web_test.mjs`.
- **SLOOP 2.8.3 merged (2026-10-29):** a three-way merge of upstream 2.7.1 → v2.8.3 over this tree (git, base 4ec1ac8;
  answering issue #1). Upstream brings SEQ > NOTES (the piano-roll editor and the per-step sheet), the SEQ menu section
  (STEP ADVANCE, DRUM PREVIEW), FILTER RESO, the punch-in fade (FX + SELECT), MIDI EXPRESSION (CC 11), Bank Select,
  steadier external clock, the held-note and drum-voice fixes, the second core's hardening from X0X (a check word on
  every job, one core until power-off after a give-up or a crash beside it, `app.ld` `.noinit.c1bar`), the USB RESCUE
  fix (`recovery.c` turns the backlight pin on before the key scan; our `main.c` `fm1_lcd_hw_init()` line stays too),
  and the audio-streaming interface first in the USB audio collection (macOS 15 audio input; bcdDevice +0.08, so hosts
  re-read the descriptors). Resolved by hand: `ui.c` (the version string), `ui_input.c` (NOTES zoom on PRESETS, kept
  off while the visualizer shows), `usb.c` (`UAC_BCD_AS` added to `USB_BCD_LO`; the UVC interface numbering unchanged),
  `ui_menu.c` (six sections: SCREEN, LIGHTS, AUDIO, SEQ, SYSTEM, VJ; `MS_GAP`), `tests/ui_pages_test.c`,
  `tests/web_test.mjs` (SYSTEM is now the fifth section), `web/test_web.mjs` (50 globals), `tests/len_test.c` (ours:
  `rainbow.c`, `mirror.c`, `SCOPE_N` 1024), `project.c settings_defaults` resets the new settings too, `tests/golden.txt`
  (VIBES, SCRATCH and the GM kit changed upstream: the sample retrigger fade and `gen_samples.py`). `web/index_pkg.html`
  and `docs/webapp/` are Synthesthetic's own and were kept whole: upstream removed its in-browser return to V15 as
  unreliable on some systems; ours stays, see the handbook. Upstream's documents refreshed in `docs/upstream-sloop/`.
  Checked on the unit the same day (the local installer, identity FM-1_950): boot, the six-section menu, 2 CORES, USB
  video through every mode with the video job on core 1 under the new stack guard (`fm1_c1_main` limits core 1's
  stack to its 2816 bytes less 256), USB audio, the rescue. Still only on paper: the bar (a crash while the video
  core runs leaves the FM-1 on one core, so no USB video, until a power-off).
- **The SMK-37 (branch smk37, 2026-10-30):** genkigenki's board layer for the M-Vave SMK-37 (sloop-fm1 branch `smk37`,
  isod89/sloop-fm1#112, on SLOOP 2.7) merged with git (shared history: a later state of the branch, or the layer on
  SLOOP's main once the pull request lands, merges the same way). Everything is behind `BOARD_SMK37`; the FM-1 build
  stays byte-identical (checked after every step). Synthesthetic's own: the identities (`SMK-37 Pro_950`, releases
  `95Y`), the nine-section menu (SEQ and VJ among the layer's PLAY, MIDI, DAW), the VISUAL fader bank (E: the
  visualizer's knobs, `ui_fbank.c`, `main.c smk_faders`), the LCD HAL split (2.8.3's backlight helper FM-1 only), the
  4 KiB pool margin on the SMK video build (`build.py`), the installer library keeping 2.8.3's port search with the
  layer's model rules, and the tests on Synthesthetic's names and sample sets. See `SMK37.md`.
  Checked on the user's SMK-37 Elite the same day (installed from the local site build, identity SMK-37 Elite_950):
  everything on the bring-up list; the one bug found, the fader flicker over the visualizer, fixed (main.c smk_faders).

