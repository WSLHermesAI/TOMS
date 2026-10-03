// anims.js -- the Animations panel: create/rename/delete animations, edit frames and times,
// and a looping preview (frames drawn at their original size, aligned on their pivots).
import { $, el, prefs, setPref, promptDialog, naturalCompare, round3 } from './util.js';

export class AnimsPanel {
  constructor(app, root) {
    this.app = app;
    this.root = root;
    this.current = prefs().anim || null;
    this.playing = true;
    this.frame = 0;
    this.t0 = 0;
    this.visible = false;
    app.on('project', () => this.render());
    app.on('build', () => this.render());
    window.addEventListener('atlas-tab', (e) => { this.visible = e.detail === 'anims'; if (this.visible) this.render(); });
    this.tick = this.tick.bind(this);
    requestAnimationFrame(this.tick);
  }

  anim() { return this.app.project?.animations.find((a) => a.name === this.current) || null; }

  render() {
    const app = this.app;
    if (!app.project || !this.visible) return;
    const anims = app.project.animations;
    if (!this.anim() && anims.length) this.current = anims[0].name;
    const a = this.anim();
    const focusKey = document.activeElement && this.root.contains(document.activeElement) ? document.activeElement.dataset.key : null;
    const list = el('div.anim-list', {},
      el('div.anim-head', {}, el('b', {}, 'Animations'),
        el('button', { title: 'new animation from the selected sprites (or empty)', onclick: () => this.create() }, '+ New')),
      el('ul', {}, ...anims.map((x) => el('li', {
        class: x.name === this.current ? 'active' : '', onclick: () => { this.current = x.name; setPref('anim', x.name); this.frame = 0; this.render(); },
      }, x.name, el('small', {}, ` ${x.frames.length} fr`)))),
      anims.length ? null : el('div.hint', {}, 'Select sprites (in order) and press + New.'));
    let frames = null, preview = null;
    if (a) {
      const idx = anims.indexOf(a);
      const upd = (label, fn) => app.mutate(`${label} ${a.name}`, (p) => fn(p.animations[idx]));
      const names = app.resolved.filter((s) => !s.exclude).map((s) => s.name).sort(naturalCompare);
      const dl = el('datalist#animSprites', {}, ...names.map((n) => el('option', { value: n })));
      frames = el('div.anim-frames', {},
        el('div.anim-head', {},
          el('input', { type: 'text', value: a.name, dataset: { key: 'an-name' }, onchange: (e) => {
            const v = e.target.value.trim();
            if (!v || anims.some((x) => x !== a && x.name === v)) { e.target.value = a.name; return; }
            upd('rename', (x) => { x.name = v; });
            this.current = v; setPref('anim', v);
          } }),
          el('label.chk', {}, el('input', { type: 'checkbox', checked: a.loop, dataset: { key: 'an-loop' }, onchange: (e) => upd('loop', (x) => { x.loop = e.target.checked; }) }), ' loop'),
          el('button', { title: 'append the selected sprites', onclick: () => upd('add frames', (x) => { for (const n of app.selection) x.frames.push({ sprite: n, time: x.frames.at(-1)?.time ?? 0.1 }); }) }, '+ Selected'),
          el('button', { title: 'set every frame time', onclick: async () => {
            const v = await promptDialog('Time of every frame (seconds)', String(a.frames[0]?.time ?? 0.1));
            const t = Number(v);
            if (v && t > 0) upd('frame times', (x) => { for (const f of x.frames) f.time = round3(t); });
          } }, 'All times...'),
          el('button.danger', { onclick: () => { app.mutate(`delete animation ${a.name}`, (p) => { p.animations.splice(idx, 1); }); this.current = null; } }, 'Delete')),
        dl,
        el('ol', {}, ...a.frames.map((f, i) => el('li', { class: i === this.frame % Math.max(1, a.frames.length) ? 'playing' : '' },
          el('span.n', {}, String(i + 1)),
          el('input.sprite', { type: 'text', value: f.sprite, list: 'animSprites', dataset: { key: 'an-s' + i }, class: app.regions.has(f.sprite) ? '' : 'bad',
            onchange: (e) => upd('frame sprite', (x) => { x.frames[i].sprite = e.target.value.trim(); }) }),
          el('input.time', { type: 'number', step: '0.01', min: '0.001', value: f.time, dataset: { key: 'an-t' + i }, title: 'seconds',
            onchange: (e) => { const t = Number(e.target.value); if (t > 0) upd('frame time', (x) => { x.frames[i].time = round3(t); }); } }),
          el('button.icon', { title: 'move up', disabled: i === 0, onclick: () => upd('move frame', (x) => { x.frames.splice(i - 1, 0, x.frames.splice(i, 1)[0]); }) }, '↑'),
          el('button.icon', { title: 'move down', disabled: i === a.frames.length - 1, onclick: () => upd('move frame', (x) => { x.frames.splice(i + 1, 0, x.frames.splice(i, 1)[0]); }) }, '↓'),
          el('button.icon', { title: 'remove', onclick: () => upd('remove frame', (x) => { x.frames.splice(i, 1); }) }, 'x')))));
      this.canvas = el('canvas.anim-preview', { width: 128, height: 128 });
      preview = el('div.anim-play', {}, this.canvas,
        el('div.anim-ctl', {},
          el('button', { onclick: (e) => { this.playing = !this.playing; e.target.textContent = this.playing ? 'Pause' : 'Play'; } }, this.playing ? 'Pause' : 'Play'),
          el('span.ro', {}, `${a.frames.reduce((s, f) => s + f.time, 0).toFixed(2)} s`)));
    } else this.canvas = null;
    this.root.replaceChildren(list, frames || el('div.anim-frames.empty', {}, 'No animation selected.'), preview || el('div'));
    if (focusKey) { const f = this.root.querySelector(`[data-key="${CSS.escape(focusKey)}"]`); if (f) f.focus(); }
  }

  async create() {
    const app = this.app;
    const sel = [...app.selection];
    let base = sel.length ? sel[0].replace(/[_\-]?\d+$/, '') : 'anim';
    if (!base) base = 'anim';
    let name = base, n = 2;
    while (app.project.animations.some((a) => a.name === name)) name = base + '_' + n++;
    const v = await promptDialog('Name of the new animation', name, { okText: 'Create' });
    if (!v || app.project.animations.some((a) => a.name === v)) return;
    app.mutate(`new animation ${v}`, (p) => { p.animations.push({ name: v, loop: true, frames: sel.map((s) => ({ sprite: s, time: 0.1 })) }); });
    this.current = v;
    setPref('anim', v);
    this.render();
  }

  tick(now) {
    requestAnimationFrame(this.tick);
    const a = this.anim();
    if (!this.visible || !this.canvas || !a || !a.frames.length) return;
    if (this.playing) {
      if (!this.t0) this.t0 = now;
      const time = a.frames[this.frame % a.frames.length]?.time || 0.1;
      if (now - this.t0 >= time * 1000) {
        this.t0 = now;
        const next = this.frame + 1;
        this.frame = a.loop ? next % a.frames.length : Math.min(next, a.frames.length - 1);
        const rows = this.root.querySelectorAll('.anim-frames ol li');
        rows.forEach((r, i) => r.classList.toggle('playing', i === this.frame));
      }
    }
    this.drawFrame(a);
  }

  drawFrame(a) {
    const app = this.app, c = this.canvas, ctx = c.getContext('2d');
    ctx.clearRect(0, 0, c.width, c.height);
    // Common box around every frame's pivot so the frames line up.
    let l = 0, t = 0, r = 0, b = 0;
    for (const f of a.frames) {
      const rg = app.regions.get(f.sprite);
      if (!rg) continue;
      const px = rg.pivot[0] * rg.origW, py = rg.pivot[1] * rg.origH;
      l = Math.max(l, px); t = Math.max(t, py); r = Math.max(r, rg.origW - px); b = Math.max(b, rg.origH - py);
    }
    const w = l + r, h = t + b;
    if (!w || !h) return;
    const s = Math.max(1, Math.floor(Math.min((c.width - 8) / w, (c.height - 8) / h))) || Math.min((c.width - 8) / w, (c.height - 8) / h);
    const f = a.frames[this.frame % a.frames.length];
    const rg = f && app.regions.get(f.sprite);
    const img = rg && app.regionCanvas(f.sprite);
    if (!img) return;
    const ox = (c.width - w * s) / 2 + l * s, oy = (c.height - h * s) / 2 + t * s;
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(img, ox - rg.pivot[0] * rg.origW * s, oy - rg.pivot[1] * rg.origH * s, rg.origW * s, rg.origH * s);
  }
}
