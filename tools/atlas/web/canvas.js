// canvas.js -- the center view.
//   Atlas mode: the page with sprite outlines (images solid, children dashed), selection, hover
//   tooltip, marquee select, drag an image to pin it there, page tabs.
//   Sprite edit mode (double-click a sprite): the sprite alone at its original size; draw a rect to
//   make a child sprite, move/resize child rects with handles, drag the pivot and 9-slice guides.
// Zoom: wheel (around the cursor), +/-; pan: middle drag or Space + drag; F = fit, 1 = 100%.
import { $, el, clamp, round3, prefs, setPref, promptDialog, toast } from './util.js';

const HANDLE = 4;   // half size of a resize handle, screen pixels

export class AtlasView {
  constructor(app, root) {
    this.app = app;
    this.root = root;
    this.vp = $('#viewport', root);
    this.canvas = $('#view', root);
    this.ctx = this.canvas.getContext('2d');
    this.tip = $('#tip', root);
    this.tabs = $('#pageTabs', root);
    this.zoomLabel = $('#zoomLabel', root);
    this.empty = $('#emptyHint', root);
    this.zoom = 1; this.panX = 0; this.panY = 0;
    this.fitPending = true;
    this.contentKey = '';
    this.drag = null;          // current pointer interaction
    this.live = null;          // edit preview while dragging: {kind, name, rect|pivot|split}
    this.hover = null;
    this.space = false;
    this.showOutlines = prefs().outlines !== false;
    this.raf = 0;

    $('#fitBtn', root).onclick = () => this.fit();
    $('#oneBtn', root).onclick = () => this.setZoom(1);
    $('#zoomInBtn', root).onclick = () => this.zoomBy(1.25);
    $('#zoomOutBtn', root).onclick = () => this.zoomBy(0.8);
    const ob = $('#outlinesBtn', root);
    ob.classList.toggle('active', this.showOutlines);
    ob.onclick = () => { this.showOutlines = !this.showOutlines; ob.classList.toggle('active', this.showOutlines); setPref('outlines', this.showOutlines); this.draw(); };
    $('#backBtn', root).onclick = () => app.setEdit(null);

    new ResizeObserver(() => this.resize()).observe(this.vp);
    window.addEventListener('resize', () => this.resize());
    const c = this.canvas;
    c.addEventListener('pointerdown', (e) => this.onDown(e));
    c.addEventListener('pointermove', (e) => this.onMove(e));
    c.addEventListener('pointerup', (e) => this.onUp(e));
    c.addEventListener('pointercancel', () => { this.drag = null; this.live = null; this.draw(); });
    c.addEventListener('pointerleave', () => { this.hover = null; this.tip.hidden = true; this.draw(); });
    c.addEventListener('dblclick', (e) => this.onDbl(e));
    c.addEventListener('wheel', (e) => this.onWheel(e), { passive: false });
    c.addEventListener('contextmenu', (e) => e.preventDefault());
    window.addEventListener('keyup', (e) => { if (e.code === 'Space') { this.space = false; this.updateCursor(); } });
    window.addEventListener('blur', () => { this.space = false; });

    app.on('build', () => { this.renderTabs(); this.checkContent(); this.draw(); });
    app.on('selection', () => this.draw());
    app.on('page', () => { this.renderTabs(); this.checkContent(); this.draw(); });
    app.on('project', () => this.draw());
    app.on('mode', () => { this.live = null; this.drag = null; this.fitPending = true; this.renderTabs(); this.checkContent(); this.draw(); });
    this.resize();
  }

  // ---- content ----------------------------------------------------------------------------------
  // What is shown: {w, h, image} for the current page or the sprite being edited.
  content() {
    const app = this.app;
    if (app.edit) {
      const r = app.regions.get(app.edit);
      if (!r) return null;
      return { w: r.origW, h: r.origH, image: app.regionCanvas(app.edit), region: r };
    }
    const page = app.pages[app.page];
    return page ? { w: page.width, h: page.height, image: page } : null;
  }

  checkContent() {
    const c = this.content();
    const key = c ? `${this.app.edit || 'page' + this.app.page}:${c.w}x${c.h}` : '';
    if (key !== this.contentKey) {
      const wasEmpty = !this.contentKey;
      const sameMode = this.contentKey.split(':')[0] === key.split(':')[0];
      this.contentKey = key;
      if (wasEmpty || !sameMode || this.fitPending) this.fitPending = true;
    }
    if (this.fitPending && c) { this.fitPending = false; this.fit(); }
    this.empty.hidden = !!c;
  }

  renderTabs() {
    const app = this.app;
    const editing = !!app.edit;
    $('#editBar', this.root).hidden = !editing;
    this.tabs.hidden = editing;
    if (editing) {
      const r = app.regions.get(app.edit);
      $('#editLabel', this.root).textContent = `Editing ${app.edit}` + (r ? `  (${r.origW}x${r.origH})` : '');
      return;
    }
    this.tabs.replaceChildren(...app.pages.map((p, i) => el('button', {
      class: i === app.page ? 'active' : '', title: `${app.result.pages[i].file} ${p.width}x${p.height}`,
      onclick: () => { app.page = i; app.emit('page'); },
    }, `${app.result.pages[i].file}`, el('small', {}, ` ${p.width}x${p.height}`))));
  }

  // ---- view transform ---------------------------------------------------------------------------
  resize() {
    const dpr = window.devicePixelRatio || 1;
    const w = Math.max(1, this.vp.clientWidth), h = Math.max(1, this.vp.clientHeight);
    this.canvas.width = Math.round(w * dpr);
    this.canvas.height = Math.round(h * dpr);
    this.canvas.style.width = w + 'px';
    this.canvas.style.height = h + 'px';
    this.drawNow();
  }
  fit() {
    const c = this.content();
    if (!c) return;
    const vw = this.vp.clientWidth, vh = this.vp.clientHeight;
    const z = Math.min((vw - 40) / c.w, (vh - 40) / c.h);
    this.zoom = clamp(z >= 1 ? Math.floor(z) : z, 0.05, this.app.edit ? 64 : 16);
    this.panX = Math.round((vw - c.w * this.zoom) / 2);
    this.panY = Math.round((vh - c.h * this.zoom) / 2);
    this.draw();
  }
  setZoom(z, cx = this.vp.clientWidth / 2, cy = this.vp.clientHeight / 2) {
    z = clamp(z, 0.05, 64);
    const wx = (cx - this.panX) / this.zoom, wy = (cy - this.panY) / this.zoom;
    this.zoom = z;
    this.panX = cx - wx * z;
    this.panY = cy - wy * z;
    this.draw();
  }
  zoomBy(f, cx, cy) { this.setZoom(this.zoom * f, cx, cy); }
  toWorld(e) {
    const b = this.canvas.getBoundingClientRect();
    const sx = e.clientX - b.left, sy = e.clientY - b.top;
    return { sx, sy, x: (sx - this.panX) / this.zoom, y: (sy - this.panY) / this.zoom };
  }
  sx(x) { return this.panX + x * this.zoom; }
  sy(y) { return this.panY + y * this.zoom; }

  // ---- drawing ----------------------------------------------------------------------------------
  draw() {
    if (this.raf) return;
    this.raf = requestAnimationFrame(() => { this.raf = 0; this.drawNow(); });
  }
  drawNow() {
    const ctx = this.ctx, dpr = window.devicePixelRatio || 1;
    const css = getComputedStyle(document.documentElement);
    const col = (n) => css.getPropertyValue(n).trim();
    this.colors = { bg: col('--canvas-bg'), c1: col('--checker-1'), c2: col('--checker-2'), outline: col('--outline'),
      child: col('--child'), sel: col('--sel'), hover: col('--hover'), pivot: col('--pivot'), split: col('--split'),
      text: col('--text'), pin: col('--pin') };
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.fillStyle = this.colors.bg;
    ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    if (this.zoomLabel) this.zoomLabel.textContent = Math.round(this.zoom * 100) + '%';
    const c = this.content();
    if (!c) return;
    // checkerboard in screen space (does not scale with the zoom)
    const x0 = this.sx(0), y0 = this.sy(0), w = c.w * this.zoom, h = c.h * this.zoom;
    ctx.save();
    ctx.beginPath();
    ctx.rect(x0, y0, w, h);
    ctx.clip();
    ctx.fillStyle = this.checker();
    ctx.fillRect(x0, y0, w, h);
    if (c.image) {
      ctx.imageSmoothingEnabled = this.zoom < 1;
      ctx.drawImage(c.image, x0, y0, w, h);
    }
    ctx.restore();
    ctx.strokeStyle = this.colors.outline;
    ctx.globalAlpha = 0.5;
    ctx.lineWidth = 1;
    ctx.strokeRect(Math.round(x0) - 0.5, Math.round(y0) - 0.5, Math.round(w) + 1, Math.round(h) + 1);
    ctx.globalAlpha = 1;
    if (this.app.edit) this.drawEdit(c); else this.drawAtlas();
  }

  checker() {
    const key = this.colors.c1 + this.colors.c2;
    if (this._checkerKey !== key) {
      const p = document.createElement('canvas');
      p.width = p.height = 16;
      const g = p.getContext('2d');
      g.fillStyle = this.colors.c1; g.fillRect(0, 0, 16, 16);
      g.fillStyle = this.colors.c2; g.fillRect(0, 0, 8, 8); g.fillRect(8, 8, 8, 8);
      this._checker = this.ctx.createPattern(p, 'repeat');
      this._checkerKey = key;
    }
    return this._checker;
  }

  rectPath(x, y, w, h, inset = 0) {
    const X = Math.round(this.sx(x)) + 0.5 + inset, Y = Math.round(this.sy(y)) + 0.5 + inset;
    const W = Math.round(this.sx(x + w)) - Math.round(this.sx(x)) - 1 - inset * 2;
    const H = Math.round(this.sy(y + h)) - Math.round(this.sy(y)) - 1 - inset * 2;
    this.ctx.beginPath();
    this.ctx.rect(X, Y, Math.max(0, W), Math.max(0, H));
  }

  pageRegions() {
    const app = this.app;
    return app.result ? app.result.regions.filter((r) => r.page === app.page) : [];
  }

  drawAtlas() {
    const ctx = this.ctx, app = this.app, C = this.colors;
    const sel = new Set(app.selection);
    const regions = this.pageRegions();
    if (this.showOutlines) {
      for (const r of regions) {
        if (r.aliasOf) continue;
        const [x, y, w, h] = r.frame;
        const kid = r.child && !r.baked;
        ctx.setLineDash(kid ? [4, 3] : []);
        ctx.strokeStyle = kid ? C.child : C.outline;
        ctx.globalAlpha = kid ? 0.9 : 0.55;
        this.rectPath(x, y, w, h, kid ? 1 : 0);
        ctx.stroke();
      }
      ctx.setLineDash([]);
      ctx.globalAlpha = 1;
    }
    for (const r of regions) {
      const entry = app.entry(r.name);
      if (entry?.pin && !r.aliasOf) {
        ctx.fillStyle = C.pin;
        ctx.beginPath();
        ctx.arc(this.sx(r.frame[0]) + 4, this.sy(r.frame[1]) + 4, 3, 0, Math.PI * 2);
        ctx.fill();
      }
    }
    if (this.hover && !sel.has(this.hover)) {
      const r = app.regions.get(this.hover);
      if (r && r.page === app.page) {
        const [x, y, w, h] = r.frame;
        ctx.fillStyle = C.hover; ctx.globalAlpha = 0.25;
        ctx.fillRect(this.sx(x), this.sy(y), w * this.zoom, h * this.zoom);
        ctx.globalAlpha = 1;
      }
    }
    for (const name of app.selection) {
      const r = app.regions.get(name);
      if (!r || r.page !== app.page) continue;
      const [x, y, w, h] = r.frame;
      ctx.fillStyle = C.sel; ctx.globalAlpha = 0.18;
      ctx.fillRect(this.sx(x), this.sy(y), w * this.zoom, h * this.zoom);
      ctx.globalAlpha = 1;
      ctx.strokeStyle = C.sel;
      ctx.lineWidth = name === app.current ? 2 : 1;
      ctx.setLineDash(r.child && !r.baked ? [4, 3] : []);
      this.rectPath(x, y, w, h);
      ctx.stroke();
      ctx.setLineDash([]);
      ctx.lineWidth = 1;
    }
    const d = this.drag;
    if (d && d.kind === 'move' && d.moved) {
      ctx.strokeStyle = C.pin; ctx.lineWidth = 2; ctx.setLineDash([6, 3]);
      this.rectPath(d.nx, d.ny, d.frame[2], d.frame[3]);
      ctx.stroke();
      ctx.setLineDash([]); ctx.lineWidth = 1;
      this.label(`pin ${d.nx},${d.ny}`, this.sx(d.nx), this.sy(d.ny) - 4);
    }
    if (d && d.kind === 'marquee' && d.moved) this.marquee(d);
  }

  marquee(d) {
    const ctx = this.ctx;
    const x = Math.min(d.sx0, d.sx1), y = Math.min(d.sy0, d.sy1);
    ctx.strokeStyle = this.colors.sel; ctx.setLineDash([3, 3]);
    ctx.strokeRect(x + 0.5, y + 0.5, Math.abs(d.sx1 - d.sx0), Math.abs(d.sy1 - d.sy0));
    ctx.setLineDash([]);
  }

  label(text, x, y) {
    const ctx = this.ctx;
    ctx.font = '11px system-ui, sans-serif';
    const w = ctx.measureText(text).width + 8;
    ctx.fillStyle = 'rgba(0,0,0,0.7)';
    ctx.fillRect(x, y - 15, w, 15);
    ctx.fillStyle = '#fff';
    ctx.fillText(text, x + 4, y - 4);
  }

  // ---- edit mode model --------------------------------------------------------------------------
  // The sprite being edited and its children, with live drag values applied.
  editModel() {
    const app = this.app, S = app.edit, r = app.regions.get(S);
    if (!r) return null;
    const live = this.live;
    const kids = app.project.sprites.filter((s) => s.parent === S).map((s) => ({
      name: s.name, rect: live && live.kind === 'rect' && live.name === s.name ? live.rect : (s.rect || [0, 0, 0, 0]), entry: s }));
    if (live && live.kind === 'new') kids.push({ name: '(new)', rect: live.rect, entry: {}, isNew: true });
    const focusName = app.current && (app.current === S || kids.some((k) => k.name === app.current)) ? app.current : S;
    const fk = kids.find((k) => k.name === focusName);
    const focusRect = fk ? fk.rect : [0, 0, r.origW, r.origH];
    const fe = app.entry(focusName) || {};
    const fr = app.regions.get(focusName);
    let pivot = fe.pivot || (fr ? fr.pivot : app.project.settings.defaultPivot) || [0.5, 0.5];
    let split = fe.split || null;
    if (live && live.name === focusName && live.kind === 'pivot') pivot = live.pivot;
    if (live && live.name === focusName && live.kind === 'split') split = live.split;
    return { S, r, kids, focusName, focusRect, pivot, split, isChildFocus: !!fk };
  }

  drawEdit() {
    const ctx = this.ctx, C = this.colors, m = this.editModel();
    if (!m) return;
    const { r } = m;
    // the trimmed area that is actually packed
    if (r.trimmed) {
      ctx.strokeStyle = C.outline; ctx.globalAlpha = 0.6; ctx.setLineDash([2, 3]);
      this.rectPath(r.offsetX, r.offsetY, r.frame[2], r.frame[3]);
      ctx.stroke();
      ctx.setLineDash([]); ctx.globalAlpha = 1;
    }
    for (const k of m.kids) {
      const [x, y, w, h] = k.rect;
      const selected = this.app.selection.includes(k.name) || k.isNew;
      ctx.strokeStyle = selected ? C.sel : C.child;
      ctx.lineWidth = selected ? 2 : 1;
      ctx.setLineDash(selected ? [] : [5, 3]);
      this.rectPath(x, y, w, h);
      ctx.stroke();
      if (selected) {
        ctx.fillStyle = C.sel; ctx.globalAlpha = 0.12;
        ctx.fillRect(this.sx(x), this.sy(y), w * this.zoom, h * this.zoom);
        ctx.globalAlpha = 1;
      }
      ctx.setLineDash([]); ctx.lineWidth = 1;
      if (this.zoom * Math.min(w, h) > 18 || selected) this.label(`${k.name} ${w}x${h}`, this.sx(x), this.sy(y));
    }
    // handles on the focused child
    if (m.isChildFocus) {
      ctx.fillStyle = C.sel;
      for (const h of this.handles(m.focusRect)) ctx.fillRect(h.x - HANDLE, h.y - HANDLE, HANDLE * 2, HANDLE * 2);
    }
    // 9-slice guides
    if (m.split) {
      const [fx, fy, fw, fh] = m.focusRect, [l, rr, t, b] = m.split;
      ctx.strokeStyle = C.split; ctx.setLineDash([6, 4]); ctx.lineWidth = 1;
      const vline = (x) => { ctx.beginPath(); ctx.moveTo(Math.round(this.sx(x)) + 0.5, this.sy(fy)); ctx.lineTo(Math.round(this.sx(x)) + 0.5, this.sy(fy + fh)); ctx.stroke(); };
      const hline = (y) => { ctx.beginPath(); ctx.moveTo(this.sx(fx), Math.round(this.sy(y)) + 0.5); ctx.lineTo(this.sx(fx + fw), Math.round(this.sy(y)) + 0.5); ctx.stroke(); };
      vline(fx + l); vline(fx + fw - rr); hline(fy + t); hline(fy + fh - b);
      ctx.setLineDash([]);
    }
    // pivot
    const p = this.pivotScreen(m);
    ctx.strokeStyle = C.pivot; ctx.lineWidth = 2;
    ctx.beginPath(); ctx.arc(p.x, p.y, 6, 0, Math.PI * 2); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(p.x - 10, p.y); ctx.lineTo(p.x + 10, p.y); ctx.moveTo(p.x, p.y - 10); ctx.lineTo(p.x, p.y + 10); ctx.stroke();
    ctx.lineWidth = 1;
  }

  pivotScreen(m) {
    const [fx, fy, fw, fh] = m.focusRect;
    return { x: this.sx(fx + m.pivot[0] * fw), y: this.sy(fy + m.pivot[1] * fh) };
  }

  handles(rect) {
    const [x, y, w, h] = rect;
    const xs = [this.sx(x), this.sx(x + w / 2), this.sx(x + w)], ys = [this.sy(y), this.sy(y + h / 2), this.sy(y + h)];
    const out = [];
    for (let j = 0; j < 3; j++) for (let i = 0; i < 3; i++) if (i !== 1 || j !== 1) out.push({ x: xs[i], y: ys[j], i, j });
    return out;
  }

  // ---- hit tests ----------------------------------------------------------------------------------
  hitAtlas(x, y) {
    let best = null, area = Infinity;
    for (const r of this.pageRegions()) {
      if (r.aliasOf) continue;
      const [rx, ry, rw, rh] = r.frame;
      if (x >= rx && y >= ry && x < rx + rw && y < ry + rh && rw * rh < area) { best = r; area = rw * rh; }
    }
    return best;
  }

  hitEdit(e) {
    const m = this.editModel();
    if (!m) return null;
    const { sx, sy, x, y } = this.toWorld(e);
    const p = this.pivotScreen(m);
    if (Math.hypot(sx - p.x, sy - p.y) <= 8) return { kind: 'pivot', m };
    if (m.split) {
      const [fx, fy, fw, fh] = m.focusRect, [l, r, t, b] = m.split;
      const inY = sy >= this.sy(fy) - 4 && sy <= this.sy(fy + fh) + 4, inX = sx >= this.sx(fx) - 4 && sx <= this.sx(fx + fw) + 4;
      const near = (a, b2) => Math.abs(a - b2) <= 4;
      if (inY && near(sx, this.sx(fx + l))) return { kind: 'split', side: 0, m };
      if (inY && near(sx, this.sx(fx + fw - r))) return { kind: 'split', side: 1, m };
      if (inX && near(sy, this.sy(fy + t))) return { kind: 'split', side: 2, m };
      if (inX && near(sy, this.sy(fy + fh - b))) return { kind: 'split', side: 3, m };
    }
    if (m.isChildFocus) {
      for (const h of this.handles(m.focusRect))
        if (Math.abs(sx - h.x) <= HANDLE + 2 && Math.abs(sy - h.y) <= HANDLE + 2) return { kind: 'resize', i: h.i, j: h.j, name: m.focusName, m };
    }
    let best = null, area = Infinity;
    for (const k of m.kids) {
      const [kx, ky, kw, kh] = k.rect;
      if (x >= kx && y >= ky && x < kx + kw && y < ky + kh && kw * kh < area) { best = k; area = kw * kh; }
    }
    if (best) return { kind: 'child', name: best.name, m };
    return { kind: 'none', m };
  }

  // ---- pointer ----------------------------------------------------------------------------------
  updateCursor(e) {
    let cur = this.space ? 'grab' : 'default';
    if (this.drag && this.drag.kind === 'pan') cur = 'grabbing';
    else if (!this.space && this.app.edit && e) {
      const h = this.hitEdit(e);
      if (h?.kind === 'pivot') cur = 'move';
      else if (h?.kind === 'split') cur = h.side < 2 ? 'ew-resize' : 'ns-resize';
      else if (h?.kind === 'resize') cur = (h.i === 1 ? 'ns' : h.j === 1 ? 'ew' : (h.i === h.j ? 'nwse' : 'nesw')) + '-resize';
      else if (h?.kind === 'child') cur = 'move';
      else cur = 'crosshair';
    }
    this.canvas.style.cursor = cur;
  }

  onDown(e) {
    this.canvas.setPointerCapture(e.pointerId);
    const w = this.toWorld(e);
    if (e.button === 1 || (e.button === 0 && this.space) || e.button === 2) {
      e.preventDefault();
      this.drag = { kind: 'pan', sx: w.sx, sy: w.sy, px: this.panX, py: this.panY };
      this.updateCursor(e);
      return;
    }
    if (e.button !== 0) return;
    const app = this.app;
    if (!app.edit) {
      const r = this.hitAtlas(w.x, w.y);
      const add = e.ctrlKey || e.metaKey || e.shiftKey;
      if (r) {
        if (add) app.select([r.name], { toggle: true });
        else if (!app.selection.includes(r.name)) app.select([r.name]);
        else app.select([r.name], { add: true });   // make it current
        const movable = (!r.child || r.baked) && !add;
        this.drag = movable ? { kind: 'move', name: r.name, frame: r.frame, x0: w.x, y0: w.y, nx: r.frame[0], ny: r.frame[1], moved: false } : null;
      } else {
        if (!add) app.select([]);
        this.drag = { kind: 'marquee', sx0: w.sx, sy0: w.sy, sx1: w.sx, sy1: w.sy, x0: w.x, y0: w.y, add, moved: false };
      }
      return;
    }
    // edit mode
    const h = this.hitEdit(e);
    const m = h.m;
    const px = Math.round(w.x), py = Math.round(w.y);
    if (h.kind === 'pivot') {
      this.drag = { kind: 'pivot', name: m.focusName };
      this.live = { kind: 'pivot', name: m.focusName, pivot: [...m.pivot] };
    } else if (h.kind === 'split') {
      this.drag = { kind: 'split', side: h.side, name: m.focusName };
      this.live = { kind: 'split', name: m.focusName, split: [...m.split] };
    } else if (h.kind === 'resize') {
      this.drag = { kind: 'resize', i: h.i, j: h.j, name: h.name, start: [...m.focusRect] };
      this.live = { kind: 'rect', name: h.name, rect: [...m.focusRect] };
    } else if (h.kind === 'child') {
      app.select([h.name], { add: e.ctrlKey || e.metaKey });
      const k = m.kids.find((x) => x.name === h.name);
      this.drag = { kind: 'moveChild', name: h.name, start: [...k.rect], x0: px, y0: py };
      this.live = { kind: 'rect', name: h.name, rect: [...k.rect] };
    } else {
      app.select([m.S]);
      this.drag = { kind: 'new', x0: clamp(px, 0, m.r.origW), y0: clamp(py, 0, m.r.origH), moved: false };
    }
    this.draw();
  }

  onMove(e) {
    const w = this.toWorld(e);
    const d = this.drag;
    if (!d) {
      this.updateCursor(e);
      this.updateHover(e, w);
      return;
    }
    this.tip.hidden = true;
    const app = this.app;
    if (d.kind === 'pan') {
      this.panX = d.px + (w.sx - d.sx); this.panY = d.py + (w.sy - d.sy);
    } else if (d.kind === 'move') {
      const page = app.pages[app.page];
      const dx = Math.round(w.x - d.x0), dy = Math.round(w.y - d.y0);
      if (Math.abs(dx) + Math.abs(dy) > 0) d.moved = true;
      d.nx = clamp(d.frame[0] + dx, 0, page.width - d.frame[2]);
      d.ny = clamp(d.frame[1] + dy, 0, page.height - d.frame[3]);
    } else if (d.kind === 'marquee') {
      d.sx1 = w.sx; d.sy1 = w.sy; d.x1 = w.x; d.y1 = w.y;
      if (Math.abs(d.sx1 - d.sx0) + Math.abs(d.sy1 - d.sy0) > 3) d.moved = true;
    } else {
      const m = this.editModel();
      if (!m) return;
      const W = m.r.origW, H = m.r.origH;
      const px = clamp(Math.round(w.x), 0, W), py = clamp(Math.round(w.y), 0, H);
      if (d.kind === 'pivot') {
        const [fx, fy, fw, fh] = m.focusRect;
        let vx = fw ? (w.x - fx) / fw : 0.5, vy = fh ? (w.y - fy) / fh : 0.5;
        if (!e.altKey) {   // snap to whole pixels (Alt: free)
          vx = fw ? Math.round(vx * fw * 2) / (fw * 2) : vx;
          vy = fh ? Math.round(vy * fh * 2) / (fh * 2) : vy;
        }
        this.live.pivot = [round3(vx), round3(vy)];
      } else if (d.kind === 'split') {
        const [fx, fy, fw, fh] = m.focusRect, s = this.live.split;
        if (d.side === 0) s[0] = clamp(px - fx, 0, fw - s[1]);
        if (d.side === 1) s[1] = clamp(fx + fw - px, 0, fw - s[0]);
        if (d.side === 2) s[2] = clamp(py - fy, 0, fh - s[3]);
        if (d.side === 3) s[3] = clamp(fy + fh - py, 0, fh - s[2]);
      } else if (d.kind === 'resize') {
        let [x, y, w2, h2] = d.start;
        let x2 = x + w2, y2 = y + h2;
        if (d.i === 0) x = Math.min(px, x2 - 1); if (d.i === 2) x2 = Math.max(px, x + 1);
        if (d.j === 0) y = Math.min(py, y2 - 1); if (d.j === 2) y2 = Math.max(py, y + 1);
        this.live.rect = [x, y, x2 - x, y2 - y];
      } else if (d.kind === 'moveChild') {
        const [x, y, cw, ch] = d.start;
        this.live.rect = [clamp(x + px - d.x0, 0, Math.max(0, W - cw)), clamp(y + py - d.y0, 0, Math.max(0, H - ch)), cw, ch];
      } else if (d.kind === 'new') {
        const x = Math.min(d.x0, px), y = Math.min(d.y0, py), w2 = Math.abs(px - d.x0), h2 = Math.abs(py - d.y0);
        if (w2 >= 1 && h2 >= 1) { d.moved = true; this.live = { kind: 'new', rect: [x, y, w2, h2] }; }
      }
      this.tipText(e, this.live && this.live.rect ? `${this.live.rect.join(', ')}` : this.live && this.live.pivot ? `pivot ${this.live.pivot.join(', ')}`
        : this.live && this.live.split ? `split ${this.live.split.join(', ')}` : '');
    }
    this.draw();
  }

  async onUp(e) {
    const d = this.drag;
    this.drag = null;
    if (!d) return;
    const app = this.app;
    const live = this.live;
    this.live = null;
    if (d.kind === 'move' && d.moved && (d.nx !== d.frame[0] || d.ny !== d.frame[1])) {
      app.mutate(`pin ${d.name}`, () => { app.ensureEntry(d.name).pin = [app.page, d.nx, d.ny]; });
    } else if (d.kind === 'marquee' && d.moved) {
      const x0 = Math.min(d.x0, d.x1), x1 = Math.max(d.x0, d.x1), y0 = Math.min(d.y0, d.y1), y1 = Math.max(d.y0, d.y1);
      const hit = this.pageRegions().filter((r) => !r.aliasOf && r.frame[0] < x1 && r.frame[0] + r.frame[2] > x0 && r.frame[1] < y1 && r.frame[1] + r.frame[3] > y0);
      app.select(hit.map((r) => r.name), { add: d.add });
    } else if (d.kind === 'pivot' && live) {
      app.mutate(`pivot ${d.name}`, () => { app.ensureEntry(d.name).pivot = live.pivot; });
    } else if (d.kind === 'split' && live) {
      app.mutate(`9-slice ${d.name}`, () => { app.ensureEntry(d.name).split = live.split; });
    } else if ((d.kind === 'resize' || d.kind === 'moveChild') && live) {
      const e2 = app.entry(d.name);
      if (e2 && live.rect.join() !== (e2.rect || []).join()) app.mutate(`rect ${d.name}`, () => { app.entry(d.name).rect = live.rect; });
    } else if (d.kind === 'new' && d.moved && live) {
      this.live = live;   // keep showing it while the name is asked
      this.draw();
      const S = app.edit;
      let n = 1;
      while (app.spriteExists(`${S}_${n}`)) n++;
      const name = await promptDialog('Name of the new child sprite', `${S}_${n}`, { okText: 'Create' });
      this.live = null;
      if (name) {
        if (app.spriteExists(name)) toast(`A sprite named "${name}" already exists`, 'error');
        else {
          app.mutate(`new child ${name}`, (p) => { p.sprites.push({ name, parent: S, rect: live.rect }); });
          app.select([name]);
        }
      }
    }
    this.draw();
    this.updateCursor(e);
  }

  onDbl(e) {
    const w = this.toWorld(e);
    if (!this.app.edit) {
      const r = this.hitAtlas(w.x, w.y);
      if (r) { this.app.select([r.name]); this.app.setEdit(r.name); }
    } else {
      const h = this.hitEdit(e);
      if (h && h.kind === 'none') this.app.setEdit(null);
    }
  }

  onWheel(e) {
    e.preventDefault();
    const w = this.toWorld(e);
    const f = Math.pow(1.0015, -e.deltaY * (e.deltaMode === 1 ? 16 : 1));
    this.zoomBy(f, w.sx, w.sy);
  }

  updateHover(e, w) {
    const app = this.app;
    if (app.edit) {
      const h = this.hitEdit(e);
      const name = h?.kind === 'child' ? h.name : null;
      const px = Math.floor(w.x), py = Math.floor(w.y);
      const r = app.regions.get(app.edit);
      const inside = r && px >= 0 && py >= 0 && px < r.origW && py < r.origH;
      this.tipText(e, inside ? `${px}, ${py}` + (name ? `  ${name}` : '') : '');
      return;
    }
    const r = this.hitAtlas(w.x, w.y);
    const name = r ? r.name : null;
    if (name !== this.hover) { this.hover = name; this.draw(); }
    if (!r) { this.tip.hidden = true; return; }
    const lines = [r.name,
      `frame ${r.frame.join(', ')}`,
      `size ${r.origW}x${r.origH}` + (r.trimmed ? ` (trim offset ${r.offsetX},${r.offsetY})` : ''),
      r.child ? `child of ${r.parent} @ ${r.local.join(', ')}` + (r.baked ? ' (baked)' : '') : null,
      app.entry(r.name)?.pin ? 'pinned' : null,
      this.aliasesOf(r.name)];
    this.tipText(e, lines.filter(Boolean).join('\n'));
  }
  aliasesOf(name) {
    const a = this.app.result.regions.filter((x) => x.aliasOf === name).map((x) => x.name);
    return a.length ? 'also: ' + a.join(', ') : null;
  }

  tipText(e, text) {
    if (!text) { this.tip.hidden = true; return; }
    this.tip.textContent = text;
    this.tip.hidden = false;
    const b = this.vp.getBoundingClientRect();
    let x = e.clientX - b.left + 14, y = e.clientY - b.top + 16;
    if (x + this.tip.offsetWidth > b.width - 4) x = e.clientX - b.left - this.tip.offsetWidth - 10;
    if (y + this.tip.offsetHeight > b.height - 4) y = e.clientY - b.top - this.tip.offsetHeight - 10;
    this.tip.style.left = x + 'px';
    this.tip.style.top = y + 'px';
  }

  // ---- keys (from app.js when no text field has focus) ------------------------------------------
  key(e) {
    if (e.code === 'Space') { if (!this.space) { this.space = true; this.updateCursor(); } e.preventDefault(); return; }
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    const k = e.key;
    if (k === 'f' || k === 'F') this.fit();
    else if (k === '1') this.setZoom(1);
    else if (k === '+' || k === '=') this.zoomBy(1.25);
    else if (k === '-') this.zoomBy(0.8);
    else if (k === 'PageDown' || k === 'PageUp') {
      const n = this.app.pages.length;
      if (n > 1 && !this.app.edit) { this.app.page = (this.app.page + (k === 'PageDown' ? 1 : n - 1)) % n; this.app.emit('page'); }
    } else if (k.startsWith('Arrow') && this.app.edit) {
      const app = this.app, cur = app.current, entry = cur && app.entry(cur);
      if (!entry || entry.parent !== app.edit || !entry.rect) return;
      e.preventDefault();
      const s = e.shiftKey ? 10 : 1;
      const dx = k === 'ArrowLeft' ? -s : k === 'ArrowRight' ? s : 0, dy = k === 'ArrowUp' ? -s : k === 'ArrowDown' ? s : 0;
      const r = app.regions.get(app.edit);
      app.mutate(`move ${cur}`, () => {
        const [x, y, w, h] = entry.rect;
        entry.rect = [clamp(x + dx, 0, Math.max(0, r.origW - w)), clamp(y + dy, 0, Math.max(0, r.origH - h)), w, h];
      });
    }
  }
}
