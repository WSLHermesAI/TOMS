// props.js -- the Properties panel: the selected sprite(s), atlas settings, output formats,
// reference folders and variants. Every edit is one undo step (app.mutate) and triggers a rebuild.
import { $, el, prefs, setPref, promptDialog, naturalCompare, round3, ALWAYS_EXPORTED } from './util.js';

const HEURISTICS = [['auto', 'Auto (best of all)'], ['bssf', 'Best short side fit'], ['blsf', 'Best long side fit'],
  ['baf', 'Best area fit'], ['bl', 'Bottom left'], ['cp', 'Contact point'], ['shelf', 'Shelf']];
const FORMAT_INFO = {
  atlas: 'libGDX / Spine 4 text atlas (+ x_ extensions)', 'atlas-spine3': 'strict Spine 3 atlas', plist: 'Cocos plist (one per page)',
  'tp-json': 'TexturePacker JSON hash (Phaser, Pixi)', pi: 'FM79979 PuzzleImage XML', rcss: 'RmlUi @spritesheet',
};

export class PropsPanel {
  constructor(app, root) {
    this.app = app;
    this.root = root;
    this.body = $('#propsBody', root);
    this.open = prefs().propsOpen || { sprite: true, atlas: true, output: true, refs: false, variants: false };
    const rerender = () => this.render();
    app.on('selection', rerender);
    app.on('build', rerender);
    app.on('project', rerender);
  }

  // ---- small field builders ----------------------------------------------------------------------
  section(id, title, ...content) {
    const d = el('details.section', { open: !!this.open[id] }, el('summary', {}, title), ...content);
    d.addEventListener('toggle', () => { this.open[id] = d.open; setPref('propsOpen', this.open); });
    return d;
  }
  row(label, ...inputs) { return el('label.prop', {}, el('span.k', {}, label), el('span.v', {}, ...inputs)); }
  num(key, value, onSet, { min, max, step = 1, width } = {}) {
    const i = el('input', { type: 'number', value: value ?? '', min, max, step, dataset: { key }, style: width ? { width } : undefined });
    i.addEventListener('change', () => {
      if (i.value === '') return;
      let v = Number(i.value);
      if (!Number.isFinite(v)) return;
      if (min !== undefined) v = Math.max(min, v);
      if (max !== undefined) v = Math.min(max, v);
      onSet(step === 1 ? Math.round(v) : v);
    });
    return i;
  }
  text(key, value, onSet, attrs = {}) {
    const i = el('input', { type: 'text', value: value ?? '', spellcheck: 'false', dataset: { key }, ...attrs });
    i.addEventListener('change', () => onSet(i.value));
    i.addEventListener('keydown', (e) => { if (e.key === 'Enter') i.blur(); });
    return i;
  }
  check(key, value, onSet, label) {
    const i = el('input', { type: 'checkbox', checked: !!value, dataset: { key } });
    i.addEventListener('change', () => onSet(i.checked));
    return label ? el('label.chk', {}, i, ' ', label) : i;
  }
  select(key, value, options, onSet) {
    const s = el('select', { dataset: { key } }, ...options.map(([v, t]) => el('option', { value: v, selected: v === value }, t)));
    s.value = value;
    s.addEventListener('change', () => onSet(s.value));
    return s;
  }
  set(label, fn) { this.app.mutate(label, fn); }

  // ---- render ------------------------------------------------------------------------------------
  render() {
    const app = this.app;
    if (!app.project) return;
    const focusKey = document.activeElement && this.root.contains(document.activeElement) ? document.activeElement.dataset.key : null;
    const scroll = this.body.scrollTop;
    const parts = [];
    if (app.selection.length === 1) parts.push(this.spriteSection(app.selection[0]));
    else if (app.selection.length > 1) parts.push(this.multiSection(app.selection));
    parts.push(this.atlasSection(), this.outputSection(), this.referencesSection(), this.variantsSection());
    this.body.replaceChildren(...parts);
    this.body.scrollTop = scroll;
    if (focusKey) {
      const f = this.body.querySelector(`[data-key="${CSS.escape(focusKey)}"]`);
      if (f) f.focus();
    }
  }

  spriteSection(name) {
    const app = this.app;
    const rs = app.resolvedMap.get(name);
    const r = app.regions.get(name);
    const e = app.entry(name) || {};
    const isChild = !!e.parent || (rs && rs.child);
    const upd = (label, fn) => this.set(`${label} ${name}`, () => fn(app.ensureEntry(name)));
    const rows = [];
    rows.push(this.row('Name', this.text('s-name', name, (v) => { if (!app.renameSprite(name, v.trim())) this.render(); })));
    if (!isChild && app.variant) {
      const has = app.ws.hasImage(name, app.variant);
      rows.push(this.row('Art', el('span.ro', {}, has ? `variant ${app.variant}` : 'base art (no variant art)')));
    }
    if (r) {
      rows.push(this.row('Atlas', el('span.ro', {}, `page ${r.page}  ${r.frame.join(', ')}`)));
      rows.push(this.row('Size', el('span.ro', {}, `${r.origW}x${r.origH}` + (r.trimmed ? `  trimmed to ${r.frame[2]}x${r.frame[3]} @ ${r.offsetX},${r.offsetY}` : ''))));
      if (r.aliasOf) rows.push(this.row('Dedupe', el('span.ro', {}, 'same pixels as ' + r.aliasOf)));
    } else rows.push(this.row('Atlas', el('span.ro', {}, rs && rs.exclude ? 'excluded' : 'not in the atlas')));
    if (isChild) {
      const parents = app.resolved.filter((s) => !s.child && s.name !== name).map((s) => s.name).sort(naturalCompare);
      if (e.parent && !parents.includes(e.parent)) parents.unshift(e.parent);
      rows.push(this.row('Parent', this.select('s-parent', e.parent, parents.map((p) => [p, p]), (v) => upd('parent of', (x) => { x.parent = v; }))));
      const rect = e.rect || [0, 0, 0, 0];
      const ri = (i, k) => this.num('s-rect' + i, rect[i], (v) => upd('rect of', (x) => { x.rect = [...(x.rect || [0, 0, 0, 0])]; x.rect[i] = v; }), { min: i < 2 ? 0 : 1, width: '4.2em' });
      rows.push(this.row('Rect', el('span.grid4', {}, ri(0), ri(1), ri(2), ri(3))), el('div.hint', {}, 'x, y, w, h in the parent\'s original pixels'));
      rows.push(this.row('', this.check('s-bake', e.bake, (v) => upd('bake', (x) => { if (v) x.bake = true; else delete x.bake; }), 'Bake (own copy of the pixels)')));
      rows.push(this.row('', el('button', { onclick: () => app.setEdit(name) }, 'Edit in parent')));
    } else {
      rows.push(this.row('Trim', this.select('s-trim', e.trim === undefined ? '' : e.trim ? 'on' : 'off',
        [['', `project (${app.project.settings.trim ? 'on' : 'off'})`], ['on', 'on'], ['off', 'off']],
        (v) => upd('trim', (x) => { if (v === '') delete x.trim; else x.trim = v === 'on'; }))));
      if (r) rows.push(this.row('', el('button', { onclick: () => app.setEdit(name) }, 'Edit sprite (children, pivot, 9-slice)')));
    }
    // pivot
    const pv = e.pivot || (r ? r.pivot : app.project.settings.defaultPivot);
    const pivotOn = !!e.pivot;
    const pin = (i) => this.num('s-pivot' + i, pv[i], (v) => upd('pivot', (x) => { x.pivot = [...(x.pivot || pv)]; x.pivot[i] = round3(v); }), { step: 0.01, width: '5em' });
    const presets = el('span.pivot-grid', { title: 'pivot presets' }, ...[0, 0.5, 1].flatMap((y) => [0, 0.5, 1].map((x) =>
      el('button', { class: pivotOn && pv[0] === x && pv[1] === y ? 'active' : '', onclick: (ev) => { ev.preventDefault(); upd('pivot', (o) => { o.pivot = [x, y]; }); } }))));
    rows.push(this.row('Pivot', this.check('s-pivoton', pivotOn, (v) => upd('pivot', (x) => { if (v) x.pivot = [...pv]; else delete x.pivot; })),
      pin(0), pin(1), presets));
    // 9-slice
    const sp = e.split || [0, 0, 0, 0];
    const si = (i) => this.num('s-split' + i, sp[i], (v) => upd('9-slice', (x) => { x.split = [...(x.split || [0, 0, 0, 0])]; x.split[i] = v; }), { min: 0, width: '4.2em' });
    rows.push(this.row('9-slice', this.check('s-spliton', !!e.split, (v) => upd('9-slice', (x) => {
      if (v) { const w = r ? r.origW : 12, h = r ? r.origH : 12; const q = Math.max(1, Math.floor(Math.min(w, h) / 3)); x.split = [q, q, q, q]; } else delete x.split;
    }))));
    if (e.split) rows.push(this.row('', el('span.grid4', {}, si(0), si(1), si(2), si(3))), el('div.hint', {}, 'left, right, top, bottom (pixels)'));
    // pin
    if (!isChild || e.bake) {
      const pinOn = !!e.pin;
      rows.push(this.row('Pin', this.check('s-pinon', pinOn, (v) => upd(v ? 'pin' : 'unpin', (x) => {
        if (v) x.pin = r ? [r.page, r.frame[0], r.frame[1]] : [0, 0, 0]; else delete x.pin;
      }), 'fixed place')));
      if (pinOn) {
        const pi = (i) => this.num('s-pin' + i, e.pin[i], (v) => upd('pin', (x) => { x.pin = [...x.pin]; x.pin[i] = v; }), { min: 0, width: '4.2em' });
        rows.push(this.row('', el('span.grid3', {}, pi(0), pi(1), pi(2))), el('div.hint', {}, 'page, x, y (top-left of the packed pixels); or drag it on the page'));
      }
    }
    rows.push(this.row('Tags', this.text('s-tags', (e.tags || []).join(', '), (v) => upd('tags', (x) => {
      const t = v.split(',').map((s) => s.trim()).filter(Boolean);
      if (t.length) x.tags = t; else delete x.tags;
    }), { placeholder: 'comma separated' })));
    rows.push(el('div.actions', {},
      isChild ? null : el('button', { title: app.variant ? `replace the art of variant ${app.variant}` : 'replace the image', onclick: () => app.replaceImage(name) }, 'Replace image...'),
      r ? el('button', { onclick: () => app.saveImage(name) }, 'Save image') : null,
      el('button.danger', { onclick: () => app.deleteSelected() }, 'Delete')));
    return this.section('sprite', `Sprite`, ...rows);
  }

  multiSection(names) {
    const app = this.app;
    const each = (label, fn) => this.set(`${label} ${names.length} sprites`, () => { for (const n of names) fn(app.ensureEntry(n), n); });
    return this.section('sprite', `${names.length} sprites selected`,
      el('div.hint', {}, names.slice(0, 12).join(', ') + (names.length > 12 ? ', ...' : '')),
      this.row('Trim', this.select('m-trim', '-', [['-', '...'], ['', 'project'], ['on', 'on'], ['off', 'off']],
        (v) => { if (v !== '-') each('trim', (x) => { if (v === '') delete x.trim; else x.trim = v === 'on'; }); })),
      this.row('Pins', el('button', { onclick: () => each('unpin', (x) => { delete x.pin; }) }, 'Unpin all')),
      this.row('Tags', this.text('m-tags', '', (v) => each('tag', (x) => {
        const t = v.split(',').map((s) => s.trim()).filter(Boolean);
        x.tags = [...new Set([...(x.tags || []), ...t])];
      }), { placeholder: 'add tags, comma separated' })),
      el('div.actions', {}, el('button.danger', { onclick: () => app.deleteSelected() }, `Delete ${names.length}`)));
  }

  atlasSection() {
    const app = this.app, s = app.project.settings;
    const S = (label, k) => (v) => this.set(label, (p) => { p.settings[k] = v; });
    return this.section('atlas', 'Atlas',
      this.row('Name', this.text('a-name', app.project.name, (v) => this.set('name', (p) => { p.name = v.trim() || 'atlas'; }))),
      this.row('Max size', el('span.grid2', {}, this.num('a-maxw', s.maxWidth, S('max width', 'maxWidth'), { min: 16, max: 16384 }),
        this.num('a-maxh', s.maxHeight, S('max height', 'maxHeight'), { min: 16, max: 16384 }))),
      this.row('Min size', el('span.grid2', {}, this.num('a-minw', s.minWidth, S('min width', 'minWidth'), { min: 1, max: 16384 }),
        this.num('a-minh', s.minHeight, S('min height', 'minHeight'), { min: 1, max: 16384 }))),
      this.row('Size', el('span.wrap', {}, this.check('a-pot', s.powerOfTwo, S('power of two', 'powerOfTwo'), 'power of 2'),
        this.check('a-sq', s.square, S('square', 'square'), 'square'), this.check('a-fixed', s.fixedSize, S('fixed size', 'fixedSize'), 'fixed = max'))),
      this.row('Padding', this.num('a-pad', s.padding, S('padding', 'padding'), { min: 0, max: 64 })),
      this.row('Border', this.num('a-border', s.border, S('border', 'border'), { min: 0, max: 64 })),
      this.row('Extrude', this.num('a-extr', s.extrude, S('extrude', 'extrude'), { min: 0, max: 16 })),
      this.row('Trim', el('span.wrap', {}, this.check('a-trim', s.trim, S('trim', 'trim'), 'transparent edges'))),
      this.row('Alpha <=', this.num('a-alpha', s.alphaThreshold, S('alpha threshold', 'alphaThreshold'), { min: 0, max: 254 })),
      this.row('Pixels', el('span.wrap', {}, this.check('a-dedupe', s.dedupe, S('dedupe', 'dedupe'), 'dedupe'),
        this.check('a-pma', s.premultiplyAlpha, S('premultiply', 'premultiplyAlpha'), 'premultiply alpha'))),
      this.row('Filter', this.select('a-filter', s.filter, [['Nearest', 'Nearest'], ['Linear', 'Linear']], S('filter', 'filter'))),
      this.row('Packing', this.select('a-heur', s.heuristic, HEURISTICS, S('heuristic', 'heuristic'))),
      this.row('Pivot', el('span.grid2', {},
        this.num('a-pvx', s.defaultPivot[0], (v) => this.set('default pivot', (p) => { p.settings.defaultPivot = [round3(v), p.settings.defaultPivot[1]]; }), { step: 0.01 }),
        this.num('a-pvy', s.defaultPivot[1], (v) => this.set('default pivot', (p) => { p.settings.defaultPivot = [p.settings.defaultPivot[0], round3(v)]; }), { step: 0.01 }))));
  }

  outputSection() {
    const app = this.app;
    const v = app.variant;
    const out = app.outputFor(v);
    const edit = (label, fn) => this.set(label, (p) => fn(v ? p.variants.find((x) => x.id === v).output : p.output));
    return this.section('output', v ? `Output (variant ${v})` : 'Output',
      this.row('Formats', el('div.formats', {}, ...app.ws.formats.map((f) => {
        if (f === ALWAYS_EXPORTED) {   // Export writes it whatever the project lists
          const box = el('input', { type: 'checkbox', checked: true, disabled: true });
          return el('label.chk', { title: 'Export always writes the .plist (Cocos Creator Sprite Atlas)' }, box, ' ', f);
        }
        return el('label.chk', { title: FORMAT_INFO[f] || f },
          this.check('o-f-' + f, out.formats.includes(f), (on) => edit(`format ${f}`, (o) => {
            o.formats = on ? app.ws.formats.filter((x) => x === f || o.formats.includes(x)) : o.formats.filter((x) => x !== f);
          })), ' ', f);
      }))),
      this.row('File name', this.text('o-name', out.name || '', (val) => edit('output name', (o) => { if (val.trim()) o.name = val.trim(); else delete o.name; }),
        { placeholder: app.project.name })),
      this.row('Folder', this.text('o-dir', out.dir, (val) => edit('output folder', (o) => { o.dir = val.trim() || '.'; }))),
      el('div.hint', {}, 'Save writes these formats (always with the .atlas, which holds the images, and the .plist) into the folder, relative to the project; Export files downloads just them.'));
  }

  referencesSection() {
    const app = this.app;
    const refs = app.project.references || [];
    const rows = refs.map((s, i) => {
      const files = app.ws.list(app.ws.resolvePath(s.path)).filter((f) => f.toLowerCase().endsWith('.png')).length;
      return el('div.listrow', {},
        el('div.line', {}, this.text('ref-path' + i, s.path, (v) => this.set('reference path', (p) => { p.references[i].path = v.trim(); })),
          el('button.icon', { title: 'remove this reference folder', onclick: () => this.set('remove reference', (p) => { p.references.splice(i, 1); }) }, 'x')),
        el('div.line.small', {},
          this.check('ref-rec' + i, s.recursive, (v) => this.set('reference subfolders', (p) => { p.references[i].recursive = v; }), 'subfolders'),
          this.text('ref-pre' + i, s.prefix, (v) => this.set('reference prefix', (p) => { p.references[i].prefix = v; }), { placeholder: 'name prefix' }),
          el('span', { class: 'ro', title: app.ws.resolvePath(s.path) }, files ? `${files} PNG(s) loaded` : 'not loaded')));
    });
    return this.section('refs', `References (${refs.length})`, ...rows,
      el('div.actions', {},
        el('button', { onclick: async () => {
          const v = await promptDialog('Reference folder (relative to the project)', '../sprites');
          if (v) this.set('add reference', (p) => { (p.references ||= []).push({ path: v, recursive: true, prefix: '' }); });
        } }, '+ Reference folder'),
        el('button', { onclick: () => document.getElementById('refsBtn').click() }, 'Import from references...')),
      el('div.hint', {}, 'Folders to import new or changed art from. The images themselves live in the packed atlas; a browser cannot keep folder paths, so the dialog asks for each folder once per session.'));
  }

  variantsSection() {
    const app = this.app;
    const rows = app.project.variants.map((v, i) => el('div.listrow', {},
      el('div.line', {}, this.text('var-id' + i, v.id, (x) => this.set('variant id', (p) => { p.variants[i].id = x.trim(); })),
        el('button.icon', { title: 'remove this variant', onclick: () => this.set('remove variant', (p) => { p.variants.splice(i, 1); }) }, 'x')),
      el('div.line.small', {}, el('span.k', {}, 'reference'),
        this.text('var-dir' + i, v.reference || '', (x) => this.set('variant reference', (p) => { p.variants[i].reference = x.trim(); }), { placeholder: 'folder to import its art from' })),
      el('div.line.small', {}, el('span.ro', {}, `${(v.images || []).length} replacement image(s)`))));
    return this.section('variants', `Variants (${app.project.variants.length})`, ...rows,
      el('div.actions', {}, el('button', { onclick: async () => {
        const id = await promptDialog('Variant id (art style)', 'hd');
        if (!id) return;
        this.set('add variant', (p) => { p.variants.push({ id, output: { ...p.output, formats: [...p.output.formats], name: (p.name + '_' + id) } }); });
      } }, '+ Variant')),
      el('div.hint', {}, 'A variant replaces images by name and writes its own atlas. Pick it in the toolbar, then add, replace or import its art.'));
  }
}
