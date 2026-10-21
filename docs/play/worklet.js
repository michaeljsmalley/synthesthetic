// SPDX-License-Identifier: GPL-3.0-only
// The Play page's audio thread is the FM-1: Synthesthetic's firmware (synthesthetic.wasm, firmware/host/web.c) runs
// here as on the FM-1's first core. The sound a block at a time; every 16 ms of it a pass of the main loop, whose
// screen changes and LEDs go to the page; every 33 ms a webcam frame while the page shows the webcam; once a second
// the flash image, if it changed, for the page to keep (IndexedDB). The page sends the panel, the knobs, MIDI and
// the webcam's settings. Every 5 s the project goes into the flash image as autosave keeps it (the FM-1 waits for a
// stop to save; a page can close while it plays). FM1Engine runs in Node too (tests/web_test.mjs).
const UI_FRAMES = 706, CAM_FRAMES = 1470, SAVE_FRAMES = 44100, FLUSH_FRAMES = 5 * 44100;   // 16 ms, 33 ms, 1 s, 5 s

export class FM1Engine {
  constructor(wasm, flash, post, song) {
    const m = wasm instanceof WebAssembly.Module ? wasm : new WebAssembly.Module(wasm);
    this.w = new WebAssembly.Instance(m, {}).exports;
    this.w._initialize?.();
    const buf = this.w.memory.buffer;                 // (fixed: the module's memory does not grow)
    this.flash = new Uint8Array(buf, this.w.web_flash_ptr(), this.w.web_flash_size());
    const fresh = !(flash && flash.byteLength === this.flash.length);
    if (fresh) this.w.web_flash_erase(); else this.flash.set(new Uint8Array(flash));
    this.w.web_boot(fresh ? 15 : 0);                  // a first visit: the demo song in all four tracks
    if (!fresh && song !== undefined) this.w.web_songmode(song ? 1 : 0);   // (song mode as the visitor left it)
    this.audio = new Float32Array(buf, this.w.web_audio_ptr(), 1024);
    this.lcd = new Uint16Array(buf, this.w.web_lcd_ptr(), 240 * 240);
    this.camPx = new Uint8Array(buf, this.w.web_cam_ptr(), 180 * 180);
    this.title = new Uint8Array(buf, this.w.web_title_ptr(), 20);
    this.post = post;
    this.tUi = 0; this.tCam = 0; this.tSave = 0; this.tFlush = 0; this.camOn = true;
    this.leds = new Uint8Array(41).fill(255); this.state = '';
    this.gen = this.w.web_flash_gen();
    post({ t: 'ready', fresh });
  }

  message(m) {
    const w = this.w;
    switch (m.t) {
      case 'input': w.web_input(m.notes >>> 0, m.buttons >>> 0); break;
      case 'enc': w.web_enc(m.e, m.s); break;
      case 'master': w.web_master(m.v); break;
      case 'midi': w.web_midi(m.p >>> 0); break;
      case 'vj': w.web_vj(m.k, m.v); break;
      case 'title': {
        const b = new TextEncoder().encode(String(m.s || '').toUpperCase().replace(/[^\x20-\x7E]/g, '?').slice(0, 19));
        this.title.fill(0); this.title.set(b); w.web_title(b.length); break;
      }
      case 'cam': this.camOn = !!m.on; break;
      case 'flush': w.web_flush(); this.save(false); break;   // the page is going away: keep everything now
      case 'stop': this.stopped = true; break;                 // (Start over: a new FM-1 takes this one's place)
    }
  }

  render(L, R) {
    const n = this.w.web_render(L.length), a = this.audio;
    for (let i = 0; i < n; i++) { L[i] = a[2 * i]; R[i] = a[2 * i + 1]; }
    this.tUi += n; this.tSave += n; this.tFlush += n;
    this.tCam = this.camOn ? this.tCam + n : 0;
    if (this.tUi >= UI_FRAMES) { this.tUi -= UI_FRAMES; this.frame(); }
    if (this.tCam >= CAM_FRAMES) { this.tCam -= CAM_FRAMES; this.camFrame(); }
    if (this.tFlush >= FLUSH_FRAMES) { this.tFlush = 0; this.w.web_flush(); }   // (unchanged: nothing written)
    if (this.tSave >= SAVE_FRAMES) { this.tSave = 0; this.save(false); }
  }

  frame() {                                           // a pass of the main loop; what changed goes to the page
    const w = this.w;
    w.web_frame();
    const d = w.web_lcd_dirty() >>> 0;
    if (d !== 0xFFFFFFFF) {
      const x0 = d & 255, y0 = (d >>> 8) & 255, cw = ((d >>> 16) & 255) + 1, ch = (d >>> 24) + 1, px = new Uint16Array(cw * ch);
      for (let y = 0; y < ch; y++) px.set(this.lcd.subarray((y0 + y) * 240 + x0, (y0 + y) * 240 + x0 + cw), y * cw);
      this.post({ t: 'lcd', x0, y0, w: cw, h: ch, px }, [px.buffer]);
    }
    let lit = false;
    for (let id = 0; id < 41; id++) { const v = w.web_led(id); if (v !== this.leds[id]) { this.leds[id] = v; lit = true; } }
    if (lit) this.post({ t: 'leds', leds: this.leds.slice() });
    const s = [w.web_playing(), w.web_vj_get(0), w.web_vj_get(1), w.web_vj_get(2), w.web_vj_get(3), w.web_vj_get(4),
               w.web_songmode(-1)];
    const key = s.join();
    if (key !== this.state) {
      this.state = key;
      this.post({ t: 'state', playing: s[0], mode: s[1], palette: s[2], autopilot: s[3], overlay: s[4], visualiser: s[5], song: s[6] });
    }
  }

  camFrame() {                                        // the webcam's picture: 160 x 90 (SCREEN: the display's copy, 180 x 180)
    const screen = this.w.web_cam();
    if (screen > 1) return;
    const px = this.camPx.slice(0, screen ? 180 * 180 : 160 * 90);
    const rgb = new Uint8Array(this.w.memory.buffer, this.w.web_cam_rgb_ptr(), 768).slice();
    this.post({ t: 'cam', screen, px, rgb }, [px.buffer, rgb.buffer]);
  }

  save(force) {                                       // the flash image, when the firmware wrote to it
    const gen = this.w.web_flash_gen();
    if (!force && gen === this.gen) return;
    this.gen = gen;
    const data = this.flash.slice().buffer;
    this.post({ t: 'flash', data }, [data]);
  }
}

if (typeof registerProcessor === 'function') {
  registerProcessor('synthesthetic', class extends AudioWorkletProcessor {
    constructor(options) {
      super();
      const post = (m, tr) => this.port.postMessage(m, tr || []);
      try {
        const o = options.processorOptions;
        this.e = new FM1Engine(o.wasm, o.flash, post, o.song);
      } catch (err) {
        post({ t: 'error', message: String((err && err.message) || err) });
      }
      this.port.onmessage = ({ data }) => { if (this.e) this.e.message(data); };
    }
    process(_inputs, outputs) {
      const out = outputs[0];
      if (this.e && this.e.stopped) return false;
      if (this.e && out && out[0]) this.e.render(out[0], out[1] || out[0]);
      return true;
    }
  });
}
