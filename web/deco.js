// SPDX-License-Identifier: GPL-3.0-only
/* Synthesthetic's site: the decorative visualisers drawn between the sections (web/make_site.py inlines this into
   the installer and the handbook). Each <canvas class="deco" data-style="..."> draws one kind of audio picture from
   a made-up signal that moves like music: layered sines, mirrored equaliser bars, a dotted waveform, area traces,
   spikes and contour rings. Only the ones on screen animate; with reduced motion each is drawn once, still. */
(() => {
  if (!window.matchMedia || !window.HTMLCanvasElement) return;
  const canvases = [...document.querySelectorAll("canvas.deco")];
  if (!canvases.length) return;
  const still = matchMedia("(prefers-reduced-motion: reduce)").matches;
  const HUES = ["#ff4d8d", "#ff9f1c", "#ffd23f", "#7ae582", "#22d3c5", "#4d9bff", "#a77bff"];
  /* the "music": bumps drifting along, a kick every half second, a little shimmer */
  const env = (x, t) => {
    let e = 0.08;
    for (let i = 0; i < 4; i++) {
      const c = (0.12 + i * 0.27 + 0.05 * Math.sin(t * (0.21 + i * 0.07) + i)) % 1, w = 0.05 + 0.03 * i;
      e += (0.55 + 0.45 * Math.sin(t * (0.9 + i * 0.4) + i * 2)) * Math.exp(-(((x - c) / w) ** 2));
    }
    const kick = Math.exp(-((t * 2) % 1) * 5);
    return Math.min(1.25, e * (0.75 + 0.35 * kick));
  };
  const jitter = (i, t) => 0.55 + 0.45 * Math.sin(i * 12.9898 + t * (3 + (i % 7))) * Math.sin(i * 4.1414 + t * 1.7);
  const rainbow = (ctx, w) => {
    const g = ctx.createLinearGradient(0, 0, w, 0);
    HUES.forEach((c, i) => g.addColorStop(i / (HUES.length - 1), c));
    return g;
  };
  const DRAW = {
    sines(ctx, w, h, t) {                              /* nested sine bundles across the spectrum */
      const mid = h / 2, n = 14;
      ctx.strokeStyle = rainbow(ctx, w);
      ctx.lineWidth = 1.2;
      for (let k = 1; k <= n; k++) {
        const s = k / n;
        ctx.globalAlpha = 0.25 + 0.6 * s;
        ctx.beginPath();
        for (let x = 0; x <= w; x += 3) {
          const u = x / w;
          const y = mid + env(u, t) * s * (h * 0.42) * Math.sin(u * 46 - t * 2.2);
          if (x) ctx.lineTo(x, y); else ctx.moveTo(x, y);
        }
        ctx.stroke();
      }
      ctx.globalAlpha = 0.5;
      ctx.beginPath(); ctx.moveTo(0, mid); ctx.lineTo(w, mid); ctx.stroke();
    },
    bars(ctx, w, h, t) {                               /* mirrored equaliser bars, violet tips to a warm middle */
      const mid = h / 2, step = 7;
      const g = ctx.createLinearGradient(0, 0, 0, h);
      ["#a77bff", "#22d3c5", "#7ae582", "#ffd23f", "#ff9f1c", "#7ae582", "#22d3c5", "#a77bff"].forEach((c, i, a) => g.addColorStop(i / (a.length - 1), c));
      ctx.fillStyle = g;
      for (let i = 0, x = 0; x < w; i++, x += step) {
        const a = Math.max(2, env(x / w, t) * jitter(i, t) * h * 0.46);
        ctx.fillRect(x, mid - a, step - 3, a * 2);
      }
      ctx.fillStyle = "rgba(255,255,255,.55)";
      ctx.fillRect(0, mid - 0.5, w, 1);
    },
    dots(ctx, w, h, t) {                               /* a dotted waveform, amber */
      const mid = h / 2, step = 7;
      ctx.fillStyle = "#ff9f1c";
      for (let i = 0, x = 3; x < w; i++, x += step) {
        const a = env(x / w, t) * jitter(i, t * 0.8) * h * 0.46;
        for (let y = 0; y <= a; y += 6) {
          ctx.globalAlpha = 1 - (y / (a + 1)) * 0.7;
          ctx.beginPath(); ctx.arc(x, mid - y, 1.6, 0, 6.283); ctx.arc(x, mid + y, 1.6, 0, 6.283); ctx.fill();
        }
      }
    },
    area(ctx, w, h, t) {                               /* two traces, teal over magenta, filled to the line */
      const layer = (col, ph, scale) => {
        const pts = [];
        for (let x = 0; x <= w; x += 18) {
          const u = x / w;
          pts.push([x, h * 0.92 - (0.2 + 0.55 * env(u, t + ph) * (0.6 + 0.4 * Math.sin(u * 23 + t + ph))) * h * scale]);
        }
        const g = ctx.createLinearGradient(0, 0, 0, h);
        g.addColorStop(0, col + "cc"); g.addColorStop(1, col + "10");
        ctx.beginPath(); ctx.moveTo(0, h);
        pts.forEach(([x, y]) => ctx.lineTo(x, y));
        ctx.lineTo(w, h); ctx.closePath(); ctx.fillStyle = g; ctx.globalAlpha = 0.8; ctx.fill();
        ctx.beginPath(); pts.forEach(([x, y], k) => (k ? ctx.lineTo(x, y) : ctx.moveTo(x, y)));
        ctx.globalAlpha = 1; ctx.strokeStyle = col; ctx.lineWidth = 1.5; ctx.stroke();
      };
      layer("#ff2e7e", 1.7, 0.75);
      layer("#22d3c5", 0, 0.95);
    },
    spikes(ctx, w, h, t) {                             /* thin lines, pink, up and down from the middle */
      const mid = h / 2, step = 5;
      ctx.strokeStyle = "#ff5fa2";
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      for (let i = 0, x = 2; x < w; i++, x += step) {
        const a = env(x / w, t * 1.1) * jitter(i * 3, t) * h * 0.47;
        ctx.moveTo(x, mid - a); ctx.lineTo(x, mid + a);
      }
      ctx.stroke();
    },
    rings(ctx, w, h, t) {                              /* contour rings breathing, yellow to red inside */
      const cx = w / 2, cy = h / 2, R = Math.min(w, h) * 0.46, n = 9;
      for (let k = n; k >= 1; k--) {
        const s = k / n;
        ctx.strokeStyle = HUES[Math.min(HUES.length - 1, Math.round((1 - s) * 3))];
        ctx.globalAlpha = 0.35 + 0.65 * s;
        ctx.lineWidth = 1.6;
        ctx.beginPath();
        for (let a = 0; a <= 6.3; a += 0.06) {
          const r = R * s * (0.8 + 0.12 * Math.sin(a * 3 + t * 0.8 + k * 0.3) + 0.08 * Math.sin(a * 5 - t * 1.1));
          const x = cx + r * Math.cos(a), y = cy + r * Math.sin(a);
          if (a) ctx.lineTo(x, y); else ctx.moveTo(x, y);
        }
        ctx.closePath();
        ctx.stroke();
      }
    },
  };
  const live = new Set();
  const fit = (c) => {
    const r = c.getBoundingClientRect(), d = Math.min(2, window.devicePixelRatio || 1);
    c.width = Math.max(1, Math.round(r.width * d));
    c.height = Math.max(1, Math.round(r.height * d));
    c._d = d;
  };
  const paint = (c, t) => {
    const ctx = c.getContext("2d"), f = DRAW[c.dataset.style];
    if (!ctx || !f) return;
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, c.width, c.height);
    ctx.setTransform(c._d, 0, 0, c._d, 0, 0);
    ctx.globalAlpha = 1;
    f(ctx, c.width / c._d, c.height / c._d, t + (Number(c.dataset.seed) || 0));
  };
  canvases.forEach((c) => { fit(c); paint(c, 0); });
  window.addEventListener("resize", () => canvases.forEach((c) => { fit(c); paint(c, performance.now() / 1000); }));
  if (still) return;
  if (window.IntersectionObserver) {
    const io = new IntersectionObserver((es) => es.forEach((e) => (e.isIntersecting ? live.add(e.target) : live.delete(e.target))));
    canvases.forEach((c) => io.observe(c));
  } else canvases.forEach((c) => live.add(c));
  let last = 0;
  const tick = (ms) => {
    if (ms - last > 33) {                              /* about 30 frames a second, like the FM-1's webcam */
      last = ms;
      live.forEach((c) => paint(c, ms / 1000));
    }
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
})();
