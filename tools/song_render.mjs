// SPDX-License-Identifier: GPL-3.0-only
// The first visit's song (tools/songs/, tools/build_web.py --song) rendered by the firmware in the browser build, as
// the Play page plays it: the boot, PLAY, the song once through (song mode) and a tail. Writes the mix and, with
// --stems, each track alone (the song written with only that track: web_boot's mask) as WAV, AAC (.m4a, macOS
// afconvert) when there, and NAME.json: the sections' start times and each render's level per section.
//   node tools/song_render.mjs build/x.wasm OUTDIR [--name x] [--stems] [--seconds N]
import fs from 'node:fs';
import { execFileSync } from 'node:child_process';
import path from 'node:path';

const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); return i < 0 ? d : args[i + 1]; };
const [wasmPath, outDir] = args;
if (!wasmPath || !outDir) { console.log('usage: node tools/song_render.mjs SONG.wasm OUTDIR [--name x] [--stems] [--seconds N]'); process.exit(2); }
const name = opt('--name', path.basename(wasmPath, '.wasm'));
const limit = Number(opt('--seconds', 0));
const module = new WebAssembly.Module(fs.readFileSync(wasmPath));
const info = (() => { try { return JSON.parse(fs.readFileSync(wasmPath.replace(/\.wasm$/, '.song.json'), 'utf8')); } catch { return null; } })();
const FS = 44100, BLOCK = 128;
fs.mkdirSync(outDir, { recursive: true });

function render(mask) {
  const w = new WebAssembly.Instance(module, {}).exports;
  w._initialize?.();
  w.web_flash_erase();
  w.web_boot(mask);
  let blocks = 0;
  const step = (n, sink) => {
    for (let i = 0; i < n; i++) {
      const got = w.web_render(BLOCK);
      if (sink) sink(new Float32Array(w.memory.buffer, w.web_audio_ptr(), 2 * got));
      if (++blocks % 5 === 0) w.web_frame();
    }
  };
  step(Math.round(4.5 * FS / BLOCK));                     // the boot screen
  w.web_input(0, 1 << 12); step(Math.round(0.06 * FS / BLOCK)); w.web_input(0, 0);   // PLAY
  const pcm = [], marks = [];
  let n = 0, last = -1, wrapped = false, tail = -1;
  const once = limit || (info ? info.seconds : 0);            // (the song once through: its length, or the wrap)
  const maxBlocks = Math.round((limit || (info ? info.seconds + 1.5 : 600)) * FS / BLOCK);
  for (let b = 0; b < maxBlocks; b++) {
    step(1, (a) => { pcm.push(Float32Array.from(a)); n += a.length / 2; });
    const s = w.web_song();
    const idx = s & 256 ? (s >> 4) & 15 : -1;
    if (idx !== last) {
      if (last >= 0 && idx === 0 && !limit) wrapped = true;
      if (!wrapped) marks.push({ index: idx, section: 'ABCD'[s & 15] ?? '-', at: n / FS });
      last = idx;
    }
    if (!once && (wrapped || !w.web_playing()) && tail < 0) tail = b + Math.round(1.5 * FS / BLOCK);
    if (tail >= 0 && b >= tail) break;
  }
  const all = new Float32Array(pcm.reduce((s, a) => s + a.length, 0));
  let o = 0;
  for (const a of pcm) { all.set(a, o); o += a.length; }
  return { all, marks };
}

function wav(file, f) {
  const b = Buffer.alloc(44 + f.length * 2);
  b.write('RIFF', 0); b.writeUInt32LE(36 + f.length * 2, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16);
  b.writeUInt16LE(1, 20); b.writeUInt16LE(2, 22); b.writeUInt32LE(FS, 24); b.writeUInt32LE(FS * 4, 28);
  b.writeUInt16LE(4, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(f.length * 2, 40);
  for (let i = 0; i < f.length; i++) b.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(f[i] * 32767))), 44 + 2 * i);
  fs.writeFileSync(file, b);
}
function m4a(file) {
  try { execFileSync('afconvert', ['-f', 'm4af', '-d', 'aac', '-b', '256000', file, file.replace(/\.wav$/, '.m4a')]); return true; }
  catch { return false; }
}
const db = (x) => (x > 0 ? 20 * Math.log10(x) : -120);
function levels(f, marks) {        // RMS and peak (dBFS) of each section of the song, and of the whole
  const cuts = marks.map((m) => Math.round(m.at * FS)).concat([f.length / 2]);
  const at = (a, b) => { let s = 0, p = 0; for (let i = 2 * a; i < 2 * b; i++) { s += f[i] * f[i]; p = Math.max(p, Math.abs(f[i])); } return { rms: +db(Math.sqrt(s / Math.max(1, 2 * (b - a)))).toFixed(1), peak: +db(p).toFixed(1) }; };
  return { all: at(0, f.length / 2), sections: marks.map((m, i) => ({ ...m, ...at(cuts[i], cuts[i + 1]) })) };
}

const renders = [['mix', 15]];
if (args.includes('--stems')) renders.push(['bass', 1], ['keys', 2], ['lead', 4], ['drums', 8]);
const report = { name, renders: {} };
for (const [part, mask] of renders) {
  const { all, marks } = render(mask);
  const file = path.join(outDir, `${name}-${part}.wav`);
  wav(file, all);
  const aac = m4a(file);
  report.renders[part] = { file: path.basename(file), m4a: aac ? path.basename(file).replace(/\.wav$/, '.m4a') : null, seconds: +(all.length / 2 / FS).toFixed(2), ...levels(all, marks) };
  if (part === 'mix') report.sections = marks;
  const l = report.renders[part];
  console.log(`${name} ${part.padEnd(5)} ${l.seconds.toFixed(1)} s  rms ${l.all.rms} dBFS  peak ${l.all.peak} dBFS  ` +
              l.sections.map((s) => `${s.section}${s.index} ${s.rms}`).join('  '));
}
fs.writeFileSync(path.join(outDir, `${name}.json`), JSON.stringify(report, null, 1));
