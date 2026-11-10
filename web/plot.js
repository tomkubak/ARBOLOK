// Malý canvas graf: časová (nebo logaritmická) osa X, až dvě osy Y, mezery v datech,
// ztenčení hustých řad a tooltip na dotek / myš. Bez knihoven.
const Plot = (function () {
  const MIN = 60e3, H = 3600e3, D = 86400e3;
  const STEPS = [MIN, 5 * MIN, 10 * MIN, 15 * MIN, 30 * MIN, H, 2 * H, 3 * H, 6 * H, 12 * H, D, 2 * D, 7 * D, 14 * D, 30 * D, 90 * D];
  const all = [];
  const pad2 = (n) => (n < 10 ? '0' : '') + n;
  const css = (n) => getComputedStyle(document.documentElement).getPropertyValue(n).trim();

  function niceStep(span, n) {
    const raw = span / Math.max(1, n), p = Math.pow(10, Math.floor(Math.log10(raw))), f = raw / p;
    return (f < 1.5 ? 1 : f < 3.5 ? 2 : f < 7.5 ? 5 : 10) * p;
  }
  function niceTicks(min, max, n) {
    if (!(max > min)) return [min];
    const st = niceStep(max - min, n), out = [];
    for (let v = Math.ceil(min / st - 1e-9) * st; v <= max + st * 1e-6; v += st) out.push(Math.round(v / st) * st);
    return out;
  }
  function timeTicks(x0, x1, maxN) {
    const span = x1 - x0;
    let step = STEPS[STEPS.length - 1];
    for (const s of STEPS) if (span / s <= maxN) { step = s; break; }
    const out = [];
    for (let t = Math.ceil(x0 / step) * step; t <= x1; t += step) out.push(t);
    return { step: step, ticks: out };
  }
  function fmtTick(t, step) {
    const d = new Date(t);
    if (step >= D || t % D === 0) return d.getUTCDate() + '.' + (d.getUTCMonth() + 1) + '.';
    return pad2(d.getUTCHours()) + ':' + pad2(d.getUTCMinutes());
  }
  function fmtDT(t) {
    const d = new Date(t);
    return d.getUTCDate() + '.' + (d.getUTCMonth() + 1) + '.' + d.getUTCFullYear() + ' ' + pad2(d.getUTCHours()) + ':' + pad2(d.getUTCMinutes());
  }
  function median(a) {
    if (!a.length) return 0;
    const b = a.slice().sort((x, y) => x - y), m = b.length >> 1;
    return b.length & 1 ? b[m] : (b[m - 1] + b[m]) / 2;
  }
  // nejbližší bod podle x (pole seřazené podle x)
  function nearest(pts, x) {
    let lo = 0, hi = pts.length - 1;
    if (hi < 0) return -1;
    while (hi - lo > 1) {
      const mid = (lo + hi) >> 1;
      if (pts[mid][0] < x) lo = mid; else hi = mid;
    }
    return Math.abs(pts[lo][0] - x) <= Math.abs(pts[hi][0] - x) ? lo : hi;
  }
  // zhuštění: průměr v pravidelných košech (zachová mezery)
  function bucket(pts, x0, x1, nb) {
    if (pts.length <= nb * 1.5) return pts;
    const sx = new Float64Array(nb), sy = new Float64Array(nb), cn = new Uint32Array(nb), w = (x1 - x0) / nb;
    for (const p of pts) {
      let k = Math.floor((p[0] - x0) / w);
      if (k < 0 || k >= nb) continue;
      sx[k] += p[0]; sy[k] += p[1]; cn[k]++;
    }
    const out = [];
    for (let k = 0; k < nb; k++) if (cn[k]) out.push([sx[k] / cn[k], sy[k] / cn[k]]);
    return out;
  }

  class Plot {
    constructor(cv) {
      this.cv = cv;
      this.ctx = cv.getContext('2d');
      this.o = null;
      this.hx = null;
      this._ds = new Map();
      const mv = (e) => {
        const r = cv.getBoundingClientRect();
        this.hx = e.clientX - r.left;
        for (const p of all) if (p !== this) p.clearHover();
        this.draw();
      };
      cv.addEventListener('pointermove', mv);
      cv.addEventListener('pointerdown', mv);
      cv.addEventListener('pointerleave', (e) => { if (e.pointerType === 'mouse') this.clearHover(); });
      cv.addEventListener('pointercancel', () => this.clearHover());
      if (window.ResizeObserver) new ResizeObserver(() => this.draw()).observe(cv);
      else window.addEventListener('resize', () => this.draw());
      all.push(this);
    }
    clearHover() { if (this.hx !== null) { this.hx = null; this.draw(); } }
    // o: {xmin,xmax,logx,xticks,xfmt,xfull,axes:[{min,max,ticks?,fmt?},{...}|null],series:[{name,color,pts,axis,fill,w,fmt}],
    //     lines:[{axis,y,color,dash}], marks:[{x0,x1,color,label}] (svislé pásy událostí, např. zalití)}
    set(o) {
      this.o = o;
      this._ds.clear();
      let dts = [];
      const s0 = o && o.series && o.series[0];
      if (s0 && s0.pts.length > 1) {
        const step = Math.max(1, Math.floor(s0.pts.length / 400));
        for (let i = step; i < s0.pts.length; i += step) dts.push((s0.pts[i][0] - s0.pts[i - step][0]) / step);
      }
      this.gap = Math.max(4 * median(dts), 120e3);
      this.hx = null;
      this.draw();
    }
    // vrací {v: body k vykreslení, gap: největší vzdálenost, která se ještě spojí čarou}
    _pts(s, pw) {
      const hit = this._ds.get(s);
      if (hit && hit.pw === pw) return hit;
      const o = this.o;
      const nb = Math.max(30, Math.floor(pw / 2));
      let v = s.pts, gap = this.gap;
      if (!o.logx && s.pts.length > nb * 1.5) {
        v = bucket(s.pts, o.xmin, o.xmax, nb);
        // sousední neprázdné koše jsou od sebe jednu šířku koše; prázdný koš = skutečná mezera
        gap = Math.max(this.gap, 1.6 * (o.xmax - o.xmin) / nb);
      }
      const r = { pw: pw, v: v, gap: gap };
      this._ds.set(s, r);
      return r;
    }
    draw() {
      const cv = this.cv, o = this.o;
      const w = cv.clientWidth, h = cv.clientHeight;
      if (!w || !h) return;
      const dpr = window.devicePixelRatio || 1;
      if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) {
        cv.width = Math.round(w * dpr);
        cv.height = Math.round(h * dpr);
      }
      const c = this.ctx;
      c.setTransform(dpr, 0, 0, dpr, 0, 0);
      c.clearRect(0, 0, w, h);
      this.stat = [];  // pro zkoušky: kolik souvislých úseků a bodů se nakreslilo
      if (!o) return;
      const ink = css('--ink'), mute = css('--mute'), line = css('--line'), card = css('--card');
      const L = o.axes[0] ? 40 : 10, R = o.axes[1] ? 40 : 10, T = 10, B = 24;
      const pw = w - L - R, ph = h - T - B;
      if (pw < 30 || ph < 30) return;
      const lx0 = o.logx ? Math.log10(o.xmin) : o.xmin, lx1 = o.logx ? Math.log10(o.xmax) : o.xmax;
      const X = (x) => L + ((o.logx ? Math.log10(Math.max(x, o.xmin)) : x) - lx0) / (lx1 - lx0) * pw;
      const Y = (a, y) => { const ax = o.axes[a]; return T + ph - (y - ax.min) / (ax.max - ax.min) * ph; };
      c.font = '11px system-ui,-apple-system,Segoe UI,Roboto,sans-serif';
      c.textBaseline = 'middle';
      c.lineWidth = 1;

      // vodorovná mřížka + popisky vlevo
      const a0 = o.axes[0];
      c.strokeStyle = line; c.fillStyle = mute; c.textAlign = 'right';
      for (const v of (a0.ticks || niceTicks(a0.min, a0.max, 4))) {
        const y = Math.round(Y(0, v)) + 0.5;
        c.beginPath(); c.moveTo(L, y); c.lineTo(L + pw, y); c.stroke();
        c.fillText(a0.fmt ? a0.fmt(v) : String(v), L - 6, y);
      }
      // popisky vpravo (druhá osa)
      const a1 = o.axes[1];
      if (a1) {
        c.textAlign = 'left';
        c.fillStyle = a1.color || mute;
        for (const v of (a1.ticks || niceTicks(a1.min, a1.max, 4))) c.fillText(a1.fmt ? a1.fmt(v) : String(v), L + pw + 6, Y(1, v));
      }
      // osa X
      let xt, xs = 0;
      if (o.xticks) xt = o.xticks;
      else { const tt = timeTicks(o.xmin, o.xmax, Math.max(2, Math.floor(pw / 62))); xt = tt.ticks; xs = tt.step; }
      c.fillStyle = mute; c.textBaseline = 'top';
      for (const t of xt) {
        const x = Math.round(X(t)) + 0.5;
        if (x < L - 1 || x > L + pw + 1) continue;
        c.strokeStyle = line;
        c.beginPath(); c.moveTo(x, T); c.lineTo(x, T + ph); c.stroke();
        const lab = o.xfmt ? o.xfmt(t) : fmtTick(t, xs);
        c.textAlign = x < L + 16 ? 'left' : (x > L + pw - 16 ? 'right' : 'center');
        c.fillText(lab, x, T + ph + 6);
      }
      c.textBaseline = 'middle';

      // události (zalití): pás od začátku do konce nárůstu + kapka nahoře
      this.markN = 0;
      for (const m of (o.marks || [])) {
        let x0 = X(m.x0), x1 = X(m.x1 === undefined ? m.x0 : m.x1);
        if (x1 < L || x0 > L + pw) continue;
        x0 = Math.max(L, x0); x1 = Math.min(L + pw, x1);
        if (x1 - x0 < 2) { const cx = (x0 + x1) / 2; x0 = cx - 1; x1 = cx + 1; }
        c.save();
        c.fillStyle = m.color || css('--water'); c.globalAlpha = 0.16;
        c.fillRect(x0, T, x1 - x0, ph);
        c.globalAlpha = 0.95;
        const cx = (x0 + x1) / 2;
        if (m.drop !== false) {  // kapka jen u zalití (hibernace je jen pás)
          c.beginPath(); c.moveTo(cx, T + 1); c.lineTo(cx - 4, T + 8); c.arc(cx, T + 9, 4, Math.PI * 1.15, Math.PI * 1.85, true); c.closePath();
          c.fill();
        }
        c.restore();
        this.markN++;
      }

      // prahové čáry
      for (const l of (o.lines || [])) {
        if (!o.axes[l.axis || 0]) continue;
        const y = Y(l.axis || 0, l.y);
        if (y < T || y > T + ph) continue;
        c.save();
        c.setLineDash(l.dash || [5, 4]);
        c.strokeStyle = l.color || mute; c.globalAlpha = 0.8; c.lineWidth = 1.25;
        c.beginPath(); c.moveTo(L, y); c.lineTo(L + pw, y); c.stroke();
        c.restore();
      }

      // řady
      for (const s of o.series) {
        const ai = s.axis || 0;
        if (!o.axes[ai] || !s.pts.length) continue;
        const ds = this._pts(s, pw);
        const segs = [];
        let cur = [], px = null;
        for (const p of ds.v) {
          if (px !== null && p[0] - px > ds.gap) { segs.push(cur); cur = []; }
          cur.push(p); px = p[0];
        }
        if (cur.length) segs.push(cur);
        this.stat.push({ name: s.name, segs: segs.length, pts: ds.v.length });
        c.save();
        c.beginPath(); c.rect(L, T - 2, pw, ph + 4); c.clip();
        if (s.fill) {
          const g = c.createLinearGradient(0, T, 0, T + ph);
          g.addColorStop(0, s.color + '55'); g.addColorStop(1, s.color + '08');
          c.fillStyle = g;
          for (const sg of segs) {
            if (sg.length < 2) continue;
            c.beginPath();
            c.moveTo(X(sg[0][0]), T + ph);
            for (const p of sg) c.lineTo(X(p[0]), Y(ai, p[1]));
            c.lineTo(X(sg[sg.length - 1][0]), T + ph);
            c.closePath(); c.fill();
          }
        }
        c.strokeStyle = s.color; c.fillStyle = s.color; c.lineWidth = s.w || 2; c.lineJoin = 'round'; c.lineCap = 'round';
        for (const sg of segs) {
          if (sg.length === 1) { c.beginPath(); c.arc(X(sg[0][0]), Y(ai, sg[0][1]), 2.5, 0, 6.2832); c.fill(); continue; }
          c.beginPath();
          sg.forEach((p, i) => { const x = X(p[0]), y = Y(ai, p[1]); if (i) c.lineTo(x, y); else c.moveTo(x, y); });
          c.stroke();
        }
        c.restore();
      }

      // tooltip
      if (this.hx !== null && this.hx >= L && this.hx <= L + pw) {
        const f = (this.hx - L) / pw;
        const xv = o.logx ? Math.pow(10, lx0 + f * (lx1 - lx0)) : lx0 + f * (lx1 - lx0);
        let base = null;
        const rows = [];
        for (const s of o.series) {
          if (!s.pts.length || !o.axes[s.axis || 0]) continue;
          const i = nearest(s.pts, xv);
          if (i < 0) continue;
          const p = s.pts[i];
          if (base === null) {
            if (Math.abs(X(p[0]) - this.hx) > 36) break;
            base = p[0];
          }
          if (Math.abs(p[0] - base) > Math.max(this.gap, 1)) continue;
          rows.push({ s: s, p: p });
        }
        if (base !== null && rows.length) {
          const bx = Math.round(X(base)) + 0.5;
          c.strokeStyle = mute; c.lineWidth = 1; c.setLineDash([3, 3]);
          c.beginPath(); c.moveTo(bx, T); c.lineTo(bx, T + ph); c.stroke(); c.setLineDash([]);
          for (const r of rows) {
            c.fillStyle = r.s.color;
            c.beginPath(); c.arc(X(r.p[0]), Y(r.s.axis || 0, r.p[1]), 3.5, 0, 6.2832); c.fill();
          }
          const head = o.xfull ? o.xfull(base) : fmtDT(base);
          c.font = '600 12px system-ui,-apple-system,Segoe UI,Roboto,sans-serif';
          let tw = c.measureText(head).width;
          const lines = rows.map((r) => r.s.name + ': ' + (r.s.fmt ? r.s.fmt(r.p[1]) : r.p[1]));
          c.font = '12px system-ui,-apple-system,Segoe UI,Roboto,sans-serif';
          for (const t of lines) tw = Math.max(tw, c.measureText(t).width + 14);
          const bw = tw + 16, bh = 22 + lines.length * 17;
          let bx0 = bx + 10;
          if (bx0 + bw > w - 2) bx0 = bx - 10 - bw;
          if (bx0 < 2) bx0 = 2;
          const by0 = T + 4;
          c.fillStyle = card; c.strokeStyle = line;
          c.beginPath();
          if (c.roundRect) c.roundRect(bx0, by0, bw, bh, 8); else c.rect(bx0, by0, bw, bh);
          c.fill(); c.stroke();
          c.textAlign = 'left'; c.textBaseline = 'middle';
          c.fillStyle = ink; c.font = '600 12px system-ui,-apple-system,Segoe UI,Roboto,sans-serif';
          c.fillText(head, bx0 + 8, by0 + 13);
          c.font = '12px system-ui,-apple-system,Segoe UI,Roboto,sans-serif';
          rows.forEach((r, i) => {
            const yy = by0 + 30 + i * 17;
            c.fillStyle = r.s.color; c.fillRect(bx0 + 8, yy - 2, 8, 4);
            c.fillStyle = ink; c.fillText(lines[i], bx0 + 22, yy);
          });
        }
      }
    }
  }
  Plot.fmtDT = fmtDT;
  Plot.all = all;
  Plot.forget = (p) => { const i = all.indexOf(p); if (i >= 0) all.splice(i, 1); };  // dočasné grafy (zpráva do PDF)
  Plot.redrawAll = () => all.forEach((p) => p.draw());
  document.addEventListener('pointerdown', (e) => { for (const p of all) if (e.target !== p.cv) p.clearHover(); }, true);
  if (window.matchMedia) {
    const mq = window.matchMedia('(prefers-color-scheme: dark)');
    if (mq.addEventListener) mq.addEventListener('change', Plot.redrawAll);
  }
  return Plot;
})();
