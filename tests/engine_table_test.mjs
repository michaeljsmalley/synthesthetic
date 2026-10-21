// SPDX-License-Identifier: GPL-3.0-only
// The editor's simulated FM-1 (web/editor.html makeMockDevice) against the firmware's engine table
// (tests/engine_dump.c): SAMPLE and GRAIN must match it exactly (names, ranges, value names, presets), because
// Synthesthetic's curated sample sets (tools/gen_samples.py: no PIANO, FLUTE or KIT) moved every set and the
// editor's audio preview plays the firmware's. The other engines' preset lists in the mock are SLOOP's older ones
// (a real FM-1 sends its own names): counted here, not failed.
//   node tests/engine_table_test.mjs build/host/engine_dump
import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import vm from 'node:vm';

const fw = JSON.parse(execFileSync(process.argv[2] || 'build/host/engine_dump', { encoding: 'utf8' }));
const html = readFileSync(new URL('../web/editor.html', import.meta.url), 'utf8');
const proto = html.slice(html.indexOf('/*PROTO-BEGIN*/'), html.indexOf('/*PROTO-END*/'));
const E = vm.runInNewContext(proto + ';({makeMockDevice})',
  { setTimeout, clearTimeout, setInterval, clearInterval, console, TextEncoder, TextDecoder, Blob, Response, DecompressionStream });
const m = E.makeMockDevice({ auto: false });
m.stop();
const js = JSON.stringify;
let fails = 0, drift = [];
function check(ok, what) { console.log(`engines: ${what.padEnd(72)} ${ok ? 'ok' : 'FAIL'}`); fails += !ok; }
check(fw.length === m.eng.length && fw.every((e, i) => e.name === m.eng[i].name), `the same ${fw.length} engines, in the same order`);
for (const [i, a] of fw.entries()) {
  const b = m.eng[i];
  const edit = (p) => js(p.map((q) => [q.label, q.min, q.max, q.def, q.names && q.names.length ? q.names : []]));
  const presets = (p) => js(p.map((q) => [q.name, Array.from(q.e), Array.from(q.env), q.mono]));
  const same = js(a.titles) === js(b.titles) && edit(a.edit) === edit(b.edit) && presets(a.presets) === presets(b.presets);
  if (a.name === 'SAMPLE' || a.name === 'GRAIN')
    check(same, `${a.name}: titles, parameters, value names and ${a.presets.length} presets as the firmware`);
  else if (!same) drift.push(a.name);
}
console.log(`engines: the mock's older SLOOP lists (not checked): ${drift.join(', ') || 'none'}`);
if (fails) { console.log(`engines: ${fails} FAILED`); process.exit(1); }
console.log('engine table test passed');
