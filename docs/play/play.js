// SPDX-License-Identifier: GPL-3.0-only
// The Play page: Synthesthetic's firmware (worklet.js, synthesthetic.wasm) on a 3D FM-1 (fm1-3d.js; a flat one
// without WebGL), its webcam picture beside it. The firmware runs in the audio thread; this side draws what it sends
// (the screen's changes, the LEDs, the webcam's frames, its state), sends it the panel (the model, the computer
// keyboard, a MIDI keyboard) and keeps its flash image in IndexedDB.
import { createFM1, KPOS, BUTTONS, WHITE, BLACK, LCDC } from './fm1-3d.js?v=9993183aff';

const $ = (id) => document.getElementById(id);
const NAMES = JSON.parse($('vj-names').textContent);
const ENC = { select: 0, algo: 1, k1: 2, k2: 3, k3: 4, k4: 5, presets: 6 };     // matrix encoders (panel.c PANEL_DEFAULT)
const B = Object.fromEntries(BUTTONS.map(([name], id) => [name, id]));        // matrix buttons by name

// ------------------------------------------------------------------ the computer keyboard (by position: e.code)
const PIANO = ['KeyZ', 'KeyS', 'KeyX', 'KeyD', 'KeyC', 'KeyF', 'KeyV', 'KeyB', 'KeyH', 'KeyN', 'KeyJ', 'KeyM', 'Comma', 'KeyL',
  'KeyQ', 'Digit2', 'KeyW', 'Digit3', 'KeyE', 'KeyR', 'Digit5', 'KeyT', 'Digit6', 'KeyY', 'KeyU', 'Digit8', 'KeyI'];   // key n: F3 + n
const KEYS = Object.fromEntries(PIANO.map((code, n) => [code, 14 + n]));
Object.assign(KEYS, { Space: B.play, Enter: B.rec, Escape: B.home, ShiftLeft: B.seq, ShiftRight: B.seq, KeyA: B.arp, KeyG: B.glo,
  ArrowLeft: B.octm, ArrowRight: B.octp });
const TURNS = { ArrowUp: ['select', 1], ArrowDown: ['select', -1], BracketRight: ['presets', 1], BracketLeft: ['presets', -1] };
const CAP = (code) => code.replace(/^(Key|Digit)/, '').replace('Comma', ',');
const HINTS = Object.fromEntries(PIANO.map((code, n) => [14 + n, CAP(code)]));
Object.assign(HINTS, { [B.play]: 'SPACE', [B.rec]: 'ENTER', [B.home]: 'ESC', [B.seq]: 'SHIFT', [B.arp]: 'A', [B.glo]: 'G', [B.octm]: '<', [B.octp]: '>' });

// ------------------------------------------------------------------ the screen: a 240 x 240 canvas, RGB565 in
const lcd = document.createElement('canvas');
lcd.width = lcd.height = 240;
const lx = lcd.getContext('2d'), lcdImg = lx.createImageData(240, 240), lcd32 = new Uint32Array(lcdImg.data.buffer);
const RGB = new Uint32Array(65536);
for (let v = 0; v < 65536; v++)
  RGB[v] = 0xFF000000 | (((v & 31) * 255 / 31) | 0) << 16 | ((((v >> 5) & 63) * 255 / 63) | 0) << 8 | (((v >> 11) * 255 / 31) | 0);
function idleScreen() {                               // before the power: the glass dark, a word on it
  lx.fillStyle = '#000'; lx.fillRect(0, 0, 240, 240);
  lx.fillStyle = '#4a3f58'; lx.font = '16px "FM-1 Terminus", monospace'; lx.textAlign = 'center'; lx.fillText('POWER ON', 120, 124);
  lcd32.fill(0xFF000000);
}

// ------------------------------------------------------------------ the FM-1: 3D, or flat without WebGL
const stage = $('stage');
let fm1 = createFM1(stage, { screen: lcd, onButton: (id, on) => hold(id, on), onKey: (n, on) => hold(14 + n, on), onKnob: turn, onMaster: masterTurn });
if (!fm1) fm1 = flatFM1(stage, lcd);
document.fonts?.load('16px "FM-1 Terminus"').then(idleScreen, idleScreen);
idleScreen();

// ------------------------------------------------------------------ the panel's state: what is held, from where
const holds = new Uint8Array(41), downAt = new Float64Array(41);
let notes = 0, buttons = 0, master = 820;
function hold(id, on) {
  if (!powered) { if (on) powerOn(); return; }
  if (on) {
    if (holds[id]++ === 0) { downAt[id] = performance.now(); apply(id, true); }
  } else if (holds[id] && --holds[id] === 0) {
    const wait = 40 - (performance.now() - downAt[id]);      // a press shorter than a UI frame still counts
    if (wait > 0) setTimeout(() => { if (!holds[id]) apply(id, false); }, wait); else apply(id, false);
  }
}
function apply(id, on) {
  if (id < 14) buttons = on ? buttons | 1 << id : buttons & ~(1 << id);
  else notes = on ? notes | 1 << (id - 14) : notes & ~(1 << (id - 14));
  send({ t: 'input', notes, buttons });
}
function turn(name, steps) { if (powered) send({ t: 'enc', e: ENC[name], s: steps }); else if (steps) powerOn(); }
function masterTurn(steps) {
  master = Math.max(0, Math.min(1023, master + steps * 32));
  fm1.master(master);
  send({ t: 'master', v: master });
}
fm1.master(master);

addEventListener('keydown', (e) => {
  if (e.metaKey || e.ctrlKey || e.altKey || e.target.closest?.('input, select, textarea, [contenteditable]')) return;
  const id = KEYS[e.code], tn = TURNS[e.code];
  if (id === undefined && !tn) return;
  if (e.code === 'Escape' && document.fullscreenElement) return;   // (Esc leaves full screen first)
  e.preventDefault();
  if (e.repeat) return;
  if (tn) { turn(tn[0], tn[1]); return; }
  keysDown.add(e.code); fm1.down(id, true); hold(id, true);
});
addEventListener('keyup', (e) => {
  if (!keysDown.delete(e.code)) return;
  const id = KEYS[e.code];
  fm1.down(id, false); hold(id, false);
});
const keysDown = new Set();
addEventListener('blur', () => { for (const code of keysDown) { fm1.down(KEYS[code], false); hold(KEYS[code], false); } keysDown.clear(); });

// ------------------------------------------------------------------ the audio thread
let ctx = null, node = null, powered = false, starting = null;
const wasmBytes = fetch(new URL('synthesthetic.wasm?v=7f815955b0', import.meta.url)).then((r) => {
  if (!r.ok) throw new Error('the firmware did not load (' + r.status + ')');
  return r.arrayBuffer();
});
wasmBytes.catch(() => {});
const flashSaved = loadFlash();
const queue = [];
function send(m) { if (node) node.port.postMessage(m); else queue.push(m); }

async function powerOn() {
  if (powered || starting) return starting;
  $('stage-msg').textContent = 'Starting…';            // (the stage itself is the power button: click it, or play)
  starting = (async () => {
    try {
      const Ctx = window.AudioContext || window.webkitAudioContext;
      if (!Ctx || !window.AudioWorkletNode) throw new Error('this browser has no AudioWorklet: try a current Chrome, Edge, Firefox or Safari');
      if (!ctx) {
        try { ctx = new Ctx({ sampleRate: 44100, latencyHint: 'interactive' }); } catch (e) { ctx = new Ctx({ latencyHint: 'interactive' }); }
      }
      const resumed = ctx.resume();                    // (inside the click: the browser lets the sound start)
      await ctx.audioWorklet.addModule(new URL('worklet.js?v=1307401125', import.meta.url));
      await start(await flashSaved);
      await resumed;
      powered = true;
      stage.classList.add('on');
      $('stage-msg').hidden = true;
      say('power-note', ctx.sampleRate === 44100 ? 'Ready. Press PLAY or play a key.' : `Your audio output runs at ${ctx.sampleRate / 1000} kHz, so the pitch will be off.`,
          ctx.sampleRate === 44100 ? '' : 'warn');
      for (const m of queue.splice(0)) send(m);
      send({ t: 'master', v: master });
      send({ t: 'cam', on: camWanted() });
    } catch (e) {
      $('stage-msg').textContent = 'Click to power on';
      say('power-note', 'It didn\'t start: ' + (e && e.message || e), 'bad');
    } finally { starting = null; }
  })();
  return starting;
}
async function start(flash) {
  const wasm = (await wasmBytes).slice(0);
  let song;                                           // song mode as this visitor left it (the FM-1 itself starts in loop mode)
  try { const v = localStorage.getItem(SONG_KEY); if (v !== null) song = v === '1'; } catch (e) { /* (private window) */ }
  node = new AudioWorkletNode(ctx, 'synthesthetic', { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2], processorOptions: { wasm, flash, song } });
  node.port.onmessage = ({ data }) => receive(data);
  node.onprocessorerror = () => say('power-note', 'The FM-1 stopped. Reload the page to start it again.', 'bad');
  node.connect(ctx.destination);
}
stage.addEventListener('pointerdown', () => { if (!powered) powerOn(); });

// ------------------------------------------------------------------ what the firmware sends
let lastState = null;
function receive(m) {
  switch (m.t) {
    case 'lcd': {
      const { x0, y0, w, h, px } = m;
      for (let y = 0; y < h; y++) {
        const o = (y0 + y) * 240 + x0, s = y * w;
        for (let x = 0; x < w; x++) lcd32[o + x] = RGB[px[s + x]];
      }
      lx.putImageData(lcdImg, 0, 0, x0, y0, w, h);
      break;
    }
    case 'leds': fm1.leds(m.leds); break;
    case 'cam': drawCam(m); break;
    case 'state': showState(m); break;
    case 'flash': saveFlash(m.data); break;
    case 'error': say('power-note', 'The firmware didn\'t start: ' + m.message, 'bad'); break;
  }
}

// ------------------------------------------------------------------ the webcam's monitor
const cam = $('cam'), cx = cam.getContext('2d');
const pic = document.createElement('canvas'), px2 = pic.getContext('2d');
pic.width = 160; pic.height = 90;
const picImg = px2.createImageData(160, 90);
const scr = document.createElement('canvas'), sx2 = scr.getContext('2d');
scr.width = scr.height = 180;
const scrImg = sx2.createImageData(180, 180);
function noSignal() {
  cx.fillStyle = '#000'; cx.fillRect(0, 0, 320, 180);
  cx.fillStyle = '#4a3f58'; cx.font = '16px "FM-1 Terminus", monospace'; cx.textAlign = 'center'; cx.fillText('NO SIGNAL', 160, 94);
}
noSignal();
document.fonts?.ready.then(() => { if (!powered) noSignal(); });
function drawCam({ screen, px, rgb }) {
  const img = screen ? scrImg : picImg, d = img.data;
  for (let i = 0, j = 0; i < px.length; i++, j += 4) { const k = px[i] * 3; d[j] = rgb[k]; d[j + 1] = rgb[k + 1]; d[j + 2] = rgb[k + 2]; d[j + 3] = 255; }
  cx.imageSmoothingEnabled = true;
  if (screen) {                                       // SCREEN: the display's copy, 180 x 180 at 72 (vcore.c MIR_OX)
    sx2.putImageData(scrImg, 0, 0);
    cx.fillStyle = '#000'; cx.fillRect(0, 0, 320, 180);
    cx.drawImage(scr, 72, 0);
  } else {                                            // 160 x 90, twice the size: the webcam's 320 x 180
    px2.putImageData(picImg, 0, 0);
    cx.drawImage(pic, 0, 0, 320, 180);
  }
}
const camVisible = { on: true };
new IntersectionObserver((es) => { camVisible.on = es[0].isIntersecting; send({ t: 'cam', on: camWanted() }); }).observe($('monitor'));
const camWanted = () => camVisible.on && !document.hidden;
$('fs').addEventListener('click', () => {
  const mon = $('monitor');
  if (document.fullscreenElement) document.exitFullscreen(); else (mon.requestFullscreen || mon.webkitRequestFullscreen)?.call(mon);
});

// the webcam's settings: as the FM-1's VJ menu (and the editor's VJ page) set them
function fill(sel, names) { sel.textContent = ''; names.forEach((n, i) => sel.append(new Option(n, String(i)))); }
fill($('vj-mode'), NAMES.modes.map((n, i) => `${i + 1} ${n}`));
fill($('vj-pal'), NAMES.palettes);
fill($('vj-auto'), ['Off', 'Every 4 bars', 'Every 8 bars', 'Every 16 bars', 'Every 32 bars']);
fill($('vj-ov'), ['Nothing', 'Info (mode, BPM)', 'Title', 'Info and title']);
const VJ = { 'vj-mode': 0, 'vj-pal': 1, 'vj-auto': 2, 'vj-ov': 3 };
for (const [id, k] of Object.entries(VJ)) $(id).addEventListener('change', (e) => { if (!powered) powerOn(); send({ t: 'vj', k, v: Number(e.target.value) }); });
function setTitle() {
  if (!powered) powerOn();
  send({ t: 'title', s: $('vj-title').value });
  if (Number($('vj-ov').value) < 2) { $('vj-ov').value = '2'; send({ t: 'vj', k: 3, v: 2 }); }   // (a title: show it)
}
$('vj-set').addEventListener('click', setTitle);
$('vj-title').addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); setTitle(); } });
const SONG_KEY = 'synthesthetic.play.song';
function showState(s) {
  lastState = s;
  try { localStorage.setItem(SONG_KEY, s.song ? '1' : '0'); } catch (e) { /* (private window) */ }
  $('vj-mode').value = String(s.mode); $('vj-pal').value = String(s.palette); $('vj-auto').value = String(s.autopilot); $('vj-ov').value = String(s.overlay);
  $('chan').textContent = `${s.mode + 1}/${NAMES.modes.length} ${NAMES.modes[s.mode] || ''}`;
  $('chan').hidden = false;
}

// ------------------------------------------------------------------ the view, the hints
const views = $('views');
function view(v) {
  fm1.view(v);
  for (const b of views.querySelectorAll('button')) b.setAttribute('aria-pressed', String(b.dataset.view === v));
  try { localStorage.setItem('synthesthetic.play.view', v); } catch (e) { /* (private window) */ }
}
views.addEventListener('click', (e) => { const b = e.target.closest('button'); if (b) view(b.dataset.view); });
{
  let v = matchMedia('(pointer: coarse)').matches ? 'top' : 'float';
  try { v = localStorage.getItem('synthesthetic.play.view') || v; } catch (e) { /* (private window) */ }
  view(v);
}
$('hints').addEventListener('change', (e) => fm1.hints(e.target.checked ? HINTS : null));

// ------------------------------------------------------------------ MIDI: a keyboard plays the selected track (or by channel)
let midiAccess = null;
$('midi').addEventListener('click', async () => {
  if (!navigator.requestMIDIAccess) { say('midi-note', 'This browser doesn\'t support Web MIDI. Try Chrome, Edge or Firefox.', 'warn'); return; }
  try {
    midiAccess = await navigator.requestMIDIAccess();
    midiAccess.onstatechange = midiAttach;
    midiAttach();
    if (!powered) powerOn();
  } catch (e) { say('midi-note', 'MIDI access was blocked.', 'warn'); }
});
function midiAttach() {
  const ins = [...midiAccess.inputs.values()];
  for (const i of ins) i.onmidimessage = midiIn;
  say('midi-note', ins.length ? 'Listening to ' + ins.map((i) => i.name).join(', ') + '.' : 'No MIDI keyboard found. Plug one in and it will be picked up right away.');
  $('midi').textContent = ins.length ? 'MIDI on' : 'Connect MIDI';
}
function midiIn(e) {
  const [st, d1 = 0, d2 = 0] = e.data;
  if (st < 0x80 || st >= 0xF0) return;                // (clock and SysEx stay out: the FM-1 here keeps its own time)
  const kind = st & 0xF0, ch = $('midi-ch').checked ? st & 15 : 14;   // channel 15: no track's, so the selected track's
  send({ t: 'midi', p: ((kind >> 4) | (kind | ch) << 8 | d1 << 16 | d2 << 24) >>> 0 });
}

// ------------------------------------------------------------------ the flash image: kept in this browser (IndexedDB)
// (v3: the demo song in song mode; an image saved before it, while the page was being made, is left unread)
const IMAGE = 'image-v5';
function db() {
  return new Promise((res, rej) => {
    const r = indexedDB.open('synthesthetic-play', 1);
    r.onupgradeneeded = () => r.result.createObjectStore('flash');
    r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error);
  });
}
async function loadFlash() {
  try {
    const d = await db();
    return await new Promise((res) => { const q = d.transaction('flash').objectStore('flash').get(IMAGE); q.onsuccess = () => res(q.result || null); q.onerror = () => res(null); });
  } catch (e) { return null; }
}
async function saveFlash(data) {
  try { const d = await db(); d.transaction('flash', 'readwrite').objectStore('flash').put(data, IMAGE); } catch (e) { /* (no storage: this visit only) */ }
}
async function eraseFlash() {
  const d = await db();
  await new Promise((res, rej) => { const t = d.transaction('flash', 'readwrite'); t.objectStore('flash').delete(IMAGE); t.oncomplete = res; t.onerror = () => rej(t.error); });
}
addEventListener('visibilitychange', () => { send({ t: 'cam', on: camWanted() }); if (document.hidden) send({ t: 'flush' }); });
addEventListener('pagehide', () => send({ t: 'flush' }));

// start over: erase the image, power on again with the demo
$('reset').addEventListener('click', () => { $('reset-row').hidden = true; $('reset-ask').hidden = false; $('reset-yes').focus(); });
$('reset-no').addEventListener('click', () => { $('reset-ask').hidden = true; $('reset-row').hidden = false; });
$('reset-yes').addEventListener('click', async () => {
  $('reset-ask').hidden = true; $('reset-row').hidden = false;
  try {
    await eraseFlash();
    try { localStorage.removeItem(SONG_KEY); } catch (e) { /* (private window) */ }
    if (node) {
      node.port.postMessage({ t: 'stop' }); node.port.onmessage = null; node.disconnect(); node = null;
      holds.fill(0); notes = buttons = 0; fm1.unlatchAll?.();
      idleScreen();
      await start(null);
      send({ t: 'master', v: master }); send({ t: 'cam', on: camWanted() });
    }
    say('reset-note', 'Erased. The FM-1 restarted with the demo song.', 'good');
  } catch (e) { say('reset-note', 'Couldn\'t erase: ' + (e && e.message || e), 'bad'); }
});

// ------------------------------------------------------------------ the computer keyboard, drawn
{
  const ROWS = [['r1', ['Backquote', 'Digit1', 'Digit2', 'Digit3', 'Digit4', 'Digit5', 'Digit6', 'Digit7', 'Digit8', 'Digit9', 'Digit0', 'Minus', 'Equal']],
    ['r2', ['KeyQ', 'KeyW', 'KeyE', 'KeyR', 'KeyT', 'KeyY', 'KeyU', 'KeyI', 'KeyO', 'KeyP', 'BracketLeft', 'BracketRight']],
    ['r3', ['KeyA', 'KeyS', 'KeyD', 'KeyF', 'KeyG', 'KeyH', 'KeyJ', 'KeyK', 'KeyL', 'Semicolon', 'Quote']],
    ['r4', ['ShiftLeft', 'KeyZ', 'KeyX', 'KeyC', 'KeyV', 'KeyB', 'KeyN', 'KeyM', 'Comma', 'Period', 'Slash', 'ShiftRight']],
    ['r5', ['Escape', 'Space', 'Enter', 'ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown']]];
  const LABEL = { Backquote: '`', Minus: '-', Equal: '=', BracketLeft: '[', BracketRight: ']', Semicolon: ';', Quote: "'", Comma: ',', Period: '.',
    Slash: '/', ShiftLeft: 'Shift', ShiftRight: 'Shift', Escape: 'Esc', Space: 'Space', Enter: 'Enter', ArrowLeft: '←', ArrowRight: '→', ArrowUp: '↑', ArrowDown: '↓' };
  const NOTE = (n) => ['F', 'F#', 'G', 'G#', 'A', 'A#', 'B', 'C', 'C#', 'D', 'D#', 'E'][n % 12] + (3 + Math.floor((n + 5) / 12));
  const FN = { [B.play]: 'Play', [B.rec]: 'Rec', [B.home]: 'Home', [B.seq]: 'Seq', [B.arp]: 'Arp', [B.glo]: 'Glo', [B.octm]: 'Oct-', [B.octp]: 'Oct+' };
  const kb = $('kb');
  for (const [cls, codes] of ROWS) {
    const row = document.createElement('div'); row.className = 'kb-row ' + cls;
    for (const code of codes) {
      const c = document.createElement('div'), id = KEYS[code], tn = TURNS[code];
      let kind = 'off', what = '';
      if (id >= 14) { kind = WHITE.includes(id - 14) ? 'white' : 'black'; what = NOTE(id - 14); }
      else if (id !== undefined) { kind = 'fn'; what = FN[id]; }
      else if (tn) { kind = 'fn'; what = (tn[0] === 'select' ? 'Select' : 'Presets') + (tn[1] > 0 ? '+' : '-'); }
      c.className = 'kc ' + kind + (code.startsWith('Shift') || code === 'Enter' ? ' wide' : '') + (code === 'Space' ? ' space' : '');
      c.innerHTML = `<b></b><i></i>`; c.firstChild.textContent = LABEL[code] || CAP(code); c.lastChild.textContent = what;
      row.append(c);
    }
    kb.append(row);
  }
}

function say(id, text, tone = '') {
  const el = $(id); el.hidden = !text; el.textContent = text;
  el.classList.toggle('tone-good', tone === 'good'); el.classList.toggle('tone-warn', tone === 'warn'); el.classList.toggle('tone-bad', tone === 'bad');
}

// ------------------------------------------------------------------ the flat FM-1 (no WebGL): the same layout, from above
function flatFM1(stage, screen) {
  const box = document.createElement('div'); box.className = 'flat';
  box.innerHTML = '<div class="body"></div>';
  const at = (el, px, py) => { el.style.left = ((px - 80) / 840 * 100) + '%'; el.style.top = ((py - 250) / 500 * 100) + '%'; el.classList.add('part'); box.append(el); return el; };
  const s = at(document.createElement('div'), LCDC[0], LCDC[1]); s.className += ' screen'; s.append(screen);
  const parts = [], latched = new Set(), knobs = {};
  for (const [name, [px, py]] of Object.entries(KPOS)) {
    const k = at(document.createElement('div'), px, py); k.className += ' knob'; k.innerHTML = '<i></i><b></b>';
    k.querySelector('b').textContent = { master: 'MASTER', select: 'SELECT', presets: 'PRESETS', algo: 'ALGORITHM' }[name] || 'KNOB ' + name[1];
    k.style.setProperty('--k', { k1: '#5a8ad0', k2: '#7ab04e', k3: '#f0c860', k4: '#e08a3a', master: '#e0a838' }[name] || '#e8e8ee');
    knobs[name] = { el: k, ang: 0 };
    let y0 = null, acc = 0;
    const step = (s) => { if (name === 'master') masterTurn(s); else { knobs[name].ang += s * 15; k.style.transform = `translate(-50%, -50%) rotate(${knobs[name].ang}deg)`; turn(name, s); } };
    k.addEventListener('pointerdown', (e) => { y0 = e.clientY; acc = 0; k.setPointerCapture(e.pointerId); e.preventDefault(); });
    k.addEventListener('pointermove', (e) => { if (y0 === null) return; acc += y0 - e.clientY; y0 = e.clientY; while (Math.abs(acc) >= 8) { step(Math.sign(acc)); acc -= Math.sign(acc) * 8; } });
    k.addEventListener('pointerup', () => { y0 = null; });
    k.addEventListener('wheel', (e) => { e.preventDefault(); step(e.deltaY < 0 ? 1 : -1); }, { passive: false });
  }
  const pressable = (el, id) => {
    parts[id] = el;
    const acts = new Map();
    el.addEventListener('pointerdown', (e) => {
      e.preventDefault(); el.setPointerCapture(e.pointerId);
      if (id < 14 && (e.button === 2 || latched.has(id))) { if (latched.delete(id)) hold(id, false); else { latched.add(id); hold(id, true); } paint(id); return; }
      acts.set(e.pointerId, 1); hold(id, true); el.classList.add('down');
    });
    const up = (e) => { if (!acts.delete(e.pointerId)) return; hold(id, false); paint(id); };
    el.addEventListener('pointerup', up); el.addEventListener('pointercancel', up);
    el.addEventListener('contextmenu', (e) => e.preventDefault());
  };
  const paint = (id) => { const el = parts[id]; if (el) el.classList.toggle('down', latched.has(id) || holds[id] > 0); };
  BUTTONS.forEach(([name, [px, py], text], id) => {
    const b = at(document.createElement('button'), px, py); b.type = 'button'; b.className += ' btnk' + (name.startsWith('oct') ? ' oct' : '') + (name === 'rec' ? ' rec' : '');
    b.textContent = text; b.setAttribute('aria-label', text.replace('\n', ' ')); pressable(b, id);
  });
  const UPX = [167, 213, 262, 357, 405, 500, 548, 595, 690, 737, 835];
  WHITE.forEach((n, i) => { const k = at(document.createElement('button'), 145 + 47.53 * i, 668); k.type = 'button'; k.className += ' key'; k.setAttribute('aria-label', 'key ' + (n + 1)); pressable(k, 14 + n); });
  BLACK.forEach((n, j) => { const k = at(document.createElement('button'), UPX[j], 587); k.type = 'button'; k.className += ' key black'; k.setAttribute('aria-label', 'key ' + (n + 1)); pressable(k, 14 + n); });
  stage.append(box);
  let hints = null;
  return {
    leds(lv) { lv.forEach((v, id) => { const el = parts[id]; if (el) el.style.setProperty('--lv', v === 3 ? 1 : v === 2 ? .3 : v === 1 ? .12 : 0); }); },
    down(id, on) { const el = parts[id]; if (el) el.classList.toggle('down', on || latched.has(id)); },
    unlatchAll() { for (const id of latched) hold(id, false); latched.clear(); },
    master(v) { knobs.master.el.style.transform = `translate(-50%, -50%) rotate(${(v / 1023 - .5) * 270}deg)`; },
    view() {},
    hints(map) { hints = map; parts.forEach((el, id) => { if (el && id >= 14) el.textContent = hints ? hints[id] || '' : ''; }); },
  };
}
