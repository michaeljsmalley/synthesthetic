// SPDX-License-Identifier: GPL-3.0-only
// Synthesthetic in the browser (firmware/host/web.c, build/web/synthesthetic.wasm), driven as web/play/worklet.js
// drives it: boots through its boot screen to the TRACKS page, a key sounds and lights, PLAY plays, the webcam's
// picture follows the sound in every mode (SCREEN: the display's copy), the projects survive in the flash image,
// and the time each call takes here, for scale. Writes build/web/test-*.ppm to look at.
//   python3 tools/build_web.py && node tests/web_test.mjs [the .wasm: build/web/synthesthetic.wasm]
import fs from 'node:fs';
import assert from 'node:assert/strict';
import { FM1Engine } from '../web/play/worklet.js';

const root = new URL('../', import.meta.url);
const bytes = fs.readFileSync(process.argv[2] || new URL('build/web/synthesthetic.wasm', root));
const module = new WebAssembly.Module(bytes);
assert.equal(WebAssembly.Module.imports(module).length, 0, 'no imports: it runs in an AudioWorklet as it is');

function power(flash) {
  const w = new WebAssembly.Instance(module, {}).exports;
  w._initialize?.();
  const img = new Uint8Array(w.memory.buffer, w.web_flash_ptr(), w.web_flash_size());
  if (flash) img.set(flash); else w.web_flash_erase();
  w.web_boot(0);
  return w;
}

// the worklet's loop: 128 frames a call, a UI frame every 16 ms of audio, a webcam frame every 33 ms
function run(w, ms, t = { frame: 0, cam: 0, render: 0, nf: 0, nc: 0, nr: 0, worst: 0 }) {
  const rate = 44100, quanta = Math.round(ms * rate / 128000);
  let energy = 0, peak = 0, since = 0, sinceCam = 0;
  for (let q = 0; q < quanta; q++) {
    let t0 = performance.now();
    const n = w.web_render(128);
    const a = new Float32Array(w.memory.buffer, w.web_audio_ptr(), 2 * n);
    for (let i = 0; i < a.length; i++) { energy += a[i] * a[i]; peak = Math.max(peak, Math.abs(a[i])); }
    let t1 = performance.now(); t.render += t1 - t0; t.nr++;
    let work = t1 - t0;
    since += 128; sinceCam += 128;
    if (since >= 706) { since -= 706; t0 = performance.now(); w.web_frame(); t1 = performance.now(); t.frame += t1 - t0; t.nf++; work += t1 - t0; }
    if (sinceCam >= 1470) { sinceCam -= 1470; t0 = performance.now(); w.web_cam(); t1 = performance.now(); t.cam += t1 - t0; t.nc++; work += t1 - t0; }
    t.worst = Math.max(t.worst, work);
  }
  return { energy: energy / Math.max(1, quanta * 256), peak, t };
}

const lcd = (w) => new Uint16Array(w.memory.buffer, w.web_lcd_ptr(), 240 * 240);
function lit(w) { let n = 0; for (const v of lcd(w)) n += v !== 0; return n; }
function ppm(w, name) {
  const px = lcd(w), out = Buffer.alloc(240 * 240 * 3);
  for (let i = 0; i < px.length; i++) { const v = px[i]; out[3 * i] = (v >> 11) * 255 / 31; out[3 * i + 1] = ((v >> 5) & 63) * 255 / 63; out[3 * i + 2] = (v & 31) * 255 / 31; }
  fs.writeFileSync(new URL(`build/web/test-${name}.ppm`, root), Buffer.concat([Buffer.from('P6\n240 240\n255\n'), out]));
}
function cam(w) {
  const screen = w.web_cam(), n = screen === 1 ? 180 * 180 : 160 * 90;
  const px = new Uint8Array(w.memory.buffer, w.web_cam_ptr(), n), rgb = new Uint8Array(w.memory.buffer, w.web_cam_rgb_ptr(), 768);
  let bright = 0;
  for (const i of px) bright += rgb[3 * i] + rgb[3 * i + 1] + rgb[3 * i + 2] > 120;
  return { screen, bright, px, rgb };
}
function camPpm(c, name) {
  const W = c.screen === 1 ? 180 : 160, H = c.screen === 1 ? 180 : 90, out = Buffer.alloc(W * H * 3);
  for (let i = 0; i < W * H; i++) out.set(c.rgb.subarray(3 * c.px[i], 3 * c.px[i] + 3), 3 * i);
  fs.writeFileSync(new URL(`build/web/test-${name}.ppm`, root), Buffer.concat([Buffer.from(`P6\n${W} ${H}\n255\n`), out]));
}
let fails = 0;
function check(ok, what) { console.log(`web: ${what.padEnd(72)} ${ok ? 'ok' : 'FAIL'}`); fails += !ok; }

const BTN = { octm: 0, octp: 1, fx: 2, sel: 3, env: 4, lfo: 5, edit: 6, glo: 7, home: 8, save: 9, arp: 10, seq: 11, play: 12, rec: 13 };
const w = power();
let r = run(w, 600);
check(lit(w) > 2000, 'the boot screen draws as it starts');
ppm(w, 'boot');
r = run(w, 3500);
ppm(w, 'tracks');
check(lit(w) > 1500, 'then the TRACKS page');
check(r.energy < 1e-8, 'silent until a key is played');
w.web_input(1 << 7, 0);                                   // C4 (key 7: F3 + 7 semitones)
r = run(w, 400);
check(r.energy > 1e-5, `a key sounds (energy ${r.energy.toExponential(2)}, peak ${r.peak.toFixed(3)})`);
check(w.web_led(14 + 7) === 3, 'and its LED is lit');
w.web_input(0, 0);
r = run(w, 2500);
check(r.energy < r.peak * 1e-2, 'released, it ends');
w.web_input(0, 1 << BTN.play); run(w, 100); w.web_input(0, 0); run(w, 100);
check(w.web_playing() === 1, 'PLAY starts the sequencer');
let beats = 0, was = 0;
for (let i = 0; i < 40; i++) { run(w, 25); const on = w.web_led(BTN.play) === 3; beats += on && !was; was = on; }
check(beats >= 1, `and PLAY lights with the beat (${beats} in a second)`);
w.web_input(0, 1 << BTN.play); run(w, 100); w.web_input(0, 0); run(w, 100);
check(w.web_playing() === 0, 'PLAY again stops it');
// a first visit: the demo song (90 BPM), in song mode: INTRO 4 bars, VERSE 8, CHORUS 8, VERSE 8, BRIDGE 4, CHORUS 8, round
// again; the chorus lifts, the bridge dips, every part sounds, nothing past the limiter's ceiling, STOP ends the song
{
  const BAR = 44100 * 60 * 4 / 90, SCENE = { 3: 'INTRO', 0: 'VERSE', 1: 'CHORUS', 2: 'BRIDGE' };
  const song = (mask, bars, watch) => {
    const d = new WebAssembly.Instance(module, {}).exports;
    d._initialize?.(); d.web_flash_erase(); d.web_boot(mask); run(d, 4500);
    d.web_input(0, 1 << BTN.play);
    const bar = [];                                       // per bar: energy, peak, the section playing
    let e = 0, pk = 0, t = 0, n = 0;
    for (let q = 0; q < Math.ceil(bars * BAR / 128); q++) {
      if (q === 8) d.web_input(0, 0);
      const k = d.web_render(128), a = new Float32Array(d.memory.buffer, d.web_audio_ptr(), 2 * k);
      for (let i = 0; i < a.length; i++) { e += a[i] * a[i]; pk = Math.max(pk, Math.abs(a[i])); }
      n += a.length; t += k;
      if (q % 5 === 0) d.web_frame();
      if (t >= (bar.length + 1) * BAR) { bar.push({ db: 10 * Math.log10(e / n + 1e-12), peak: pk, at: d.web_song() }); e = pk = n = 0; }
    }
    return { d, bar };
  };
  const all = song(15, 44);
  const order = all.bar.map((b) => SCENE[b.at & 15] || '-').filter((s, i, a) => i === 0 || s !== a[i - 1]);
  check(order.join(' ') === 'INTRO VERSE CHORUS VERSE BRIDGE CHORUS INTRO', `song mode: ${order.join(', ')}`);
  const at = (from, to) => all.bar.slice(from, to).reduce((s, b) => s + 10 ** (b.db / 10), 0) / (to - from);
  const verse = 10 * Math.log10(at(4, 12)), chorus = 10 * Math.log10(at(12, 20)), bridge = 10 * Math.log10(at(28, 32));
  check(chorus > verse + 0.5 && bridge < verse - 0.5,
        `the chorus lifts, the bridge dips (verse ${verse.toFixed(1)}, chorus ${chorus.toFixed(1)}, bridge ${bridge.toFixed(1)} dB)`);
  check(Math.max(...all.bar.map((b) => b.peak)) < 0.6, 'under the master limiter\'s ceiling (no clipping)');
  const parts = [1, 2, 4, 8].map((m) => { const s = song(m, 20); return 10 * Math.log10(s.bar.slice(4, 20).reduce((a, b) => a + 10 ** (b.db / 10), 0) / 16); });
  const names = ['bass', 'keys', 'lead', 'drums'], lo = Math.min(...parts), hi = Math.max(...parts);
  check(lo > -30 && hi - lo < 7, 'verse and chorus: every part sounds, within 7 dB: ' + parts.map((x, i) => `${names[i]} ${x.toFixed(1)}`).join(', '));
  all.d.web_input(0, 1 << BTN.play); run(all.d, 100); all.d.web_input(0, 0); run(all.d, 200);
  check(all.d.web_playing() === 0 && all.d.web_song() === 0, 'STOP ends the song (the verse is the loop again)');
  ppm(all.d, 'demo');
}
// the visualiser: HOME on the TRACKS page; the webcam's modes
w.web_input(0, 1 << BTN.home); run(w, 100); w.web_input(0, 0); run(w, 300);
check(w.web_vj_get(4) === 1, 'HOME on TRACKS opens the visualiser');
w.web_vj(2, 0);                                           // (the autopilot off: each mode stays while it is looked at)
const t = { frame: 0, cam: 0, render: 0, nf: 0, nc: 0, nr: 0, worst: 0 };
const modeCost = [];
for (let mode = 0; mode < 31; mode++) {
  w.web_vj(0, mode);
  run(w, 50);
  let notes = 0, k = 0;
  const t0 = { ...t };
  for (let i = 0; i < 8; i++) { notes ^= 1 << (k = (k + 5) % 24); w.web_input(notes, 0); run(w, 120, t); }
  w.web_input(0, 0);
  const c = cam(w);
  modeCost.push([(t.cam - t0.cam) / Math.max(1, t.nc - t0.nc), mode]);
  if (mode === 30) check(c.screen === 1 && c.bright > 300, `SCREEN: the webcam shows the display's copy (${c.bright} bright pixels)`);
  else check(c.screen === 0 && w.web_vj_get(0) === mode, `mode ${mode}: the webcam draws (${c.bright} bright pixels)`);
  if ([0, 5, 16, 23, 24, 28, 30].includes(mode)) camPpm(c, `cam-${mode}`);
  if (mode === 16) ppm(w, 'vis');
}
w.web_vj(0, 16); run(w, 300);
// the title, the overlay
const title = 'HELLO FROM A PAGE';
new Uint8Array(w.memory.buffer, w.web_title_ptr(), 20).set(new TextEncoder().encode(title));
w.web_title(title.length); w.web_vj(3, 2); run(w, 200);
check(w.web_vj_get(3) === 2, 'the overlay setting takes');
camPpm(cam(w), 'cam-title');
// the flash image: a setting survives a power cycle
const gen0 = w.web_flash_gen();
run(w, 6000);
check(w.web_flash_gen() !== gen0, 'the settings are written to the flash image');
const saved = new Uint8Array(w.memory.buffer, w.web_flash_ptr(), w.web_flash_size()).slice();
const w2 = power(saved);
run(w2, 4500);
check(w2.web_vj_get(0) === 16 && w2.web_vj_get(3) === 2, 'and come back at the next power-on');
// the Play page's audio thread (web/play/worklet.js FM1Engine): what it sends, the project kept between visits
{
  const L = new Float32Array(128), R = new Float32Array(128);
  const go = (e, ms, f) => { for (let i = 0; i < Math.round(ms * 44.1 / 128); i++) { e.render(L, R); if (f) f(); } };
  const seen = {}, last = {};
  const post = (m) => { seen[m.t] = (seen[m.t] || 0) + 1; last[m.t] = m; };
  const e = new FM1Engine(bytes, null, post);
  go(e, 4500);
  check(last.ready.fresh === true && seen.lcd > 20 && seen.leds >= 1 && seen.cam > 100 && seen.state >= 1,
        `engine: a first visit boots, sends the screen (${seen.lcd}), the LEDs, the webcam (${seen.cam}), its state`);
  e.message({ t: 'vj', k: 2, v: 0 });                     // (the autopilot off: PLAY starts bar 0, which it would take
                                                          // as its bar to change the picture whenever the boot ended past bar 0)
  e.message({ t: 'vj', k: 0, v: 24 });                    // FIRE
  e.message({ t: 'input', notes: 0, buttons: 1 << BTN.play }); go(e, 100); e.message({ t: 'input', notes: 0, buttons: 0 });
  go(e, 6000);
  check(last.state.playing === 1 && last.state.mode === 24 && seen.flash >= 1 && last.flash.data.byteLength === 0x70000,
        'engine: plays, takes a webcam setting, keeps the project in the flash image while it plays (every 5 s)');
  let energy = 0;
  const seen2 = {};
  const e3 = new FM1Engine(bytes, last.flash.data, (m) => { seen2[m.t] = m; }, last.state.song === 1);
  go(e3, 4500);
  e3.message({ t: 'input', notes: 0, buttons: 1 << BTN.play }); go(e3, 100); e3.message({ t: 'input', notes: 0, buttons: 0 });
  go(e3, 2000, () => { for (const v of L) energy += v * v; });
  check(seen2.ready.fresh === false && seen2.state.mode === 24 && seen2.state.song === 1 && energy > 1,
        `engine: the next visit starts from that image: the setting, song mode and the song (energy ${energy.toFixed(1)})`);
  const e5 = new FM1Engine(bytes, last.flash.data, () => {}, false);   // (song mode left off: the verse loop plays)
  go(e5, 4500); e5.message({ t: 'input', notes: 0, buttons: 1 << BTN.play }); go(e5, 100); e5.message({ t: 'input', notes: 0, buttons: 0 });
  let loopE = 0; go(e5, 2000, () => { for (const v of L) loopE += v * v; });
  check(e5.w.web_songmode(-1) === 0 && e5.w.web_song() === 0 && loopE > 1, `engine: in loop mode PLAY plays the loop (energy ${loopE.toFixed(1)})`);
  e3.message({ t: 'stop' });
  check(e3.stopped === true, 'engine: stops when a new FM-1 takes its place (Start over)');
  const e4 = new FM1Engine(bytes, new ArrayBuffer(100), (m) => { seen2.ready4 = m; });
  check(seen2.ready4.fresh === true, 'engine: an image of the wrong size is not loaded: a new FM-1, with the demo');
}
modeCost.sort((a, b) => b[0] - a[0]);
console.log(`web: per call here: audio ${(t.render / t.nr).toFixed(3)} ms / 128 frames (2.902 ms of sound), UI frame ` +
            `${(t.frame / t.nf).toFixed(3)} ms, webcam frame ${(t.cam / t.nc).toFixed(3)} ms (slowest: mode ` +
            `${modeCost[0][1]} ${modeCost[0][0].toFixed(2)} ms), worst quantum ${t.worst.toFixed(2)} ms`);
if (fails) { console.log(`web: ${fails} FAILED`); process.exit(1); }
console.log('web tests passed');
