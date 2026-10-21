// SPDX-License-Identifier: GPL-3.0-only
// The FM-1 in 3D (three.js r128), for the Play page. Adapted from FM1.quest's faceplate by hericdk
// (github.com/hericdk/fm1-quest, page/sim-src/lab-3d.js, GPL-3.0): its layout measured from a top-down photo of the
// FM-1 (1 world unit = 100 px of that photo, origin at the body's centre, +Z up from the plate), its materials and
// its key light. Here: driven by Synthesthetic's firmware (play.js), the screen a 240 x 240 canvas pinned onto the
// glass with a CSS homography (sharp at any angle), several pointers at once (hold SEQ, tap the keys), a held button
// latched by a right click or a long press, the computer keys printed on the caps on request.
//
//   const fm1 = createFM1(stage, { screen, onButton(id, down), onKey(n, down), onKnob(id, steps), onMaster(v) })
//     -> null without WebGL (the page shows the flat panel instead)
//   fm1.leds(levels)      41 levels by key id (0..13 buttons, 14..40 keys): 0 off, 1 backlight, 2 glow, 3 lit
//   fm1.down(id, on)      a key or button held from elsewhere (the computer keyboard, MIDI): it goes down
//   fm1.view(name)        'float' | 'still' | 'top' | 'screen'
//   fm1.hints(map|null)   key id -> the computer key to print on its cap
const T = globalThis.THREE;

// ---------------------------------------------------------------- the measured layout (photo px)
export const KPOS = { master: [147, 327], select: [237, 327], presets: [147, 415], algo: [237, 415], k1: [553, 327], k2: [647, 327], k3: [742, 327], k4: [838, 327] };
const KCOL = { k1: '#5a8ad0', k2: '#7ab04e', k3: '#f0c860', k4: '#e08a3a', master: '#e0a838' };
const KLAB = { master: 'MASTER', select: 'SELECT', presets: 'PRESETS', algo: 'ALGORITHM', k1: 'KNOB 1', k2: 'KNOB 2', k3: 'KNOB 3', k4: 'KNOB 4' };
// buttons by matrix id (firmware/src/panel.c PANEL_DEFAULT): their place, their print
export const BUTTONS = [
  ['octm', [160, 483], 'OCT −'], ['octp', [226, 483], 'OCT +'], ['fx', [565, 420], 'FX'], ['sel', [618, 420], 'SEL'],
  ['env', [670, 420], 'ENV'], ['lfo', [723, 420], 'LFO'], ['edit', [775, 420], 'EDIT'], ['glo', [829, 420], 'GLO'],
  ['home', [565, 472], 'HOME'], ['save', [618, 472], 'SAVE'], ['arp', [670, 472], 'ARP'], ['seq', [723, 472], 'SEQ'],
  ['play', [775, 472], 'PLAY\nSTOP'], ['rec', [829, 472], 'REC']];
const UPX = [167, 213, 262, 357, 405, 500, 548, 595, 690, 737, 835], UPY = 587, LOWY = 668, lowX = i => 145 + 47.53 * i;
export const WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];   // key n (0 = F3) of white key i
export const BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25];
export const LCDC = [395, 392];       // the screen's centre; the model's is 15 % larger than the real one, for legibility
const W = 8.4, H = 5.0, TOP = .3, BT = .05, GL = 1.61;
const P = (x, y) => [(x - 500) / 100, (500 - y) / 100];
const clamp = (v, a, b) => Math.max(a, Math.min(b, v)), lerp = (a, b, t) => a + (b - a) * t;
const FONT = '"FM-1 Terminus", ui-monospace, Menlo, monospace';

export function createFM1(stage, opt) {
  if (!T) return null;
  let renderer;
  try { renderer = new T.WebGLRenderer({ antialias: true, alpha: true, powerPreference: 'high-performance' }); } catch (e) { return null; }
  if (!renderer.getContext()) return null;
  const C = hex => new T.Color(hex).convertSRGBToLinear();     // (r128 does not colour-manage hex: dark stays dark)
  const cvs = (w, h) => { const c = document.createElement('canvas'); c.width = w; c.height = h; return c; };
  const mkTex = c => { const t = new T.CanvasTexture(c); t.encoding = T.sRGBEncoding; t.anisotropy = renderer.capabilities.getMaxAnisotropy(); return t; };
  const redraws = [];                                           // canvases to draw again once the font is in

  // ---------------------------------------------------------------- the stage: the canvas, the screen on the glass
  const cv = renderer.domElement; cv.className = 'fm1-gl'; stage.append(cv);
  const scr = document.createElement('div'); scr.className = 'fm1-glass'; scr.append(opt.screen); stage.append(scr);
  renderer.setPixelRatio(Math.min(devicePixelRatio || 1, 2)); renderer.outputEncoding = T.sRGBEncoding;
  renderer.toneMapping = T.ACESFilmicToneMapping; renderer.toneMappingExposure = 1.3; renderer.setClearColor(0x000000, 0);
  const scene = new T.Scene(), camera = new T.PerspectiveCamera(24, 2, .5, 120);
  scene.add(new T.HemisphereLight(0xc8d0ff, 0x1c1620, .5));
  const key = new T.DirectionalLight(0xfff2e0, 1.15); key.position.set(-5, 7, 9); scene.add(key);
  const rim = new T.DirectionalLight(0xff9fd0, .45); rim.position.set(7, -3, 4); scene.add(rim);   // (the site's plum)
  (function env() {                                             // a studio of soft boxes, for the gloss
    const c = cvs(512, 256), x = c.getContext('2d'), g = x.createLinearGradient(0, 0, 0, 256);
    g.addColorStop(0, '#7a6a96'); g.addColorStop(.42, '#211a2a'); g.addColorStop(1, '#070509'); x.fillStyle = g; x.fillRect(0, 0, 512, 256);
    x.fillStyle = '#fff'; x.globalAlpha = .85; x.fillRect(60, 46, 190, 22); x.fillRect(300, 74, 150, 14); x.globalAlpha = .55; x.fillRect(120, 100, 260, 6);
    x.globalAlpha = .35; x.fillRect(200, 18, 110, 10); x.fillStyle = '#ff4d8d'; x.globalAlpha = .35; x.fillRect(430, 40, 60, 90);
    const t = new T.CanvasTexture(c); t.mapping = T.EquirectangularReflectionMapping; t.encoding = T.sRGBEncoding;
    const pm = new T.PMREMGenerator(renderer); scene.environment = pm.fromEquirectangular(t).texture; pm.dispose();
  })();

  function rr(w, h, r) {                                        // a rounded rectangle, centred
    const s = new T.Shape(), x = -w / 2, y = -h / 2; r = Math.min(r, w / 2, h / 2);
    s.moveTo(x + r, y); s.lineTo(x + w - r, y); s.absarc(x + w - r, y + r, r, -Math.PI / 2, 0, false); s.lineTo(x + w, y + h - r);
    s.absarc(x + w - r, y + h - r, r, 0, Math.PI / 2, false); s.lineTo(x + r, y + h); s.absarc(x + r, y + h - r, r, Math.PI / 2, Math.PI, false);
    s.lineTo(x, y + r); s.absarc(x + r, y + r, r, Math.PI, Math.PI * 1.5, false); return s;
  }
  const ext = (shape, depth, b) => new T.ExtrudeGeometry(shape, { depth, bevelEnabled: true, bevelThickness: b, bevelSize: b, bevelOffset: -b, bevelSegments: 3, curveSegments: 20 });
  const dev = new T.Group(); dev.rotation.order = 'YXZ'; const root = new T.Group(); root.add(dev); scene.add(root);
  const pick = [];

  // light leaking from the gap round a key: its sides glow at the base, fading towards the dark top
  function glowMat(color, rough, h) {
    const m = new T.MeshPhysicalMaterial({ color: C(color), roughness: rough * .8, metalness: 0, clearcoat: .45, clearcoatRoughness: .42, envMapIntensity: .75 });
    const u = { uLevel: { value: 0 }, uColor: { value: C('#ffc46a') }, uH: { value: h } }; m.userData.u = u;
    m.onBeforeCompile = sh => {
      Object.assign(sh.uniforms, u);
      sh.vertexShader = sh.vertexShader.replace('#include <common>', '#include <common>\nvarying float vZ;').replace('#include <begin_vertex>', '#include <begin_vertex>\nvZ = position.z;');
      sh.fragmentShader = sh.fragmentShader.replace('#include <common>', '#include <common>\nvarying float vZ; uniform float uLevel; uniform vec3 uColor; uniform float uH;')
        .replace('#include <emissivemap_fragment>', '#include <emissivemap_fragment>\n float gk = clamp(vZ / uH, 0., 1.); totalEmissiveRadiance += uColor * uLevel * pow(1. - gk, 2.1) * 2.2;');
    };
    return m;
  }
  const haloTex = (() => { const c = cvs(128, 256), x = c.getContext('2d'); x.filter = 'blur(16px)'; x.fillStyle = '#fff'; const w = 56, h = 168, r = 28, X = 36, Y = 44;
    x.beginPath(); x.moveTo(X + r, Y); x.arcTo(X + w, Y, X + w, Y + h, r); x.arcTo(X + w, Y + h, X, Y + h, r); x.arcTo(X, Y + h, X, Y, r); x.arcTo(X, Y, X + w, Y, r); x.fill(); return new T.CanvasTexture(c); })();

  // ---------------------------------------------------------------- the body and its printed plate
  const body = new T.Mesh(ext(rr(W, H, .42), .5, BT), new T.MeshPhysicalMaterial({ color: C('#060608'), roughness: .2, metalness: .05, clearcoat: 1, clearcoatRoughness: .08, envMapIntensity: 1.25 }));
  body.position.z = -.25; dev.add(body);
  const TW = 2490, TH = 1470, K = 3, plateCv = cvs(TW, TH), plateTex = mkTex(plateCv);
  const grain = Array.from({ length: 26000 }, () => [Math.random() * TW, Math.random() * TH, Math.random() * .05]);
  function drawPlate() {
    const x = plateCv.getContext('2d'), Tp = (px, py) => [(px - 500) * K + TW / 2, (py - 500) * K + TH / 2];
    const g = x.createLinearGradient(0, 0, TW, TH); g.addColorStop(0, '#222226'); g.addColorStop(.5, '#19191c'); g.addColorStop(1, '#131316'); x.fillStyle = g; x.fillRect(0, 0, TW, TH);
    for (const [gx, gy, a] of grain) { x.fillStyle = `rgba(255,255,255,${a})`; x.fillRect(gx, gy, 2, 2); }
    const panel = (x0, y0, x1, y1, r) => { const [a, b] = Tp(x0, y0), [c, d] = Tp(x1, y1); x.save(); x.beginPath(); x.roundRect ? x.roundRect(a, b, c - a, d - b, r * K) : x.rect(a, b, c - a, d - b);
      x.fillStyle = '#060607'; x.fill(); x.lineWidth = 5; x.strokeStyle = 'rgba(255,255,255,.09)'; x.stroke(); x.clip();
      const sg = x.createLinearGradient(0, b, 0, b + 60); sg.addColorStop(0, 'rgba(0,0,0,.8)'); sg.addColorStop(1, 'rgba(0,0,0,0)'); x.fillStyle = sg; x.fillRect(a, b, c - a, 60); x.restore(); };
    panel(528, 388, 865, 505, 20); panel(125, 463, 260, 502, 14); panel(108, 535, 892, 718, 30);
    x.fillStyle = '#b9b9c4'; x.textAlign = 'center'; x.textBaseline = 'middle'; x.font = `${16 * K}px ${FONT}`;
    for (const [id, [px]] of Object.entries(KPOS)) { const [a, b] = Tp(px + (id === 'presets' || id === 'algo' ? 3 : 0), id === 'presets' || id === 'algo' ? 377 : 289); x.fillText(KLAB[id], a, b); }
    x.fillStyle = '#6a6a72'; for (const [px, py] of [[695, 442], [310, 627], [452, 627], [640, 627], [785, 627]]) { const [a, b] = Tp(px, py); x.beginPath(); x.arc(a, b, 2.2 * K, 0, 7); x.fill(); }
    plateTex.needsUpdate = true;
  }
  drawPlate(); redraws.push(drawPlate);
  { const sh = rr(W - 2 * BT, H - 2 * BT, .37), geo = new T.ShapeGeometry(sh, 24), uv = geo.attributes.uv, pos = geo.attributes.position;
    for (let i = 0; i < uv.count; i++) uv.setXY(i, pos.getX(i) / (W - 2 * BT) + .5, pos.getY(i) / (H - 2 * BT) + .5);
    const plate = new T.Mesh(geo, new T.MeshPhysicalMaterial({ map: plateTex, roughness: .34, metalness: .1, clearcoat: .7, clearcoatRoughness: .22, envMapIntensity: .9 }));
    plate.position.z = TOP + .002; dev.add(plate); }
  { const pts = rr(W - 2 * BT, H - 2 * BT, .37).getPoints(64).map(p => new T.Vector3(p.x, p.y, TOP + .006));
    dev.add(new T.LineLoop(new T.BufferGeometry().setFromPoints(pts), new T.LineBasicMaterial({ color: C('#7a7a8a'), transparent: true, opacity: .5 }))); }
  { // the front edge: the jacks, USB-C, the switch (no maker's mark: Synthesthetic is not M-VAVE's)
    const c = cvs(2532, 180), x = c.getContext('2d'), X = px => (px - 78) * 3;
    x.fillStyle = '#0c0c0f'; x.fillRect(0, 0, 2532, 180);
    x.fillStyle = '#1b1b20'; x.beginPath(); x.roundRect ? x.roundRect(X(497), 40, 879, 100, 18) : x.rect(X(497), 40, 879, 100); x.fill(); x.strokeStyle = '#34343a'; x.lineWidth = 3; x.stroke();
    x.fillStyle = '#050506'; x.beginPath(); x.arc(X(532), 92, 30, 0, 7); x.fill(); x.strokeStyle = '#3b3b42'; x.lineWidth = 6; x.stroke(); x.beginPath(); x.arc(X(596), 92, 14, 0, 7); x.fill();
    x.strokeStyle = '#8a8a92'; x.lineWidth = 5; x.beginPath(); x.roundRect ? x.roundRect(X(646), 78, 60, 28, 14) : x.rect(X(646), 78, 60, 28); x.stroke(); x.fillStyle = '#111'; x.fill();
    x.fillStyle = '#26262b'; x.beginPath(); x.roundRect ? x.roundRect(X(718), 70, 56, 44, 22) : x.rect(X(718), 70, 56, 44); x.fill(); x.fillStyle = '#aaa'; x.beginPath(); x.arc(X(736), 92, 11, 0, 7); x.fill();
    const m = new T.Mesh(new T.PlaneGeometry(8.44, .6), new T.MeshStandardMaterial({ map: mkTex(c), roughness: .4, metalness: .1 })); m.rotation.x = Math.PI / 2; m.position.set(0, -H / 2 - .001, 0); dev.add(m); }

  // ---------------------------------------------------------------- the screen's bezel and glass
  const [lx, ly] = P(LCDC[0], LCDC[1]);
  { const bez = new T.Mesh(ext(rr(2.3, 2.3, .22), .05, .03), new T.MeshPhysicalMaterial({ color: C('#030304'), roughness: .12, metalness: .3, clearcoat: 1, clearcoatRoughness: .05, envMapIntensity: 1.3 }));
    bez.position.set(lx, ly, TOP - .01); dev.add(bez);
    const glass = new T.Mesh(new T.PlaneGeometry(GL + .02, GL + .02), new T.MeshPhysicalMaterial({ color: C('#040405'), roughness: .06, metalness: 0, clearcoat: 1, clearcoatRoughness: .04 }));
    glass.position.set(lx, ly, TOP + .066); dev.add(glass);
    const pts = rr(GL + .08, GL + .08, .1).getPoints(40).map(p => new T.Vector3(p.x + lx, p.y + ly, TOP + .069));
    dev.add(new T.LineLoop(new T.BufferGeometry().setFromPoints(pts), new T.LineBasicMaterial({ color: C('#4a4a58'), transparent: true, opacity: .8 }))); }
  const corners = [[lx - GL / 2, ly + GL / 2], [lx + GL / 2, ly + GL / 2], [lx + GL / 2, ly - GL / 2], [lx - GL / 2, ly - GL / 2]].map(([x, y]) => new T.Vector3(x, y, TOP + .068));

  // ---------------------------------------------------------------- the knobs
  const grip = (() => { const c = cvs(64, 8), x = c.getContext('2d'); x.fillStyle = '#9a9aa2'; x.fillRect(0, 0, 64, 8); x.fillStyle = '#2a2a2e'; x.fillRect(0, 0, 32, 8);
    const t = new T.CanvasTexture(c); t.wrapS = T.RepeatWrapping; t.repeat.set(24, 1); return t; })();
  const knobs = {};
  for (const [id, [px, py]] of Object.entries(KPOS)) {
    const g = new T.Group(), [x, y] = P(px, py); g.position.set(x, y, TOP);
    const skirt = new T.Mesh(new T.CylinderGeometry(.215, .23, .05, 40), new T.MeshStandardMaterial({ color: C('#060607'), roughness: .6 })); skirt.rotation.x = Math.PI / 2; skirt.position.z = .025; g.add(skirt);
    const kb = new T.Mesh(new T.CylinderGeometry(.185, .195, .3, 48), new T.MeshPhysicalMaterial({ color: C('#0a0a0c'), roughness: .38, metalness: .25, bumpMap: grip, bumpScale: 2.2, clearcoat: .3, clearcoatRoughness: .4, envMapIntensity: .9 }));
    kb.rotation.x = Math.PI / 2; kb.position.z = .17; g.add(kb);
    const top = new T.Mesh(new T.CircleGeometry(.175, 48), new T.MeshPhysicalMaterial({ color: C('#101013'), roughness: .25, metalness: .3, clearcoat: .8, clearcoatRoughness: .15, envMapIntensity: 1.1 })); top.position.z = .321; g.add(top);
    const pg = new T.Group(); pg.position.z = .327; const ind = new T.Mesh(new T.BoxGeometry(.034, .15, .012), new T.MeshBasicMaterial({ color: C(KCOL[id] || '#e8e8ee') })); ind.position.y = .085; pg.add(ind); g.add(pg);
    dev.add(g); kb.userData.pick = top.userData.pick = { kind: 'knob', id }; pick.push(kb, top);
    knobs[id] = { pg, ang: id === 'master' ? 0 : 0 };
  }

  // ---------------------------------------------------------------- the buttons and keys: dark, matte, lit from below
  const lit = [];                                               // {id (key id 0..40), mat, halo, keyG, lv, ...}
  const amber = C('#ffc46a'), red = C('#ff4a38');
  function litThing(shape, depth, color, rough, gapW, gapH, px, py, o) {
    const [x, y] = P(px, py), mat = glowMat(color, rough, depth + .05), grp = new T.Group(); grp.position.set(x, y, TOP); dev.add(grp);
    const gap = new T.Mesh(new T.ShapeGeometry(rr(gapW, gapH, Math.min(gapW, gapH) / 2 * (o.pill ? 1 : .35)), 16), new T.MeshBasicMaterial({ color: C('#000') })); gap.position.z = .004; grp.add(gap);
    const halo = new T.Mesh(new T.PlaneGeometry(gapW + .5, gapH + .5), new T.MeshBasicMaterial({ map: haloTex, color: C('#000'), transparent: true, blending: T.AdditiveBlending, depthWrite: false })); halo.position.z = .006; grp.add(halo);
    const keyG = new T.Group(); keyG.position.z = -.02; grp.add(keyG); const m = new T.Mesh(ext(shape, depth, .025), mat); keyG.add(m);
    const rec = Object.assign({ mat, halo, keyG, lv: 0, top: depth + .05 - .02, x, y, held: false }, o);
    m.userData.pick = { kind: 'key', id: o.id }; pick.push(m); lit[o.id] = rec; return rec;
  }
  function capLabel(w, h, draw, z, parent) {                    // a print on a cap, drawn again with the font
    const c = cvs(256, Math.round(256 * h / w)), x = c.getContext('2d'), t = mkTex(c);
    const m = new T.Mesh(new T.PlaneGeometry(w, h), new T.MeshBasicMaterial({ map: t, transparent: true, depthWrite: false })); m.position.z = z; parent.add(m);
    const go = () => { x.clearRect(0, 0, c.width, c.height); draw(x, c.width, c.height); t.needsUpdate = true; }; go(); redraws.push(go); return go;
  }
  let hintMap = null;
  const hintDraws = [];
  BUTTONS.forEach(([name, [px, py], text], id) => {
    const oct = name.startsWith('oct'), w = oct ? .5 : .44, h = oct ? .3 : .36;
    const r = litThing(rr(w, h, .07), .09, '#0c0c0f', .5, w + .09, h + .09, px, py, { id, name, small: true, col: name === 'rec' ? red : amber, pressedZ: .035 });
    hintDraws.push(capLabel(w * .9, h * .8, (x, W2, H2) => {
      const L = text.split('\n'), hint = hintMap && hintMap[id];
      x.fillStyle = '#e4e4ea'; x.textAlign = 'center'; x.textBaseline = 'middle';
      x.font = `${L.length > 1 ? 50 : 64}px ${FONT}`;
      const y0 = hint ? H2 * .4 : H2 / 2;
      L.forEach((s, i) => x.fillText(s, W2 / 2, y0 + (i - (L.length - 1) / 2) * 52));
      if (hint) { x.fillStyle = '#ffd23f'; x.font = `44px ${FONT}`; x.fillText(hint, W2 / 2, H2 * .86); }
    }, .09 + .05 - .02 + .001, r.keyG));
  });
  WHITE.forEach((n, i) => {
    const r = litThing(rr(.43, .76, .215), .2, '#0e0e11', .6, .5, .83, lowX(i), LOWY, { id: 14 + n, col: amber, pressedZ: .045, pill: true });
    hintDraws.push(capLabel(.3, .3, (x, W2, H2) => { const s = hintMap && hintMap[14 + n]; if (!s) return; x.fillStyle = '#ffd23f'; x.textAlign = 'center'; x.textBaseline = 'middle'; x.font = `120px ${FONT}`; x.fillText(s, W2 / 2, H2 / 2); }, .2 + .05 - .02 + .001, r.keyG).bind(null));
    r.keyG.children[r.keyG.children.length - 1].position.y = -.18;
  });
  BLACK.forEach((n, j) => {
    const r = litThing(rr(.42, .77, .21), .28, '#070709', .62, .49, .84, UPX[j], UPY, { id: 14 + n, col: amber, pressedZ: .05, pill: true });
    hintDraws.push(capLabel(.3, .3, (x, W2, H2) => { const s = hintMap && hintMap[14 + n]; if (!s) return; x.fillStyle = '#ffd23f'; x.textAlign = 'center'; x.textBaseline = 'middle'; x.font = `120px ${FONT}`; x.fillText(s, W2 / 2, H2 / 2); }, .28 + .05 - .02 + .001, r.keyG));
    r.keyG.children[r.keyG.children.length - 1].position.y = -.18;
  });

  // a soft shadow on an imaginary floor
  const shadow = new T.Mesh(new T.PlaneGeometry(1, 1), new T.MeshBasicMaterial({ transparent: true, depthWrite: false, map: (() => {
    const c = cvs(256, 128), x = c.getContext('2d'), g = x.createRadialGradient(128, 64, 4, 128, 64, 120); g.addColorStop(0, 'rgba(0,0,0,.75)'); g.addColorStop(1, 'rgba(0,0,0,0)');
    x.fillStyle = g; x.save(); x.scale(1, .5); x.fillRect(0, 0, 256, 256); x.restore(); return new T.CanvasTexture(c); })() }));
  shadow.rotation.x = -Math.PI / 2; shadow.position.y = -3.4; scene.add(shadow);
  if (document.fonts) document.fonts.load(`16px ${FONT}`).then(() => redraws.forEach(f => f()), () => {});

  // ---------------------------------------------------------------- input: rays against the model, a pointer each
  const ray = new T.Raycaster(), ndc = new T.Vector2(), acts = new Map(), latched = new Set(), external = new Set();
  let hover = null, mouse = { x: 0, y: 0, in: false };
  function hit(e) {
    const r = cv.getBoundingClientRect(); ndc.set(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1);
    ray.setFromCamera(ndc, camera); const h = ray.intersectObjects(pick, false)[0]; return h ? h.object.userData.pick : null;
  }
  const isBtn = id => id < 14;
  function press(id, on) {                                      // a button or a key, from the model
    if (isBtn(id)) opt.onButton(id, on); else opt.onKey(id - 14, on);
  }
  function knobTurn(id, s) {
    if (id === 'master') { opt.onMaster(s); return; }
    knobs[id].ang += s * 15; opt.onKnob(id, s);
  }
  cv.addEventListener('pointerdown', e => {
    const h = hit(e); if (!h) return; e.preventDefault(); cv.setPointerCapture(e.pointerId);
    if (h.kind === 'knob') { acts.set(e.pointerId, { kind: 'knob', id: h.id, y0: e.clientY, acc: 0 }); return; }
    const id = h.id;
    if (isBtn(id) && (e.button === 2 || latched.has(id))) {     // a right click latches a button (held), a click lets it go
      if (latched.delete(id)) press(id, false); else { latched.add(id); press(id, true); }
      return;
    }
    const a = { kind: 'key', id, t: performance.now(), timer: 0 };
    if (isBtn(id) && e.pointerType !== 'mouse')                 // a long press on a touch screen latches it too
      a.timer = setTimeout(() => { a.timer = 0; if (acts.get(e.pointerId) === a) { acts.delete(e.pointerId); latched.add(id); if (navigator.vibrate) navigator.vibrate(12); } }, 650);
    acts.set(e.pointerId, a); press(id, true);
  });
  cv.addEventListener('pointermove', e => {
    const r = stage.getBoundingClientRect(); mouse.x = ((e.clientX - r.left) / r.width) * 2 - 1; mouse.y = ((e.clientY - r.top) / r.height) * 2 - 1; mouse.in = true;
    const a = acts.get(e.pointerId);
    if (a && a.kind === 'knob') { a.acc += a.y0 - e.clientY; a.y0 = e.clientY; while (Math.abs(a.acc) >= 8) { knobTurn(a.id, a.acc > 0 ? 1 : -1); a.acc -= Math.sign(a.acc) * 8; } return; }
    if (!a && e.pointerType === 'mouse') { hover = hit(e); cv.style.cursor = hover ? (hover.kind === 'knob' ? 'ns-resize' : 'pointer') : 'default'; }
  });
  const up = e => { const a = acts.get(e.pointerId); if (!a) return; acts.delete(e.pointerId); if (a.kind !== 'key') return; if (a.timer) clearTimeout(a.timer); press(a.id, false); };
  cv.addEventListener('pointerup', up); cv.addEventListener('pointercancel', up);
  cv.addEventListener('pointerleave', () => { mouse.in = false; hover = null; });
  cv.addEventListener('contextmenu', e => e.preventDefault());
  cv.addEventListener('wheel', e => { const h = hit(e); if (h && h.kind === 'knob') { e.preventDefault(); knobTurn(h.id, e.deltaY < 0 ? 1 : -1); } }, { passive: false });

  // ---------------------------------------------------------------- the screen on the glass: a square onto a quad
  const tmp = new T.Vector3(); let sw = 1, sh = 1;
  function homography(q) {
    const [[x0, y0], [x1, y1], [x2, y2], [x3, y3]] = q, dx1 = x1 - x2, dx2 = x3 - x2, dx3 = x0 - x1 + x2 - x3, dy1 = y1 - y2, dy2 = y3 - y2, dy3 = y0 - y1 + y2 - y3, den = dx1 * dy2 - dy1 * dx2;
    const g = (dx3 * dy2 - dy3 * dx2) / den, h = (dx1 * dy3 - dy1 * dx3) / den, a = x1 - x0 + g * x1, b = x3 - x0 + h * x3, d = y1 - y0 + g * y1, e = y3 - y0 + h * y3, s = 1 / 240;
    return `matrix3d(${a * s},${d * s},0,${g * s},${b * s},${e * s},0,${h * s},0,0,1,0,${x0},${y0},0,1)`;
  }
  const proj = v => { tmp.copy(v).applyMatrix4(dev.matrixWorld).project(camera); return [(tmp.x + 1) / 2 * sw, (1 - tmp.y) / 2 * sh]; };

  // ---------------------------------------------------------------- the view: floats and sways, or still, from above, at the screen
  const reduced = matchMedia('(prefers-reduced-motion: reduce)');
  const st = { hv: 0, tx: 0, ty: 0, zoom: 0, zt: 0, tv: 0, top: false, float: !reduced.matches }, BASE = -.6;
  let D = 16, last = performance.now(), live = true, raf = 0;
  function resize() {
    const w = Math.max(280, stage.clientWidth), h = Math.max(220, stage.clientHeight);
    sw = w; sh = h; renderer.setSize(w, h, true); camera.aspect = w / h;
    const vf = camera.fov * Math.PI / 180;           // (room for the sway and the near edge, tilted towards you)
    D = Math.max((W * 1.1 / 2) / (Math.tan(vf / 2) * camera.aspect), (H * 1.22 / 2) / Math.tan(vf / 2)); camera.updateProjectionMatrix();
  }
  new ResizeObserver(resize).observe(stage); resize();
  const scrC = new T.Vector3(lx, ly, TOP), camTgt = new T.Vector3(), camPos = new T.Vector3(), scrW = new T.Vector3(), white = new T.Color(1, 1, 1);
  let ledLv = new Uint8Array(41);
  function frame(now) {
    raf = requestAnimationFrame(frame);
    if (!live || document.hidden) { last = now; return; }
    const dt = Math.min(.05, (now - last) / 1000); last = now; const t = now / 1000, ease = k => 1 - Math.exp(-dt * k);
    st.hv += ((mouse.in ? 1 : 0) - st.hv) * ease(5); st.zoom += (st.zt - st.zoom) * ease(5.5); st.tv += ((st.top ? 1 : 0) - st.tv) * ease(5);
    const amp = st.float ? (1 - .8 * st.hv) * (1 - .65 * st.zoom) * (1 - .75 * st.tv) : 0;
    st.tx += ((-mouse.x * .16 * st.hv * (st.float ? 1 : .4)) - st.tx) * ease(7); st.ty += ((mouse.y * .1 * st.hv * (st.float ? 1 : .4)) - st.ty) * ease(7);
    dev.rotation.y = st.tx + Math.sin(t * .55) * .2 * amp + Math.sin(t * .21 + 2) * .05 * amp;
    dev.rotation.x = lerp(BASE, 0, st.tv) - st.ty * (1 - .5 * st.tv) + Math.sin(t * .43 + 1) * .05 * amp;
    dev.rotation.z = Math.sin(t * .37) * .02 * amp;
    const bob = Math.sin(t * .9) * .08 * amp; root.position.y = bob;
    shadow.scale.set(9.6 - bob * 3, 3.6 - bob, 1); shadow.material.opacity = clamp(.55 - bob * 1.2, .2, .7); shadow.position.y = -3.4 - bob * .3;
    root.updateMatrixWorld(true);
    scrW.copy(scrC).applyMatrix4(dev.matrixWorld); const e = st.zoom * st.zoom * (3 - 2 * st.zoom);
    camTgt.set(lerp(0, scrW.x, e), lerp(0, scrW.y, e), lerp(0, scrW.z, e));
    camPos.set(lerp(0, scrW.x, e), lerp(lerp(1.7, 0, st.tv), scrW.y + .9, e), lerp(D, scrW.z + D * .46, e)); camera.position.copy(camPos); camera.lookAt(camTgt);
    for (const r of lit) {
      if (!r) continue;
      const L = ledLv[r.id], down = acts.size && [...acts.values()].some(a => a.kind === 'key' && a.id === r.id) || latched.has(r.id) || external.has(r.id);
      const hov = hover && hover.kind === 'key' && hover.id === r.id ? (r.small ? .22 : .14) : 0;
      const tgt = clamp((L === 3 ? 1 : L === 2 ? .3 : L === 1 ? .12 : 0) + hov + (down ? .3 : 0), 0, 1.3);
      r.lv += (tgt - r.lv) * (tgt > r.lv ? .55 : .16);
      r.mat.userData.u.uLevel.value = r.lv * (r.small ? .8 : 1); r.mat.userData.u.uColor.value.copy(r.col);
      r.halo.material.color.copy(r.col).multiplyScalar(clamp(r.lv, 0, 1.3) * (r.small ? .55 : .75));
      r.keyG.position.z = -.02 - (down ? r.pressedZ : 0);
    }
    for (const id in knobs) knobs[id].pg.rotation.z = -knobs[id].ang * Math.PI / 180;
    renderer.render(scene, camera);
    scr.style.transform = homography(corners.map(proj)); scr.style.visibility = 'visible';
  }
  new IntersectionObserver(es => { live = es[0].isIntersecting; }).observe(stage);
  raf = requestAnimationFrame(frame);

  return {
    leds(lv) { ledLv = lv; },
    down(id, on) { if (on) external.add(id); else external.delete(id); },
    unlatchAll() { for (const id of latched) press(id, false); latched.clear(); },
    master(v) { knobs.master.ang = (v / 1023 - .5) * 270; },
    view(v) {
      st.float = v === 'float' && !reduced.matches; st.top = v === 'top'; st.zt = v === 'screen' ? 1 : 0;
    },
    hints(map) { hintMap = map; hintDraws.forEach(f => f()); },
    dispose() { cancelAnimationFrame(raf); renderer.dispose(); },
  };
}
